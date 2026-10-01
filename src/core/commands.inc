volatile bool cancel_search = false;
void os_yield(void) {
    extern uint16_t read_kbd(void);
    uint16_t key = read_kbd();
    if (key == 0x011B) { // Esc key? Wait, Esc is just 0x11B maybe, or Ctrl+C
        cancel_search = true;
    }
}



/* =========================================================================
   SHELL PROCESSOR
   Main terminal command loop and file system navigator
   ========================================================================= */

static char rpt_cmd_buffer[128];
static int rpt_count_remaining = 0;
static uint32_t rpt_start_time = 0;
static volatile uint32_t rpt_magic = 0;
static volatile bool rpt_password_given = false;
void handle_enter(void);

void try_print_prompt(void) {
    if (rpt_count_remaining == 0) {
        print_prompt();
    }
}

void check_and_run_rpt(void) {
    /* If rpt count is done, always stop — regardless of sys_state */
    if (rpt_magic == 0xDEADBEEF && rpt_count_remaining == 0) {
        rpt_magic = 0;
        rpt_password_given = false;
        if (sys_state == 0) print_prompt();
        return;
    }

    /* Auto-inject password/confirmation ONLY while rpt is active with remaining iterations */
    if (rpt_magic == 0xDEADBEEF && rpt_count_remaining > 0 && rpt_password_given && sys_state != 0) {
        bool is_pass = (sys_state == 2 || sys_state == 3 || sys_state == 5 ||
                        sys_state == 6 || sys_state == 7 || sys_state == 8 ||
                        sys_state == 10 || sys_state == 12 || sys_state == 13 ||
                        sys_state == 15 || sys_state == 17 || sys_state == 18 ||
                        sys_state == 19 || sys_state == 20 || sys_state == 21 ||
                        sys_state == 22 || sys_state == 24 || sys_state == 25 ||
                        sys_state == 26 || sys_state == 31 || sys_state == 80 ||
                        sys_state == 81 || sys_state == 84 || sys_state == 85);
        bool is_conf = (sys_state == 27 || sys_state == 32 || sys_state == 33);
        if (is_pass) {
            clear_buffer();
            str_cpy(kbd_buffer, passwords[cur_user_index]);
            kbd_index = str_len(kbd_buffer);
            handle_enter();
        } else if (is_conf) {
            clear_buffer();
            kbd_buffer[0] = 'Y';
            kbd_buffer[1] = '\0';
            kbd_index = 1;
            handle_enter();
        }
        return;
    }

    /* Run next rpt iteration when idle */
    if (rpt_magic == 0xDEADBEEF && rpt_count_remaining > 0 && sys_state == 0) {
        rpt_count_remaining--;
        clear_buffer();
        str_cpy(kbd_buffer, rpt_cmd_buffer);
        kbd_index = str_len(rpt_cmd_buffer);
        kbd_cursor = kbd_index;
        handle_enter();
    }
}
static char ed_lines[1024][80];
static int ed_line_count = 0;
static int ed_cx = 0, ed_cy = 0;
static int ed_sy = 0;
static bool ed_saved = true;

static void ed_format_stat(char *buf, uint32_t val, const char *suffix) {
    char suf = ' ';
    uint64_t disp = val;
    if (val >= 1000000000UL) { disp = val / 1000000000UL; suf = 'B'; }
    else if (val >= 1000000UL) { disp = val / 1000000UL; suf = 'm'; }
    else if (val >= 1000UL) { disp = val / 1000UL; suf = 'k'; }
    int d1 = (disp / 100) % 10;
    int d2 = (disp / 10) % 10;
    int d3 = disp % 10;
    buf[0] = (disp >= 100) ? ('0' + d1) : ' ';
    buf[1] = (disp >= 10)  ? ('0' + d2) : ' ';
    buf[2] = '0' + d3;
    buf[3] = ' ';
    buf[4] = suf;
    buf[5] = ' ';
    buf[6] = '<';
    buf[7] = ' ';
    for (int i=0; i<5; i++) buf[8+i] = suffix[i];
}

static void ed_draw() {
    uint16_t* vmem = (uint16_t*)0xB8000;
    for (int y = 0; y < 21; y++) {
        int doc_y = ed_sy + y;
        for (int x = 0; x < 80; x++) {
            char c = ' ';
            uint16_t term_col_attr = ((uint16_t)vga_get_color()) << 8;
            if (doc_y < ed_line_count) {
                char ch = ed_lines[doc_y][x];
                if (ch != '\0') c = ch;
                else {
                    for (int rem = x; rem < 80; rem++) vmem[y * 80 + rem] = (uint16_t)' ' | term_col_attr;
                    break;
                }
            }
            vmem[y * 80 + x] = (uint16_t)c | term_col_attr;
        }
    }
    
    uint32_t chars = 0, words = 0;
    for (int i=0; i<ed_line_count; i++) {
        bool in_word = false;
        for (int j=0; j<80 && ed_lines[i][j]; j++) {
            chars++;
            if (ed_lines[i][j] != ' ') { if (!in_word) { words++; in_word = true; } }
            else in_word = false;
        }
    }
    uint32_t pages = (ed_line_count + 29) / 30;
    
    uint16_t st_col = ed_saved ? 0x2F00 : 0x4F00; // Green/Red background, White text
    
    // Line 21: Hyphens
    for (int x = 0; x < 80; x++) vmem[21 * 80 + x] = (uint16_t)'-' | 0x0F00; // Black background, White text
    
    // Formatter buffers
    char s_char[13], s_word[13], s_page[13];
    ed_format_stat(s_char, chars, "CHARS");
    ed_format_stat(s_word, words, "WORDS");
    ed_format_stat(s_page, pages, "PAGES");
    
    const char *l22 = "ESC (SAVE) / ESC+SHIFT (SAVE&EXIT)                                 ";
    for (int x = 0; x < 67; x++) vmem[22 * 80 + x] = (uint16_t)l22[x] | st_col;
    for (int x = 0; x < 13; x++) vmem[22 * 80 + 67 + x] = (uint16_t)s_char[x] | st_col;
    
    const char *l23 = "   Cut      Copy      paste      Undo      Redo      move          ";
    for (int x = 0; x < 67; x++) vmem[23 * 80 + x] = (uint16_t)l23[x] | st_col;
    for (int x = 0; x < 13; x++) vmem[23 * 80 + 67 + x] = (uint16_t)s_word[x] | st_col;
    
    const char *l24 = "CTRL + X  CTRL + C   CTRL + V  CTRL + Z  CTRL + Y   ARROWS         ";
    for (int x = 0; x < 67; x++) vmem[24 * 80 + x] = (uint16_t)l24[x] | st_col;
    for (int x = 0; x < 13; x++) vmem[24 * 80 + 67 + x] = (uint16_t)s_page[x] | st_col;
    
    int cy_scr = ed_cy - ed_sy;
    if (cy_scr >= 0 && cy_scr < 21) {
        outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t) ((cy_scr * 80 + ed_cx) & 0xFF));
        outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t) (((cy_scr * 80 + ed_cx) >> 8) & 0xFF));
    }
}

static char editor_buf[65536];
int editor_len = 0;
char editor_fname[280];

extern bool is_redirecting;
extern char redirect_buf[8192];
extern int redirect_len;

static char clipboard_fname[280] = {0};
static uint32_t clipboard_dir_clus = 0;
static bool clipboard_is_cut = false;

static char srcsys_query[64];
static bool srcsys_content_mode = false;
static void _srcsys_cb(const char *name, uint8_t attr, uint32_t size, uint32_t clus) {
    (void)size; (void)clus;
    bool is_dir = (attr & 0x10) != 0;
    if (is_dir && !srcsys_content_mode && str_cmp(srcsys_query, "dir") == 0) {
        vga_print("  "); vga_print(name); vga_print(" (DIR)\n");
        return;
    }
    
    if (!srcsys_content_mode) {
        int qlen = str_len(srcsys_query);
        int nlen = str_len(name);
        bool match = false;
        if (qlen > 0) {
            for (int i = 0; i <= nlen - qlen; i++) {
                bool found = true;
                for (int j = 0; j < qlen; j++) {
                    if (name[i+j] != srcsys_query[j]) { found = false; break; }
                }
                if (found) { match = true; break; }
            }
        }
        
        if (match) {
            vga_print("  "); vga_print(name);
            if (is_dir) vga_print(" (DIR)\n");
            else vga_print("\n");
        }
    } else {
        if (is_dir) return;
        extern uint8_t _xep_buf[];
        uint32_t sz = fat32_readfile(cur_dir_clus, name, _xep_buf, 131072);
        if (sz > 0) {
            int qlen = str_len(srcsys_query);
            for (uint32_t i = 0; i <= sz - qlen; i++) {
                bool found = true;
                for (int j = 0; j < qlen; j++) {
                    if (_xep_buf[i+j] != srcsys_query[j]) { found = false; break; }
                }
                if (found) {
                    vga_print("  "); vga_print(name); vga_print(" (Match found)\n");
                    break;
                }
            }
        }
    }
}

void handle_enter() {
    extern bool shift_pressed;
    shift_pressed = false; // Workaround for dropped shift-release scancodes

    kbd_buffer[kbd_index] = '\0'; 
    vga_print("\n");
    
    uint32_t start_time = get_uptime_seconds();

    if (sys_state == 0) {
        if (kbd_index > 0) {
            add_to_history(kbd_buffer);
        }
		
		if (str_cmp(kbd_buffer, "cr") == 0 || str_starts_with(kbd_buffer, "cr ")) {
		    if (str_cmp(kbd_buffer, "cr") == 0) {
		        vga_print("===========================================\n");
		        vga_print("              AVAILABLE COLORS             \n");
		        vga_print("===========================================\n");
		        vga_print("black                            light_gray\n");
		        vga_print("blue                              dark_gray\n");
		        vga_print("green                            light_blue\n");
		        vga_print("cyan                            light_green\n");
		        vga_print("red                              light_cyan\n");
		        vga_print("magenta                           light_red\n");
		        vga_print("brown                         light_magenta\n");
		        vga_print("yellow                                     \n");
		        vga_print("white                                      \n");
		    } else {
		        char* args = kbd_buffer + 3;
		        int i = 0;
        
		        while (args[i] != ' ' && args[i] != '\0') {
		            i++;
		        }
		        args[i] = '\0';
		        char* bg_color = args + i + 1;

		        const char* color_names[16] = {
		            "black", "blue", "green", "cyan", "red", "magenta", "brown", "light_gray",
		            "dark_gray", "light_blue", "light_green", "light_cyan", "light_red", "light_magenta", "yellow", "white"
		        };

		        uint8_t fg = 15;
		        for (int c = 0; c < 16; c++) {
		            if (str_cmp(args, color_names[c]) == 0) fg = c;
		        }

		        uint8_t bg = 0;
		        for (int d = 0; d < 16; d++) {
		            if (str_cmp(bg_color, color_names[d]) == 0) bg = d;
		        }

		        if (fg == bg) {
		            vga_print("Error: Foreground and background colors cannot be the same!\n");
		        } else {
                    if (cur_user_index >= 0 && cur_user_index < users_count) {
                        user_fg_colors[cur_user_index] = fg;
                        user_bg_colors[cur_user_index] = bg;
                        save_system_users();
                    }
		            vga_set_color(fg, bg);
		            vga_recolor_screen();
		        }
		    }
		}
        else if (str_cmp(kbd_buffer, "cl") == 0) {
            vga_clear();
        }
        else if (str_cmp(kbd_buffer, "nm") == 0) {
            bool new_val = !show_user;
            for (int i = 0; i < 12; i++) sessions[i].show_user_flag = new_val;
            restore_current_banner();
            if (new_val) vga_print("Welcome message username: ENABLED\n");
            else vga_print("Welcome message username: DISABLED\n");
        }

        else if (str_cmp(kbd_buffer, "sys") == 0) {
            vga_print("   /\\      OS / Kernel: VIATAhos "); vga_print(OS_VER_STRING); vga_print("\n");
            vga_print("  /  \\     CPU: "); print_cpu_hardware_info(); vga_print("\n");
            vga_print(" /____\\    GPU: "); print_gpu_hardware_info(); vga_print("\n");
            vga_print(" [][][]    Disk: "); print_disk_info(); vga_print("\n");
            vga_print(" ::::::    RAM: "); print_ram_info(); vga_print("\n");
            vga_print(" ......    Display: "); print_display_info(); vga_print("\n");
            vga_print("           Uptime: ");
            uint32_t uptime = get_uptime_seconds();
            print_two_digits((uint8_t)(uptime / 3600)); vga_print("h ");
            print_two_digits((uint8_t)((uptime % 3600) / 60)); vga_print("m ");
            print_two_digits((uint8_t)(uptime % 60)); vga_print("s\n");
        }
        else if (str_cmp(kbd_buffer, "hs") == 0) {
            vga_print("Command History:\n");
            for (int i = 0; i < sessions[current_session].history_count; i++) {
                vga_print("  "); print_two_digits((uint8_t)(i + 1)); vga_print(". ");
                vga_print(sessions[current_session].history[i]); vga_print("\n");
            }
        }
        else if (str_cmp(kbd_buffer, "upwt") == 0) { print_uptime(); }
        else if (str_cmp(kbd_buffer, "wt") == 0) { print_rtc_datetime(); }
        else if (str_cmp(kbd_buffer, "wtw") == 0) { print_last_cmd_time(); }
        else if (str_starts_with(kbd_buffer, "w ")) {
            int secs = 0;
            const char* p = kbd_buffer + 2;
            while(*p >= '0' && *p <= '9') {
                secs = secs * 10 + (*p - '0');
                p++;
            }
            if (secs > 0) {
                uint32_t st = get_uptime_seconds();
                while (1) {
                    uint32_t cur = get_uptime_seconds();
                    uint32_t el = cur >= st ? cur - st : 0;
                    if (el >= (uint32_t)secs) break;
                    for (volatile int i = 0; i < 500000; i++) {} // Delay to prevent CMOS hammering
                    os_yield();
                    if (cancel_search) { cancel_search = false; break; }
                }
            }
        }
        else if (str_starts_with(kbd_buffer, "rpt(")) {
            char cmd_to_run[128];
            int i = 4, j = 0;
            while (kbd_buffer[i] != ')' && kbd_buffer[i] != '\0' && j < 127) {
                cmd_to_run[j++] = kbd_buffer[i++];
            }
            cmd_to_run[j] = '\0';
            if (kbd_buffer[i] == ')') {
                i++;
                int times = 0;
                while (kbd_buffer[i] >= '0' && kbd_buffer[i] <= '9') {
                    times = times * 10 + (kbd_buffer[i] - '0');
                    i++;
                }
                if (times > 0 && cmd_to_run[0]) {
                    if (times > 1000) times = 1000;
                    str_cpy(rpt_cmd_buffer, cmd_to_run);
                    rpt_count_remaining = times;
                    rpt_start_time = get_uptime_seconds();
                    rpt_magic = 0xDEADBEEF;
                    rpt_password_given = false;
                } else {
                    vga_print("Usage: rpt(cmd)N\n");
                }
            } else {
                vga_print("Usage: rpt(cmd)N\n");
            }
        }
        else if (str_starts_with(kbd_buffer, "cf ") || str_starts_with(kbd_buffer, "xf ")) {
            bool is_cut = kbd_buffer[0] == 'x';
            const char *args = kbd_buffer + 3;
            char type[20]; char name_arg[280];
            int ti=0; while(*args && *args!=' ' && ti<19) type[ti++]=*args++; type[ti]='\0';
            while(*args==' ') args++;
            int ni=0; while(*args && *args!=' ' && ni<250) name_arg[ni++]=*args++; name_arg[ni]='\0';
            
            if (!type[0] || !name_arg[0]) {
                vga_print("Usage: "); vga_print(is_cut ? "xf" : "cf"); vga_print(" ext name\n");
            } else {
                char fname[280];
                str_cpy(fname, name_arg); str_cat(fname, "."); str_cat(fname, type);
                
                uint32_t fsize = 0;
                uint32_t clus = fat32_find(cur_dir_clus, fname, 0, &fsize);
                if (clus != 0xFFFFFFFF) {
                    str_cpy(clipboard_fname, fname);
                    clipboard_dir_clus = cur_dir_clus;
                    clipboard_is_cut = is_cut;
                    vga_print("File copied to clipboard.\n");
                } else {
                    vga_print("Error: File not found.\n");
                }
            }
        }
        else if (str_starts_with(kbd_buffer, "vf")) {
            if (!clipboard_fname[0]) {
                vga_print("Error: Clipboard is empty.\n");
            } else {
                const char *args = kbd_buffer + 2;
                while(*args == ' ') args++;
                
                char fname_out[72];
                if (*args) {
                    char type[20]; char name_arg[280];
                    int ti=0; while(*args && *args!=' ' && ti<19) type[ti++]=*args++; type[ti]='\0';
                    while(*args==' ') args++;
                    int ni=0; while(*args && *args!=' ' && ni<250) name_arg[ni++]=*args++; name_arg[ni]='\0';
                    if (!type[0] || !name_arg[0]) {
                        vga_print("Usage: vf OR vf ext name\n");
                        goto vf_end;
                    }
                    str_cpy(fname_out, name_arg); str_cat(fname_out, "."); str_cat(fname_out, type);
                } else {
                    str_cpy(fname_out, clipboard_fname);
                }
                
                uint32_t tsize=0;
                char base_name[72];
                str_cpy(base_name, fname_out);
                int counter = 1;
                while (fat32_find(cur_dir_clus, fname_out, 0, &tsize) != 0xFFFFFFFF && counter < 100) {
                    char n_part[64], e_part[8];
                    int idx=0, n_idx=0, e_idx=0;
                    while(base_name[idx] && base_name[idx] != '.') n_part[n_idx++] = base_name[idx++];
                    n_part[n_idx] = '\0';
                    if (base_name[idx] == '.') {
                        idx++;
                        while(base_name[idx]) e_part[e_idx++] = base_name[idx++];
                    }
                    e_part[e_idx] = '\0';
                    
                    str_cpy(fname_out, n_part);
                    str_cat(fname_out, "_");
                    char num_str[8];
                    int c = counter, d = 0;
                    while(c > 0) { num_str[d++] = '0' + (c % 10); c /= 10; }
                    for(int z=0; z<d/2; z++) { char t = num_str[z]; num_str[z] = num_str[d-1-z]; num_str[d-1-z] = t; }
                    num_str[d] = '\0';
                    str_cat(fname_out, num_str);
                    if (e_part[0]) { str_cat(fname_out, "."); str_cat(fname_out, e_part); }
                    counter++;
                }

                if (fat32_find(cur_dir_clus, fname_out, 0, &tsize) != 0xFFFFFFFF) {
                    vga_print("Error: Destination file already exists.\n");
                } else {
                    uint32_t sz = fat32_readfile(clipboard_dir_clus, clipboard_fname, _xep_buf, 131072);
                    if (sz != 0xFFFFFFFF) {
                        if (fat32_mkfile(cur_dir_clus, fname_out, _xep_buf, sz)) {
                            if (clipboard_is_cut) {
                                fat32_delete(clipboard_dir_clus, clipboard_fname);
                                clipboard_fname[0] = '\0';
                                vga_print("File moved successfully.\n");
                            } else {
                                vga_print("File pasted successfully.\n");
                            }
                        } else {
                            vga_print("Error: Disk full or invalid operation.\n");
                        }
                    } else {
                        vga_print("Error: Source file read failed.\n");
                    }
                }
            vf_end:;
            }
        }
        else if (str_starts_with(kbd_buffer, "fs ")) {
            const char *args = kbd_buffer + 3;
            char type[20]; char name_arg[280]; char cmd_to_run[128];
            int ti=0; while(*args && *args!=' ' && ti<19) type[ti++]=*args++; type[ti]='\0';
            while(*args==' ') args++;
            int ni=0; while(*args && *args!=' ' && ni<249) name_arg[ni++]=*args++; name_arg[ni]='\0';
            while(*args==' ') args++;
            int ci=0; while(*args && ci<127) cmd_to_run[ci++]=*args++; cmd_to_run[ci]='\0';
            
            if (!type[0] || !name_arg[0] || !cmd_to_run[0]) {
                vga_print("Usage: fs ext name cmd\n");
            } else {
                is_redirecting = true;
                redirect_len = 0;
                
                str_cpy(kbd_buffer, cmd_to_run);
                kbd_index = str_len(cmd_to_run);
                handle_enter();
                
                is_redirecting = false;
                char fname[280];
                str_cpy(fname, name_arg); str_cat(fname, "."); str_cat(fname, type);
                
                int fnl = str_len(fname);
                bool is_smol = (fnl > 5 && str_cmp(fname + fnl - 5, ".smol") == 0);
                if (is_smol) {
                    char oext[16] = {0};
                    int tl = str_len(type);
                    int bsz = (tl > 5) ? tl - 5 : 0;
                    for (int k = 0; k < bsz && k < 13; k++) oext[k] = type[k];
                    oext[bsz < 13 ? bsz : 13] = '\0';
                    uint32_t csz = smol_compress((const uint8_t*)redirect_buf, redirect_len, _smol_dst, sizeof(_smol_dst), oext);
                    fat32_delete(cur_dir_clus, fname);
                    if (csz > 0 && fat32_mkfile(cur_dir_clus, fname, _smol_dst, csz)) {
                        vga_print("Output saved and compressed to "); vga_print(fname); vga_print("\n");
                    } else {
                        vga_print("Error saving compressed file.\n");
                    }
                } else {
                    fat32_delete(cur_dir_clus, fname);
                    if (fat32_mkfile(cur_dir_clus, fname, (const uint8_t*)redirect_buf, redirect_len)) {
                        vga_print("Output saved to "); vga_print(fname); vga_print("\n");
                    } else {
                        vga_print("Error saving file.\n");
                    }
                }
            }
        }
        else if (str_cmp(kbd_buffer, "by&") == 0 || (kbd_buffer[0] == 'b' && kbd_buffer[1] == 'y' && kbd_buffer[2] == '&' && kbd_buffer[3] == '\0')) {
			vga_print(by_text_start); vga_print("\n");
        }
        else if (str_starts_with(kbd_buffer, "srcsys ")) {
            const char *args = kbd_buffer + 7;
            while(*args == ' ') args++;
            
            if (*args == '\0') {
                vga_print("Usage: srcsys ext name OR srcsys dir OR srcsys \"text\"\n");
            } else {
                srcsys_content_mode = false;
                if (str_cmp(args, "dir") == 0) {
                    str_cpy(srcsys_query, "dir");
                } else if (*args == '"') {
                    args++;
                    int i=0; while(args[i] && args[i]!='"' && i<63) { srcsys_query[i]=args[i]; i++; }
                    srcsys_query[i]='\0';
                    srcsys_content_mode = true;
                } else {
                    int i=0; while(args[i] && args[i]!=' ' && i<63) { srcsys_query[i]=args[i]; i++; }
                    srcsys_query[i]='\0';
                }
                vga_print("Search results:\n");
                fat32_list_dir(sessions[current_session].fs_cluster_stack[0], _srcsys_cb);
            }
        }

        else if (str_starts_with(kbd_buffer, "updk")) {
            if (kn_mode) {
                sys_state = 27;
                vga_print("Are you sure you want to format the secondary disk? (Y/N): ");
            } else {
                sys_state = 26;
                vga_print("Password: ");
            }
        }
        else if (str_cmp(kbd_buffer, "fixfs") == 0) {
            if (kn_mode) {
                vga_print("Wiping root directory to fix corruption...\n");
                uint32_t root_lba = _f32_cluster_to_lba(fat32_root());
                uint8_t root_sec[512];
                f32_mem_set(root_sec, 0, 512);
                for(uint32_t s=0; s<FAT32_SECS_CLUS; s++) {
                    ata_write_sector(current_ata_base, current_ata_sel, root_lba + s, root_sec);
                }
                save_system_users();
                vga_print("Root directory fixed! (Old corrupted files are lost)\n");
            } else {
                sys_state = 87;
                vga_print("Password: ");
            }
        }
        else if (str_cmp(kbd_buffer, "h") == 0) {
            vga_print("===========================================\n");
            vga_print("           VIATAhos Command List           \n");
            vga_print("===========================================\n");
             vga_print("   bnsys   - List files in Recycle Bin\n");
             vga_print("   by&     - OS Credits\n");
             vga_print("   cf      - Copy a file to clipboard\n");
             vga_print("   chgnm   - Change current shell name\n");
             vga_print(" * chgsys  - Log out of account\n");
             vga_print("   cl      - Clears the screen\n");
             vga_print(" * clbn    - Securely empty Recycle Bin\n");
             vga_print("   cr      - Change color of the screen\n");
             vga_print("   di      - Edit text files\n");
             vga_print("   dl      - Delete a file\n");
             vga_print(" * dlbn    - Securely delete a file from Recycle Bin\n");
             vga_print(" * dlsys   - Delete current user account\n");
             vga_print("   do      - Rename or format a file\n");
             vga_print("   fs      - Save command output to file\n");
             vga_print("   ft      - File system navigation/tree\n");
             vga_print("   h       - Open this list\n");
             vga_print("   hs      - Show command history\n");
             vga_print(" * kn      - Makes kn commands passwordless\n");
             vga_print("   kpt     - Suspend system\n");
             vga_print("   mk      - Create file/folder\n");
             vga_print(" * mksys   - Create a new system user\n");
             vga_print("   nm      - Toggle username in welcome banner\n");
             vga_print("   qd      - Append text to a file quickly\n");
             vga_print(" * rebsys  - Reboot the computer\n");
             vga_print("   rpt     - Repeat a command\n");
             vga_print(" * scr     - Configure auto-suspend timer\n");
             vga_print("   shnm    - Toggle shell name in prompt\n");
             vga_print(" * shtsys  - Shutdown the computer\n");
             vga_print("   smol    - File compression\n");
             vga_print("   srcbn   - Search inside Recycle Bin\n");
             vga_print("   srcsys  - Search system\n");
             vga_print("   sys     - Show CPU and system info\n");
             vga_print(" * sysnm   - Change current user name\n");
             vga_print(" * syspw   - Change user password\n");
             vga_print("   unbn    - Restore file from Recycle Bin\n");
             vga_print(" * updk    - Format secondary disk (Admin)\n");
             vga_print("   upwt    - Show system uptime\n");
             vga_print("   vf      - Paste file from clipboard\n");
             vga_print("   w       - Wait for N seconds\n");
             vga_print("   wt      - Show full date and system time\n");
             vga_print("   wtw     - Show last command execution time\n");
             vga_print("   xf      - Cut a file to clipboard\n");
             vga_print("   xp      - Execute file/script\n");

        }
		else if (str_cmp(kbd_buffer, "kpt") == 0) {
		    sys_state = 21;
		    vga_disable_banner();
		    vga_clear();
		    vga_print("=== SYSTEM SUSPENDED ===\nEnter password to wake up: ");
		    vga_screen_off();
		    suspend_screen_active = false;
		}
        else if (str_cmp(kbd_buffer, "chgsys") == 0) {
            if (kn_mode) global_logout(4);
            else { sys_state = 7; vga_print("Password: "); }
        }
        else if (str_starts_with(kbd_buffer, "sysnm ") || str_cmp(kbd_buffer, "sysnm") == 0) {
            if (kbd_buffer[5] == ' ') {
                vga_print("Usage: sysnm (no arguments)\n");
            } else {
                sys_state = 82;
                vga_print("New Username: ");
            }
        }
        else if (str_cmp(kbd_buffer, "chgnm") == 0) {
            vga_print("Usage: chgnm <name>\n");
        }
        else if (str_starts_with(kbd_buffer, "chgnm ")) {
            if (cur_user_index < 0 || cur_user_index >= users_count) {
                vga_print("Error: No user logged in!\n");
            } else {
                const char* name = kbd_buffer + 6;
                while (*name == ' ') name++;
                int len = 0; while (name[len] != '\0') len++;
                if (len == 0 || len > 15) {
                    vga_print("Error: Invalid shell name! Must be 1-15 characters.\n");
                } else {
                    str_cpy(user_shell_names[cur_user_index][current_session], name);
                    str_cpy(sessions[current_session].shell_name, name);
                    save_system_users();
                    vga_print("Shell name changed to: ");
                    vga_print(name);
                    vga_print("\n");
                    restore_current_banner();
                }
            }
        }
        else if (str_cmp(kbd_buffer, "mksys") == 0) {
            if (users_count >= MAX_USERS) {
                vga_print("Error: Maximum user limit reached!\n");
            } else {
                if (kn_mode) {
                    vga_disable_banner(); vga_clear();
                    vga_print("Create Account\nUsername: "); sys_state = 9;
                } else {
                    sys_state = 8; vga_print("Password: ");
                }
            }
        }
        else if (str_cmp(kbd_buffer, "dlsys") == 0) {
            if (users_count <= 1) vga_print("Error: Cannot delete the only existing account!\n");
            else {
                str_cpy(temp_user, users[cur_user_index]);
                if (kn_mode) { 
                    sys_state = 34; 
                    vga_print("Are you sure you want to delete your account? (Y/N): "); 
                }
                else { 
                    sys_state = 17; 
                    vga_print("Password: "); 
                }
            }
        }
        else if (str_cmp(kbd_buffer, "kn") == 0) {
            if (!kn_mode) { sys_state = 6; vga_print("Password: "); }
            else {
                for(int i=0; i<12; i++) sessions[i].k_mode = false;
                vga_print("KN mode disabled.\n");
            }
        }
        else if (str_cmp(kbd_buffer, "rebsys") == 0) {
            if (kn_mode) {
                sys_reboot();
            } else {
                sys_state = 19;
                vga_print("Password: ");
            }
        }
        else if (str_cmp(kbd_buffer, "shtsys") == 0) {
            if (kn_mode) {
                sys_shutdown();
            } else {
                sys_state = 20;
                vga_print("Password: ");
            }
        }
        else if (str_starts_with(kbd_buffer, "syspw ") || str_cmp(kbd_buffer, "syspw") == 0) {
            if (kbd_buffer[5] == ' ') {
                vga_print("Usage: syspw (no arguments)\n");
            } else {
                sys_state = 84;
                vga_print("New Password: ");
            }
        }
        else if (str_cmp(kbd_buffer, "shnm") == 0) {
            bool new_val = !show_shell;
            for (int i = 0; i < 12; i++) sessions[i].show_shell_flag = new_val;
            if (new_val) vga_print("Shell name display: ENABLED\n");
            else vga_print("Shell name display: DISABLED\n");
            restore_current_banner();
        }
		else if (str_starts_with(kbd_buffer, "scr ")) {
		    const char* arg = kbd_buffer + 4;
		    int mins = 0;
		    bool valid = false;
            
		    while (*arg >= '0' && *arg <= '9') {
		        mins = mins * 10 + (*arg - '0');
		        arg++;
		        valid = true;
		    }
            
		    if (!valid || *arg != '\0') {
		        vga_print("Error: Invalid time format!\n");
		    } else {
		        if (kn_mode) {
		            if (mins == 0) {
		                inactivity_timeout = 0;
		                vga_print("Auto-suspend disabled.\n");
		            } else {
		                inactivity_timeout = mins * 60;
					last_activity_time = get_uptime_seconds();
		                vga_print("Auto-suspend timer set to ");
		                print_two_digits((uint8_t)mins);
		                vga_print(" minutes.\n");
		            }
		        } else {
		            temp_scr_mins = mins;
		            sys_state = 22;
		            vga_print("Password: ");
		        }
		    }
		}
        /* ============================================================
           FILE SYSTEM COMMANDS (ft, mk, smol, xp)
           ============================================================ */
        else if (str_cmp(kbd_buffer, "ft") == 0) {
            /* ---- PATH BREADCRUMB ---- */
// vga_set_color(10, 0);
            if (ses.fs_depth > 0) {
                vga_print("  ");
                vga_putchar(current_drive);
                if (ses.fs_sub_drive[0]) { vga_putchar(':'); vga_print(ses.fs_sub_drive); }
                vga_print(":\\");
                for (int _d = 1; _d <= ses.fs_depth; _d++) {
                    vga_print(ses.fs_name_stack[_d]);
                    vga_putchar('\\');
                }
                vga_putchar('\n');
            }

            /* ---- CONTENTS ---- */
// vga_set_color(15, 0);
            
            if (ses.fs_depth == 0) {
                for (int i=0; i<_drive_count; i++) {
                    if (_drives[i].present) {
// vga_set_color(15, 0);
                        vga_print("============================================================\n");
                        int name_len = 1;
                        if (_drives[i].sub[0]) { 
                            vga_print(_drives[i].sub); 
                            name_len = 0;
                            while (_drives[i].sub[name_len]) name_len++;
                        }
                        else { 
                            vga_putchar(_drives[i].letter); 
                        }
                        
                        for (int _k = 0; _k < 4 - name_len; _k++) {
                            vga_putchar('-');
                        }
                        vga_print("\\ ");
                        vga_print(_drives[i].model);
                        vga_print("  -  ");
                        vga_print_uint(_drives[i].size_mb);
                        vga_print(" MB\n");
                        
                        if (_drives[i].hw_type == DRV_HDD || _drives[i].hw_type == DRV_VIRTUAL || _drives[i].hw_type == DRV_USB_FLASH) {
                            fat32_switch_drive(&_drives[i]);
                            uint32_t rclus = fat32_root();
                            if ((_drives[i].is_boot || (_drives[i].letter == viata_boot_drive_letter() && !_drives[i].sub[0])) &&
                                cur_user_index >= 0 && user_home_clusters[cur_user_index] != 0) {
                                rclus = user_home_clusters[cur_user_index];
                            }
                            _fat32_print_tree(rclus, 0);
                        } else {
// vga_set_color(8, 0);
                            vga_print("    (Unsupported format or empty media)\n");
// vga_set_color(15, 0);
                        }
                        vga_print("\n");
                    }
                }
                vga_print("============================================================\n");
                fat32_switch_drive(viata_get_drive(current_drive, ses.fs_sub_drive));
            } else {
                fat32_switch_drive(viata_get_drive(current_drive, ses.fs_sub_drive));
                vga_print("============================================================\n");
                vga_putchar(current_drive);
                vga_print("---\\ "); vga_print(ses.fs_name_stack[ses.fs_depth]);
                vga_print("  -  ");
                _ft_print_size(fat32_get_dir_size(cur_dir_clus, 0));
                vga_print("\n");
                _fat32_print_tree(cur_dir_clus, 0);
                vga_print("\n============================================================\n");
            }

// vga_set_color(15, 0);
        }
        else if (str_starts_with(kbd_buffer, "ft ")) {
            /* ================================================================
               ft navigation — unified token parser.
               Syntax: ft [tok0] [> tok1] [> tok2] ...
               Each token (separated by >) is classified as:
                 - '<'  char(s) → go up N directory levels
                 - single uppercase letter A-F → switch main drive
                 - sub-drive code (e.g. V1 X1 R1 D1 F1 N1) → switch sub-drive
                 - anything else → navigate into directory (create with >>)
               Tokens are applied LEFT TO RIGHT.
               Examples:
                 ft F > V1      → switch to F, then switch sub-drive to V1
                 ft V1 > F      → switch sub-drive V1, then back to main F
                 ft > docs      → enter directory "docs"
                 ft > dir1 > dir2   → nested navigation
                 ft <<          → go up 2 levels
               ================================================================ */

            
            #define _IS_SUB(s) ((s)[0]&&(s)[1]>='1'&&(s)[1]<='9'&&!(s)[2]&&\
                ((s)[0]=='V'||(s)[0]=='X'||(s)[0]=='R'||(s)[0]=='D'||(s)[0]=='F'||(s)[0]=='N'))
            #define _IS_DRV(s) ((s)[0]>='A'&&(s)[0]<='F'&&!(s)[1])

            const char *p = kbd_buffer + 3;

            
            bool has_gt = false;
            for (int i = 0; p[i]; i++) if (p[i] == '>') has_gt = true;
            bool valid = true;
            if (!has_gt) {
                while (*p == ' ') p++;
                if (p[0] != '<') {
                    vga_print("  Invalid syntax. Valid examples:\n");
                    vga_print("  ft F > V1    ft > folder    ft <\n");
                    valid = false;
                }
                p = kbd_buffer + 3; 
            }

            if (valid) {
                
                char tok[64]; int ti = 0;
                bool create_next = false;  

                
                for (;;) {
                    char c = *p;

                    if (c == '>' || c == '\0') {
                        
                        tok[ti] = '\0'; ti = 0;
                        
                        int ts = 0; while (tok[ts] == ' ') ts++;
                        int te = 0; while (tok[ts+te]) te++;
                        while (te > 0 && tok[ts+te-1] == ' ') te--;
                        
                        for (int mv = 0; mv < te; mv++) tok[mv] = tok[ts+mv];
                        tok[te] = '\0';

                        
                        char utok[64];
                        for (int uv = 0; uv <= te; uv++) {
                            char uc = tok[uv];
                            utok[uv] = (uc >= 'a' && uc <= 'z') ? uc - 32 : uc;
                        }

                        /* --- Apply token --- */
                        if (te > 0) {
                            
                            if (utok[0] == '<') {
                                int levels = 0; int li = 0;
                                while (utok[li] == '<') { levels++; li++; }
                                for (int lv = 0; lv < levels; lv++) {
                                    if (ses.fs_depth > 0) ses.fs_depth--;
                                    else { vga_print("  Error: already in root.\n"); break; }
                                }
                            }
                            
                            else if (_IS_DRV(utok)) {
                                ses.c_drive = utok[0];
                                ses.fs_depth = 0;
                                ses.fs_sub_drive[0] = '\0';
                                ses.fs_cluster_stack[0] = (ses.c_drive == viata_boot_drive_letter() && cur_user_index >= 0) ? user_home_clusters[cur_user_index] : fat32_root();
                                fat32_switch_drive(viata_get_drive(ses.c_drive, ses.fs_sub_drive));
                            }
                            
                            else if (_IS_SUB(utok)) {
                                
                                bool is_source = (ses.fs_sub_drive[0] == utok[0] &&
                                                  ses.fs_sub_drive[1] == utok[1] &&
                                                  !ses.fs_sub_drive[2]);
                                if (!is_source) {
                                    
                                    ses.fs_sub_drive[0] = utok[0];
                                    ses.fs_sub_drive[1] = utok[1];
                                    ses.fs_sub_drive[2] = '\0';
                                    ses.fs_depth = 0;
                                    ses.fs_cluster_stack[0] = fat32_root();
                                    fat32_switch_drive(viata_get_drive(current_drive, ses.fs_sub_drive));
                                }
                                
                            }
                            
                            else {
                                bool is_d = false;
                                uint32_t nc = fat32_find(cur_dir_clus, tok, &is_d, 0);
                                if (nc == 0xFFFFFFFF && create_next) {
                                    fat32_mkdir(cur_dir_clus, tok);
                                    nc = fat32_find(cur_dir_clus, tok, &is_d, 0);
                                }
                                if (nc != 0xFFFFFFFF && is_d) {
                                    if (ses.fs_depth < 15) {
                                        ses.fs_depth++;
                                        ses.fs_cluster_stack[ses.fs_depth] = nc;
                                        str_cpy(ses.fs_name_stack[ses.fs_depth], tok);
                                    }
                                } else if (nc == 0xFFFFFFFF) {
                                    vga_print("  Error: not found: ");
                                    vga_print(tok); vga_print("\n");
                                } else {
                                    vga_print("  Error: not a directory.\n");
                                }
                            }
                        }

                        create_next = false;
                        if (c == '\0') break;

                        
                        p++;
                        if (*p == '>') { create_next = true; p++; }
                        while (*p == ' ') p++;  

                    } else {
                        if (ti < 63) tok[ti++] = c;
                        p++;
                    }
                }
            }

            fat32_switch_drive(viata_get_drive(current_drive, ses.fs_sub_drive));
            #undef _IS_SUB
            #undef _IS_DRV
        }
        
        else if (str_starts_with(kbd_buffer, "mk ")) {
            const char *args = kbd_buffer + 3;
            while (*args == ' ') args++;
            char type[20]; char name_arg[280];
            int ti=0; while(*args && *args!=' ' && ti<19) type[ti++]=*args++;
            type[ti]='\0';
            while(*args==' ') args++;
            int ni=0; while(*args && ni<249) name_arg[ni++]=*args++;
            name_arg[ni]='\0';
            if (!name_arg[0]) {
                vga_print("Usage: mk dir NAME | mk EXT NAME\n");
            } else if (str_cmp(type,"dir")==0) {
                if (fat32_mkdir(cur_dir_clus, name_arg))
                    vga_print("Directory created.\n");
                else vga_print("Error: directory already exists or disk full.\n");
            } else {
                char fname[280];
                str_cpy(fname, name_arg); str_cat(fname, "."); str_cat(fname, type);
                if (fat32_mkfile(cur_dir_clus, fname, 0, 0)) {
                    vga_print("File created: "); vga_print(fname); vga_print("\n");
                } else { vga_print("Error: file already exists or disk full.\n"); }
            }
        }
        
        else if (str_cmp(kbd_buffer, "smol") == 0) {
            vga_print("Usage: smol ext name\n");
        }
        else if (str_starts_with(kbd_buffer, "smol ")) {
            const char *args = kbd_buffer + 5;
            while (*args == ' ') args++;
            char type[20]; char name_arg[280];
            int ti=0; while(*args && *args!=' ' && ti<19) type[ti++]=*args++;
            type[ti]='\0'; while(*args==' ') args++;
            int ni=0; while(*args && ni<249) name_arg[ni++]=*args++;
            name_arg[ni]='\0';
            if (!type[0] || !name_arg[0]) {
                vga_print("Usage: smol ext name\n");
            } else {
                char orig_ext[20];
                str_cpy(orig_ext, type);
                int tl = str_len(orig_ext);
                if (tl > 5 && str_cmp(orig_ext + tl - 5, ".smol") == 0) {
                    orig_ext[tl - 5] = '\0';
                }
                char src_name[280], dst_name[280];
                str_cpy(src_name, name_arg); str_cat(src_name, "."); str_cat(src_name, orig_ext);
                str_cpy(dst_name, src_name); str_cat(dst_name, ".smol");
                uint32_t src_sz = fat32_readfile(cur_dir_clus, src_name, _smol_src, 131072);
                if (src_sz == 0xFFFFFFFF) {
                    vga_print("Error: file not found: "); vga_print(src_name); vga_print("\n");
                } else {
                    uint32_t dst_sz = smol_compress(_smol_src, src_sz, _smol_dst, 131072, orig_ext);
                    if (!dst_sz) {
                        vga_print("Error: compression failed (file too large).\n");
                    } else {
                        fat32_delete(cur_dir_clus, dst_name);
                        if (fat32_mkfile(cur_dir_clus, dst_name, _smol_dst, dst_sz)) {
                            vga_print(src_name); vga_print(" -> "); vga_print(dst_name); vga_print("\n");
                            vga_print("  Original: "); vga_print_uint(src_sz);
                            vga_print(" B  ->  SMOL: "); vga_print_uint(dst_sz);
                            vga_print(" B (");
                            if (src_sz > 0) {
                                uint32_t saved = (src_sz > dst_sz) ? (src_sz - dst_sz) : 0;
                                uint32_t pct = (saved * 100) / src_sz;
                                vga_putchar('-'); vga_print_uint(pct); vga_print("%");
                            } else {
                                vga_print("0%");
                            }
                            vga_print(")\n");
                        } else vga_print("Error: cannot write compressed file.\n");
                    }
                }
            }
        }
        
        else if (str_cmp(kbd_buffer, "di") == 0) {
            vga_print("Usage: di ext name\n");
        }
        else if (str_starts_with(kbd_buffer, "di ")) {
            const char *args = kbd_buffer + 3;
            while (*args == ' ') args++;
            char type[20]; char name_arg[280];
            int ti=0; while(*args && *args!=' ' && ti<19) type[ti++]=*args++;
            type[ti]='\0'; while(*args==' ') args++;
            int ni=0; while(*args && ni<249) name_arg[ni++]=*args++;
            name_arg[ni]='\0';
            if (!name_arg[0] || !type[0]) {
                vga_print("Usage: di ext name\n");
            } else {
                char fname[280];
                str_cpy(fname, name_arg); str_cat(fname, "."); str_cat(fname, type);
                
                int fnl = str_len(fname);
                bool is_smol = (fnl > 5 && str_cmp(fname + fnl - 5, ".smol") == 0);
                char fname_smol[280];
                str_cpy(fname_smol, fname);
                if (!is_smol) str_cat(fname_smol, ".smol");

                str_cpy(editor_fname, fname);
                editor_len = 0;

                if (is_smol) {
                    uint32_t csz = fat32_readfile(cur_dir_clus, fname, _smol_src, sizeof(_smol_src));
                    if (csz != 0xFFFFFFFF) {
                        editor_len = smol_decompress(_smol_src, csz, (uint8_t*)editor_buf, sizeof(editor_buf) - 1, 0);
                    }
                } else {
                    editor_len = fat32_readfile(cur_dir_clus, fname, (uint8_t*)editor_buf, sizeof(editor_buf) - 1);
                    if (editor_len == 0xFFFFFFFF) {
                        uint32_t csz = fat32_readfile(cur_dir_clus, fname_smol, _smol_src, sizeof(_smol_src));
                        if (csz != 0xFFFFFFFF) {
                            editor_len = smol_decompress(_smol_src, csz, (uint8_t*)editor_buf, sizeof(editor_buf) - 1, 0);
                            str_cpy(editor_fname, fname_smol);
                        } else {
                            editor_len = 0;
                        }
                    } else {
                        if (editor_len >= (int)sizeof(smol_header_t)) {
                            const smol_header_t *hdr = (const smol_header_t*)editor_buf;
                            if (hdr->magic == SMOL_MAGIC) {
                                for (int bi = 0; bi < editor_len; bi++) _smol_src[bi] = (uint8_t)editor_buf[bi];
                                editor_len = smol_decompress(_smol_src, editor_len, (uint8_t*)editor_buf, sizeof(editor_buf) - 1, 0);
                            }
                        }
                    }
                }
                if (editor_len < 0) editor_len = 0;
                editor_buf[editor_len] = '\0';
                
                for (int i=0; i<1024; i++) {
                    for (int j=0; j<80; j++) ed_lines[i][j] = '\0';
                }
                
                ed_line_count = 1;
                int lx = 0, ly = 0;
                for (int i=0; i<editor_len; i++) {
                    if (editor_buf[i] == '\n') { ly++; lx = 0; if (ly >= 1024) break; ed_line_count++; }
                    else if (editor_buf[i] != '\r') {
                        if (lx < 79) ed_lines[ly][lx++] = editor_buf[i];
                    }
                }
                ed_cx = lx;
                ed_cy = ly;
                if (ed_cy >= 1024) { ed_cy = 1023; ed_cx = 79; ed_line_count = 1024; }
                if (ed_cy > 20) ed_sy = ed_cy - 20;
                else ed_sy = 0;
                
                ed_saved = true; vga_screen_on(); sys_state = 30;
                ed_draw();
                return;
            }
        }
        else if (str_cmp(kbd_buffer, "qd") == 0) {
            vga_print("Usage: qd ext name >text\n");
        }
        else if (str_starts_with(kbd_buffer, "qd ")) {
            char *p = kbd_buffer + 3;
            while (*p == ' ') p++;
            char ext[20], name[280];
            int i=0; while(*p && *p!=' ' && i<19) ext[i++]=*p++;
            ext[i]='\0'; while(*p==' ') p++;
            i=0; while(*p && *p!=' ' && *p!='>' && i<249) name[i++]=*p++;
            name[i]='\0'; while(*p==' ') p++;
            if (*p == '>') p++;
            if (!name[0] || !*p) {
                vga_print("Usage: qd ext name >text\n");
            } else {
                char fname[280];
                int i=0; while(name[i]) { fname[i] = name[i]; i++; }
                fname[i++] = '.';
                int j=0; while(ext[j]) { fname[i++] = ext[j++]; }
                fname[i] = '\0';
                
                int fnl = str_len(fname);
                bool is_smol = (fnl > 5 && str_cmp(fname + fnl - 5, ".smol") == 0);
                
                if (is_smol) {
                    uint32_t csz = fat32_readfile(cur_dir_clus, fname, _smol_src, sizeof(_smol_src));
                    if (csz != 0xFFFFFFFF) {
                        editor_len = smol_decompress(_smol_src, csz, (uint8_t*)editor_buf, sizeof(editor_buf) - 1, 0);
                    } else editor_len = 0;
                } else {
                    editor_len = fat32_readfile(cur_dir_clus, fname, (uint8_t*)editor_buf, sizeof(editor_buf) - 1);
                    if (editor_len == 0xFFFFFFFF) editor_len = 0;
                }
                int add_len = 0; while (p[add_len]) add_len++;
                if (editor_len + add_len + 2 < (int)sizeof(editor_buf) - 1) {
                    if (editor_len > 0 && editor_buf[editor_len - 1] != '\n') {
                        editor_buf[editor_len++] = '\n';
                    }
                    for(int k=0; k<add_len; k++) editor_buf[editor_len++] = p[k];
                    editor_buf[editor_len++] = '\n';
                    editor_buf[editor_len] = '\0';
                    fat32_delete(cur_dir_clus, fname);
                    if (is_smol) {
                        char oext[16] = {0};
                        int el = str_len(ext);
                        int bsz = (el > 5) ? el - 5 : 0;
                        for (int k = 0; k < bsz && k < 13; k++) oext[k] = ext[k];
                        oext[bsz < 13 ? bsz : 13] = '\0';
                        uint32_t out_csz = smol_compress((const uint8_t*)editor_buf, editor_len, _smol_dst, sizeof(_smol_dst), oext);
                        if (out_csz > 0 && fat32_mkfile(cur_dir_clus, fname, _smol_dst, out_csz)) {
                            vga_print("Text appended (SMOL compressed).\n");
                        } else {
                            vga_print("Error: compression/write failed.\n");
                        }
                    } else {
                        fat32_mkfile(cur_dir_clus, fname, (uint8_t*)editor_buf, editor_len);
                        vga_print("Text appended.\n");
                    }
                } else {
                    vga_print("Error: File too large to append.\n");
                }
            }
        }
        else if (str_cmp(kbd_buffer, "dl") == 0) {
            vga_print("Usage: dl ext name\n");
        }
        else if (str_starts_with(kbd_buffer, "dl ")) {
            char *p = kbd_buffer + 3;
            while (*p == ' ') p++;
            char ext[20], name[280];
            int i=0; while(*p && *p!=' ' && i<19) ext[i++]=*p++;
            ext[i]='\0'; while(*p==' ') p++;
            i=0; while(*p && i<249) name[i++]=*p++;
            name[i]='\0';
            if (!name[0]) {
                vga_print("Usage: dl ext name\n");
            } else {
                char fname[280];
                i=0; while(name[i]) { fname[i] = name[i]; i++; }
                if (str_cmp(ext, "dir") != 0 && str_cmp(ext, "sys") != 0) {
                    fname[i++] = '.';
                    int j=0; while(ext[j]) { fname[i++] = ext[j++]; }
                }
                fname[i] = '\0';
                
                uint32_t trash = fat32_get_trash_dir();
                char trash_name[280];
                str_cpy(trash_name, fname);
                if (fat32_move(cur_dir_clus, trash, fname, trash_name)) {
                    vga_print("Moved to Recycle Bin.\n");
                } else {
                    vga_print("Error: File not found or exists in Trash.\n");
                }
            }
        }
        
        else if (str_cmp(kbd_buffer, "bnsys") == 0) {
            uint32_t trash = fat32_get_trash_dir();
            vga_print("============================================================\n");
            vga_putchar(current_drive);
            vga_print("---\\ RECYCLE BIN  -  ");
            _ft_print_size(fat32_get_dir_size(trash, 0));
            vga_print("\n");
            _fat32_print_tree(trash, 0);
            vga_print("\n============================================================\n");
        }
        else if (str_cmp(kbd_buffer, "unbn") == 0) {
            vga_print("Usage: unbn ext name\n");
        }
        else if (str_starts_with(kbd_buffer, "unbn ")) {
            char *p = kbd_buffer + 5;
            while (*p == ' ') p++;
            char ext[20], name[280];
            int i=0; while(*p && *p!=' ' && i<19) ext[i++]=*p++;
            ext[i]='\0'; while(*p==' ') p++;
            i=0; while(*p && i<249) name[i++]=*p++;
            name[i]='\0';
            if (!name[0]) {
                vga_print("Usage: unbn ext name\n");
            } else {
                char fname[280];
                i=0; while(name[i]) { fname[i] = name[i]; i++; }
                if (str_cmp(ext, "dir") != 0 && str_cmp(ext, "sys") != 0) {
                    fname[i++] = '.';
                    int j=0; while(ext[j]) { fname[i++] = ext[j++]; }
                }
                fname[i] = '\0';
                
                uint32_t trash = fat32_get_trash_dir();
                if (fat32_move(trash, cur_dir_clus, fname, fname)) {
                    vga_print("Restored.\n");
                } else {
                    vga_print("Error: Not found in Trash or already exists.\n");
                }
            }
        }
        
        else if (str_starts_with(kbd_buffer, "dlbn ")) {
            if (!kn_mode) {
                str_cpy(temp_extra, kbd_buffer);
                sys_state = 31;
                vga_print("Password: ");
            } else {
                str_cpy(temp_extra, kbd_buffer);
                sys_state = 33;
                vga_print("Are you sure you want to securely delete this file? (Y/N): ");
            }
        }
        
        else if (str_starts_with(kbd_buffer, "srcbn ")) {
            char *query = kbd_buffer + 6;
            while (*query == ' ') query++;
            if (*query == '\0') {
                vga_print("Usage: srcbn text\n");
            } else {
                vga_print("Searching in Recycle Bin for: "); vga_print(query); vga_print("\n");
                srcsys_content_mode = false;
                str_cpy(srcsys_query, query);
                fat32_list_dir(fat32_get_trash_dir(), _srcsys_cb);
            }
        }
        else if (str_cmp(kbd_buffer, "clbn") == 0) {
            if (!kn_mode) {
                str_cpy(temp_extra, kbd_buffer);
                sys_state = 31;
                vga_print("Password: ");
            } else {
                sys_state = 32;
                vga_print("Are you sure you want to empty the Recycle Bin? (Y/N): ");
            }
        }

        else if (str_cmp(kbd_buffer, "do") == 0) {
            vga_print("Usage: do ext1 name1 ext2 name2\n");
        }
        else if (str_starts_with(kbd_buffer, "do ")) {
            const char *args = kbd_buffer + 3;
            while (*args == ' ') args++;
            
            char ext1[20], name1[250], ext2[20], name2[250];
            int i = 0;
            while (*args && *args != ' ' && i < 19) ext1[i++] = *args++;
            ext1[i] = '\0'; while (*args == ' ') args++;
            
            i = 0;
            while (*args && *args != ' ' && i < 249) name1[i++] = *args++;
            name1[i] = '\0'; while (*args == ' ') args++;
            
            i = 0;
            while (*args && *args != ' ' && i < 19) ext2[i++] = *args++;
            ext2[i] = '\0'; while (*args == ' ') args++;
            
            i = 0;
            while (*args && i < 249) name2[i++] = *args++;
            name2[i] = '\0';
            
            if (!name1[0] || !name2[0]) {
                vga_print("Usage: do ext1 name1 ext2 name2\n");
            } else {
                char old_name[280], new_name[280];
                int j = 0; while (name1[j]) { old_name[j] = name1[j]; j++; }
                old_name[j++] = '.';
                int k = 0; while (ext1[k]) { old_name[j++] = ext1[k++]; }
                old_name[j] = '\0';
                
                j = 0; while (name2[j]) { new_name[j] = name2[j]; j++; }
                new_name[j++] = '.';
                k = 0; while (ext2[k]) { new_name[j++] = ext2[k++]; }
                new_name[j] = '\0';
                
                bool is_dir = false; uint32_t fsize = 0;
                uint32_t c = fat32_find(cur_dir_clus, old_name, &is_dir, &fsize);
                if (c != 0xFFFFFFFF && is_dir) {
                    if (str_cmp(ext1, ext2) != 0) {
                        vga_print("Error: Cannot change extension of a directory!\n");
                    } else {
                        if (fat32_rename(cur_dir_clus, old_name, new_name)) vga_print("Directory renamed.\n");
                        else vga_print("Error renaming directory.\n");
                    }
                } else {
                    if (fat32_rename(cur_dir_clus, old_name, new_name)) {
                        vga_print("File format/name successfully changed.\n");
                    } else {
                        vga_print("Error: File not found or destination already exists.\n");
                    }
                }
            }
        }
        else if (str_cmp(kbd_buffer, "xp") == 0) {
            vga_print("Usage: xp ext name\n");
        }
        else if (str_starts_with(kbd_buffer, "xp ")) {
            const char *args = kbd_buffer + 3;
            while (*args == ' ') args++;
            char type[20]; char name_arg[280];
            int ti=0; while(*args && *args!=' ' && ti<19) type[ti++]=*args++;
            type[ti]='\0'; while(*args==' ') args++;
            int ni=0; while(*args && ni<249) name_arg[ni++]=*args++;
            name_arg[ni]='\0';
            
            if (!name_arg[0] || !type[0]) {
                vga_print("Usage: xp ext name\n");
            } else {
                char fname[280];
                str_cpy(fname, name_arg); str_cat(fname, "."); str_cat(fname, type);
                
                int fnl = str_len(fname);
                bool is_smol = (fnl > 5 && str_cmp(fname + fnl - 5, ".smol") == 0);
                char fname_smol[280];
                str_cpy(fname_smol, fname);
                if (!is_smol) str_cat(fname_smol, ".smol");

                uint32_t loaded = 0xFFFFFFFF;
                bool was_compressed = false;
                char orig_ext[16] = {0};

                if (is_smol) {
                    uint32_t csz = fat32_readfile(cur_dir_clus, fname, _smol_src, 131072);
                    if (csz != 0xFFFFFFFF) {
                        loaded = smol_decompress(_smol_src, csz, _xep_buf, 131072, orig_ext);
                        was_compressed = true;
                    }
                } else {
                    loaded = fat32_readfile(cur_dir_clus, fname, _xep_buf, 131072);
                    if (loaded == 0xFFFFFFFF) {
                        uint32_t csz = fat32_readfile(cur_dir_clus, fname_smol, _smol_src, 131072);
                        if (csz != 0xFFFFFFFF) {
                            loaded = smol_decompress(_smol_src, csz, _xep_buf, 131072, orig_ext);
                            was_compressed = true;
                            str_cpy(fname, fname_smol);
                        }
                    } else {
                        if (loaded >= sizeof(smol_header_t)) {
                            const smol_header_t *hdr = (const smol_header_t*)_xep_buf;
                            if (hdr->magic == SMOL_MAGIC) {
                                for (uint32_t bi = 0; bi < loaded; bi++) _smol_src[bi] = _xep_buf[bi];
                                loaded = smol_decompress(_smol_src, loaded, _xep_buf, 131072, orig_ext);
                                was_compressed = true;
                            }
                        }
                    }
                }

                if (loaded == 0xFFFFFFFF) {
                    vga_print("Error: file not found: "); vga_print(fname); vga_print("\n");
                } else {
                    if (was_compressed) {
                        vga_print("[SMOL] Decompressing in RAM (");
                        vga_print_uint(loaded);
                        vga_print(" B)...\n");
                    }
                    file_type_t ft2; bool comp2;
                    ext_parse(fname, &ft2, &comp2);
                    if (ft2 == EXT_UNKNOWN || ft2 == EXT_DATA) {
                        if (str_cmp(orig_ext, "xep") == 0) ft2 = EXT_XEP;
                        else if (str_cmp(orig_ext, "kvbn") == 0) ft2 = EXT_KVBN;
                    }

                    if (ft2 == EXT_KVBN) {
                        vga_print("[KVBN] Executing script...\n");
                        static char script_buf[4096];
                        uint32_t slen = (loaded < sizeof(script_buf) - 1) ? loaded : (sizeof(script_buf) - 1);
                        for (uint32_t si = 0; si < slen; si++) script_buf[si] = _xep_buf[si];
                        script_buf[slen] = '\0';
                        
                        uint32_t pos = 0;
                        char line[128];
                        while (pos < slen) {
                            int li = 0;
                            while (pos < slen && script_buf[pos] != '\n' && script_buf[pos] != '\r' && li < 127)
                                line[li++] = script_buf[pos++];
                            while (pos < slen && (script_buf[pos] == '\n' || script_buf[pos] == '\r')) pos++;
                            line[li] = '\0';
                            
                            if (li == 0 || line[0] == '#') continue;
                            
                            str_cpy(kbd_buffer, line);
                            kbd_index = li; kbd_cursor = li;
                            vga_print("> "); vga_print(line); vga_print("\n");
                            handle_enter();
                        }
                        vga_print("[KVBN] Script terminated.\n");
                    } else if (ft2 == EXT_DATA || ft2 == EXT_UNKNOWN) {
                        _xep_buf[loaded] = '\0';
                        vga_print((const char*)_xep_buf);
                        vga_print("\n");
                    } else if (ft2 == EXT_XEP) {
                        vga_print("[XEP] Loading at 0x300000...\n");
                        uint8_t *load_addr = (uint8_t*)0x300000UL;
                        for (uint32_t bi = 0; bi < loaded; bi++) load_addr[bi] = _xep_buf[bi];
                        typedef void (*xep_fn)(void);
                        xep_fn entry = (xep_fn)(void*)0x300000UL;
                        entry();
                        vga_print("[XEP] Program terminated.\n");
                    } else {
                        vga_print("Error: file is not executable.\n");
                    }
                }
            }
        }
        else if (kbd_index > 0) {
            /* Only show error if buffer has printable ASCII (not PS/2 garbage) */
            bool looks_valid = true;
            for (int _ci = 0; _ci < kbd_index; _ci++) {
                if (kbd_buffer[_ci] < 32 || kbd_buffer[_ci] > 126) { looks_valid = false; break; }
            }
            if (looks_valid) {
                vga_print("COMMAND NOT FOUND!!!\n");
            }
        }

        if (sys_state == 0) { try_print_prompt(); }
    }

    else if (sys_state == 1) {
        if (is_empty_or_spaces(kbd_buffer)) {
            vga_clear(); vga_print("Error: Username cannot be empty!\nCreate Admin Account\nUsername: ");
        } else {
            str_cpy(temp_user, kbd_buffer); sys_state = 2; vga_print("Password: ");
        }
    }
    else if (sys_state == 2) {
        if (is_empty_or_spaces(kbd_buffer)) {
            vga_clear(); vga_print("Error: Password cannot be empty!\nCreate Admin Account\nUsername: "); sys_state = 1;
        } else {
            str_cpy(temp_pass, kbd_buffer); sys_state = 3; vga_print("Confirm Password: ");
        }
    }
    else if (sys_state == 3) {
        if (str_cmp(temp_pass, kbd_buffer) == 0) {
            str_cpy(users[users_count], temp_user);
            str_cpy(passwords[users_count], temp_pass);
            user_bg_colors[users_count] = 0;
            user_fg_colors[users_count] = 15;
            user_flags[users_count] = 0;
            user_home_clusters[users_count] = 0;
            init_user_shell_names(users_count);
            users_count++;
            save_system_users();
            global_login(users_count - 1, false);
        } else {
            vga_clear(); vga_print("Error: Passwords do not match!\n");
            sys_state = 1; vga_print("Create Admin Account\nUsername: ");
        }
    }
    else if (sys_state == 4) {
        int found = find_user_index(kbd_buffer);
        if (found != -1) { cur_user_index = found; sys_state = 5; vga_print("Password: "); }
        else { vga_clear(); vga_print("Error: User not found!\n"); print_users_list(); }
    }
    else if (sys_state == 5) {
        if (str_cmp(passwords[cur_user_index], kbd_buffer) == 0) { if(rpt_magic == 0xDEADBEEF) rpt_password_given=true; global_login(cur_user_index, true); }
        else { vga_clear(); vga_print("Access Denied!\n"); print_users_list(); sys_state = 4; }
    }
    else if (sys_state == 6) {
        if (str_cmp(passwords[cur_user_index], kbd_buffer) == 0) { if(rpt_magic == 0xDEADBEEF) rpt_password_given=true;
            sys_state = 0; for(int i=0; i<12; i++) sessions[i].k_mode = true;
            vga_print("Access Granted! KN Mode Enabled.\n"); try_print_prompt();
        } else { sys_state = 0; vga_print("Access Denied!\n"); try_print_prompt(); }
    }
    else if (sys_state == 7) {
        if (str_cmp(passwords[cur_user_index], kbd_buffer) == 0) { if(rpt_magic == 0xDEADBEEF) rpt_password_given=true; global_logout(4); }
        else { sys_state = 0; vga_print("Access Denied!\n"); try_print_prompt(); }
    }
    else if (sys_state == 8) {
        if (str_cmp(passwords[cur_user_index], kbd_buffer) == 0) { if(rpt_magic == 0xDEADBEEF) rpt_password_given=true;
            vga_disable_banner(); vga_clear();
            vga_print("Create Account\nUsername: "); sys_state = 9;
        } else { sys_state = 0; vga_print("Access Denied!\n"); try_print_prompt(); }
    }
    else if (sys_state == 9) {
        if (is_empty_or_spaces(kbd_buffer)) {
            vga_clear(); vga_print("Error: Username cannot be empty!\nCreate Account\nUsername: ");
        } else if (find_user_index(kbd_buffer) != -1) {
            vga_clear(); vga_print("Error: Username already exists!\nCreate Account\nUsername: "); sys_state = 9;
        } else { str_cpy(temp_user, kbd_buffer); sys_state = 10; vga_print("Password: "); }
    }
    else if (sys_state == 10) {
        if (is_empty_or_spaces(kbd_buffer)) {
            vga_clear(); vga_print("Error: Password cannot be empty!\nCreate Account\nUsername: "); sys_state = 9;
        } else {
            str_cpy(temp_pass, kbd_buffer); sys_state = 12; vga_print("Confirm Password: ");
        }
    }
    else if (sys_state == 12) {
        if (str_cmp(temp_pass, kbd_buffer) == 0) {
            str_cpy(users[users_count], temp_user); str_cpy(passwords[users_count], temp_pass);
            user_bg_colors[users_count] = 0;
            user_fg_colors[users_count] = 15;
            user_flags[users_count] = 0;
            user_home_clusters[users_count] = 0;
            init_user_shell_names(users_count);
            users_count++; save_system_users(); restore_current_banner(); vga_clear();
            vga_print("Account created successfully!\n"); sys_state = 0; try_print_prompt();
        } else {
            vga_clear(); vga_print("Error: Passwords do not match!\nCreate Account\nUsername: "); sys_state = 9;
        }
    }

    else if (sys_state == 17) {
        int idx = find_user_index(temp_user);
        if (idx != -1 && str_cmp(passwords[idx], kbd_buffer) == 0) {
            sys_state = 34;
            vga_print("Are you sure you want to delete your account? (Y/N): ");
        } else { sys_state = 0; vga_print("Access Denied!\n"); try_print_prompt(); }
    }
    else if (sys_state == 34) {
        int i = 0; while (kbd_buffer[i] == ' ') i++;
        if (kbd_buffer[i] == 'y' || kbd_buffer[i] == 'Y') {
            int idx = find_user_index(temp_user);
            if (idx != -1) {
                bool was_current = (idx == cur_user_index);
                delete_user_by_index(idx);
                if (!was_current) { vga_print("User successfully deleted!\n"); sys_state = 0; try_print_prompt(); }
            }
        } else {
            vga_print("Operation cancelled.\n");
            sys_state = 0; try_print_prompt();
        }
    }
    else if (sys_state == 19) {
        if (str_cmp(passwords[cur_user_index], kbd_buffer) == 0) { if(rpt_magic == 0xDEADBEEF) rpt_password_given=true;
            sys_reboot();
        } else {
            sys_state = 0;
            vga_print("Access Denied!\n");
            try_print_prompt();
        }
    }
    else if (sys_state == 20) {
        if (str_cmp(passwords[cur_user_index], kbd_buffer) == 0) { if(rpt_magic == 0xDEADBEEF) rpt_password_given=true;
            sys_shutdown();
        } else {
            sys_state = 0;
            vga_print("Access Denied!\n");
            try_print_prompt();
        }
    }
	else if (sys_state == 21) {
	    if (str_cmp(passwords[cur_user_index], kbd_buffer) == 0) { if(rpt_magic == 0xDEADBEEF) rpt_password_given=true;
	        sys_state = 0;
	        suspend_screen_active = false;
	        vga_screen_on();
	        restore_current_banner();
	        vga_clear();
	        vga_print("System resumed.\n");
	        try_print_prompt();
	    } else {
	        vga_clear();
	        vga_print("Access Denied!\n=== SYSTEM SUSPENDED ===\nEnter password to wake up: ");
	        vga_screen_on();
	        suspend_screen_active = true;
	        suspend_timer_start = get_uptime_seconds();
        }
	}
	else if (sys_state == 22) {
	    if (str_cmp(passwords[cur_user_index], kbd_buffer) == 0) { if(rpt_magic == 0xDEADBEEF) rpt_password_given=true;
	        if (temp_scr_mins == 0) {
	            inactivity_timeout = 0;
	            vga_print("Auto-suspend disabled.\n");
	        } else {
	            inactivity_timeout = temp_scr_mins * 60;
	            last_activity_time = get_uptime_seconds();
	            vga_print("Auto-suspend timer set to ");
	            print_two_digits((uint8_t)temp_scr_mins);
	            vga_print(" minutes.\n");
	        }
	        sys_state = 0;
	        try_print_prompt();
	    } else {
	        sys_state = 0;
	        vga_print("Access Denied!\n");
	        try_print_prompt();
	    }
	}
    else if (sys_state == 80) {
        if (str_cmp(passwords[cur_user_index], kbd_buffer) == 0) { if(rpt_magic == 0xDEADBEEF) rpt_password_given=true;
            str_cpy(users[cur_user_index], temp_extra);
            restore_current_banner();
            vga_print("Username successfully changed to: "); vga_print(users[cur_user_index]); vga_print("\n");
            save_system_users();
        } else {
            vga_print("Access Denied!\n");
        }
        sys_state = 0; try_print_prompt();
    }
    else if (sys_state == 81) {
        if (str_cmp(passwords[cur_user_index], kbd_buffer) == 0) { if(rpt_magic == 0xDEADBEEF) rpt_password_given=true;
            str_cpy(passwords[cur_user_index], temp_extra);
            vga_print("Password successfully changed!\n");
            save_system_users();
        } else {
            vga_print("Access Denied!\n");
        }
        sys_state = 0; try_print_prompt();
    }
    else if (sys_state == 82) {
        if (is_empty_or_spaces(kbd_buffer)) {
            vga_print("Error: Username cannot be empty!\n");
            sys_state = 0; try_print_prompt();
        } else if (str_len(kbd_buffer) > 15) {
            vga_print("Error: Username cannot exceed 15 characters!\n");
            sys_state = 0; try_print_prompt();
        } else {
            str_cpy(temp_extra, kbd_buffer);
            sys_state = 83;
            vga_print("Are you sure you want to change username to '"); vga_print(temp_extra); vga_print("'? (Y/N): ");
        }
    }
    else if (sys_state == 83) {
        if (str_cmp(kbd_buffer, "Y") == 0 || str_cmp(kbd_buffer, "y") == 0) {
            if (kn_mode) {
                str_cpy(users[cur_user_index], temp_extra);
                restore_current_banner();
                vga_print("Username successfully changed to: "); vga_print(users[cur_user_index]); vga_print("\n");
                save_system_users();
                sys_state = 0; try_print_prompt();
            } else {
                sys_state = 80;
                vga_print("Current Password: ");
            }
        } else {
            vga_print("Aborted.\n");
            sys_state = 0; try_print_prompt();
        }
    }
    else if (sys_state == 84) {
        if (is_empty_or_spaces(kbd_buffer)) {
            vga_print("Error: Password cannot be empty!\n");
            sys_state = 0; try_print_prompt();
        } else if (str_len(kbd_buffer) > 12) {
            vga_print("Error: Password cannot exceed 12 characters!\n");
            sys_state = 0; try_print_prompt();
        } else {
            str_cpy(temp_extra, kbd_buffer);
            sys_state = 85;
            vga_print("Confirm Password: ");
        }
    }
    else if (sys_state == 85) {
        if (str_cmp(temp_extra, kbd_buffer) == 0) {
            sys_state = 86;
            vga_print("Are you sure you want to change your password? (Y/N): ");
        } else {
            vga_print("Error: Passwords do not match!\n");
            sys_state = 0; try_print_prompt();
        }
    }
    else if (sys_state == 86) {
        if (str_cmp(kbd_buffer, "Y") == 0 || str_cmp(kbd_buffer, "y") == 0) {
            if (kn_mode) {
                str_cpy(passwords[cur_user_index], temp_extra);
                vga_print("Password successfully changed!\n");
                save_system_users();
                sys_state = 0; try_print_prompt();
            } else {
                sys_state = 81;
                vga_print("Current Password: ");
            }
        } else {
            vga_print("Aborted.\n");
            sys_state = 0; try_print_prompt();
        }
    }
	else if (sys_state == 26) {
	    if (str_cmp(passwords[cur_user_index], kbd_buffer) == 0) { if(rpt_magic == 0xDEADBEEF) rpt_password_given=true;
            sys_state = 27;
            vga_print("Are you sure you want to format the secondary disk? (Y/N): ");
	    } else {
	        sys_state = 0;
	        vga_print("Access Denied!\n");
	        try_print_prompt();
	    }
	}
	
	else if (sys_state == 27) {
	    if (kbd_buffer[0] == 'y' || kbd_buffer[0] == 'Y') {
            detect_drives();
            viata_drive_info_t *boot_d = viata_get_drive(viata_boot_drive_letter(), "");
            if (boot_d) boot_d->is_boot = true;
            
            bool found = false;
            for (int i=0; i<_drive_count; i++) {
                if (!_drives[i].is_boot && (_drives[i].hw_type == DRV_HDD || _drives[i].hw_type == DRV_VIRTUAL || _drives[i].hw_type == DRV_USB_FLASH)) {
                    found = true;
                    vga_print("Formatting secondary disk...\n");
                    uint16_t old_b = current_ata_base; uint8_t old_s = current_ata_sel;
                    viata_drive_info_t *old_drv = current_fat32_drv;
                    
                    current_ata_base = _drives[i].ata_base;
                    current_ata_sel = _drives[i].ata_sel;
                    current_fat32_drv = &_drives[i];
                    fat32_format();
                    
                    current_ata_base = old_b; current_ata_sel = old_s;
                    current_fat32_drv = old_drv;
                    
                    vga_print("Disk formatted successfully.\n");
                    break;
                }
            }
            if (!found) vga_print("Error: No secondary disk found.\n");
	    } else {
	        vga_print("Format cancelled.\n");
	    }
	    sys_state = 0;
	    try_print_prompt();
	}

	else if (sys_state == 31) {
	    if (str_cmp(passwords[cur_user_index], kbd_buffer) == 0) { if(rpt_magic == 0xDEADBEEF) rpt_password_given=true;
            sys_state = 0;
            str_cpy(kbd_buffer, temp_extra);
            kbd_index = str_len(temp_extra);
            bool old_kn = kn_mode;
            kn_mode = true;
            handle_enter();
            kn_mode = old_kn;
            return;
	    } else {
	        sys_state = 0;
	        vga_print("Access Denied!\n");
	        try_print_prompt();
	    }
	}
	
	else if (sys_state == 32) {
        int idx = 0; while (kbd_buffer[idx] == ' ') idx++;
        char c = kbd_buffer[idx];
	    if (c == 'y' || c == 'Y') {
	        vga_print("Emptying Recycle Bin securely...\n");
	        fat32_empty_trash();
	        vga_print("Recycle Bin emptied.\n");
	    } else {
	        vga_print("Operation cancelled.\n");
	    }
	    sys_state = 0;
	    try_print_prompt();
	}
	
	else if (sys_state == 33) {
        int idx = 0; while (kbd_buffer[idx] == ' ') idx++;
        char c = kbd_buffer[idx];
	    if (c == 'y' || c == 'Y') {
            char *p = temp_extra + 5;
            while (*p == ' ') p++;
            char ext[20], name[280];
            int i=0; while(*p && *p!=' ' && i<19) ext[i++]=*p++;
            ext[i]='\0'; while(*p==' ') p++;
            i=0; while(*p && i<249) name[i++]=*p++;
            name[i]='\0';
            if (!name[0]) {
                vga_print("Usage: dlbn ext name\n");
            } else {
                char fname[280];
                i=0; while(name[i]) { fname[i] = name[i]; i++; }
                if (str_cmp(ext, "dir") != 0 && str_cmp(ext, "sys") != 0) {
                    fname[i++] = '.';
                    int j=0; while(ext[j]) { fname[i++] = ext[j++]; }
                }
                fname[i] = '\0';
                
                uint32_t trash = fat32_get_trash_dir();
                if (fat32_hard_delete(trash, fname)) {
                    vga_print("File securely deleted from Recycle Bin.\n");
                } else {
                    vga_print("Error: File not found in Recycle Bin.\n");
                }
            }
	    } else {
	        vga_print("Operation cancelled.\n");
	    }
	    sys_state = 0;
	    try_print_prompt();
	}

    uint32_t end_time = get_uptime_seconds();

    if (str_cmp(kbd_buffer, "wtw") != 0) {
        if (rpt_magic == 0xDEADBEEF) {
            if (rpt_count_remaining == 0) {
                if (end_time >= rpt_start_time) last_cmd_duration = end_time - rpt_start_time;
                else last_cmd_duration = 0;
                rpt_magic = 0;
            }
        } else {
            if (end_time >= start_time) last_cmd_duration = end_time - start_time;
            else last_cmd_duration = 0;
        }
    }

    clear_buffer();
}


