#include "vga.h"
#include "io.h"

static uint16_t* const VGA_BUFFER = (uint16_t*) 0xB8000;
static const int VGA_WIDTH = 80;
static const int VGA_HEIGHT = 25;
#define VGA_HISTORY 100

static uint16_t term_buffers[12][VGA_HISTORY][80];
static int term_rows[12] = {0};
static int term_cols[12] = {0};
static int view_offsets[12] = {0};
static int current_vga_session = 0;

static uint8_t term_color = 0x0F;

static bool show_banner = false;
static char banner_line1[80] = {0};
static char banner_line2[80] = {0};
static char banner_line3[80] = {0};

void vga_flush(void) {
    int session = current_vga_session;
    int offset = view_offsets[session];
    int start_y = show_banner ? 4 : 1;
    
    
    if (show_banner) {
        for(int i = 0; i < VGA_WIDTH; i++) {
            VGA_BUFFER[0 * VGA_WIDTH + i] = banner_line1[i] ? ((uint16_t)banner_line1[i] | (uint16_t)term_color << 8) : ((uint16_t)' ' | (uint16_t)term_color << 8);
            VGA_BUFFER[1 * VGA_WIDTH + i] = banner_line2[i] ? ((uint16_t)banner_line2[i] | (uint16_t)term_color << 8) : ((uint16_t)' ' | (uint16_t)term_color << 8);
            VGA_BUFFER[2 * VGA_WIDTH + i] = banner_line3[i] ? ((uint16_t)banner_line3[i] | (uint16_t)term_color << 8) : ((uint16_t)' ' | (uint16_t)term_color << 8);
            VGA_BUFFER[3 * VGA_WIDTH + i] = ((uint16_t)' ' | (uint16_t)term_color << 8);
        }
    } else {
        for(int i = 0; i < VGA_WIDTH; i++) {
            VGA_BUFFER[0 * VGA_WIDTH + i] = ((uint16_t)' ' | (uint16_t)term_color << 8);
        }
    }
    
    int visible_rows = VGA_HEIGHT - start_y;
    int bottom_hist_y = term_rows[session];
    if (bottom_hist_y < visible_rows - 1) {
        bottom_hist_y = visible_rows - 1;
    }
    bottom_hist_y -= offset;
    
    for (int y = start_y; y < VGA_HEIGHT; y++) {
        int hist_y = bottom_hist_y - ((VGA_HEIGHT - 1) - y);
        for (int x = 0; x < VGA_WIDTH; x++) {
            if (hist_y >= 0 && hist_y < VGA_HISTORY) {
                VGA_BUFFER[y * VGA_WIDTH + x] = term_buffers[session][hist_y][x];
            } else {
                VGA_BUFFER[y * VGA_WIDTH + x] = ((uint16_t)' ' | (uint16_t)term_color << 8);
            }
        }
    }
}

void vga_set_banner(const char* line1, const char* line2, const char* line3) {
    int i;
    for (i = 0; i < 79 && line1[i]; i++) banner_line1[i] = line1[i];
    while (i < 80) banner_line1[i++] = '\0';
    for (i = 0; i < 79 && line2[i]; i++) banner_line2[i] = line2[i];
    while (i < 80) banner_line2[i++] = '\0';
    for (i = 0; i < 79 && line3[i]; i++) banner_line3[i] = line3[i];
    while (i < 80) banner_line3[i++] = '\0';
    show_banner = true;
    vga_flush();
}

void vga_disable_banner(void) {
    show_banner = false;
    vga_flush();
}

void update_cursor(int x, int y) {
    uint16_t pos = y * VGA_WIDTH + x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t) (pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t) ((pos >> 8) & 0xFF));
}

void enable_cursor(uint8_t cursor_start, uint8_t cursor_end) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, (inb(0x3D5) & 0xC0) | cursor_start);
    outb(0x3D4, 0x0B);
    outb(0x3D5, (inb(0x3D5) & 0xE0) | cursor_end);
}

static uint16_t vga_entry(unsigned char uc, uint8_t color) {
    return (uint16_t) uc | (uint16_t) color << 8;
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    term_color = fg | (bg << 4);
}

void vga_recolor_screen(void) {
    for (int s = 0; s < 12; s++) {
        for (int y = 0; y < VGA_HISTORY; y++) {
            for (int x = 0; x < VGA_WIDTH; x++) {
                unsigned char c = term_buffers[s][y][x] & 0xFF;
                term_buffers[s][y][x] = vga_entry(c, term_color);
            }
        }
    }
    vga_flush();
}

void vga_clear(void) {
    int session = current_vga_session;
    for (int y = 0; y < VGA_HISTORY; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            term_buffers[session][y][x] = vga_entry(' ', term_color);
        }
    }
    
    term_rows[session] = 0;
    term_cols[session] = 0;
    view_offsets[session] = 0;
    vga_flush();
    
    int start_y = show_banner ? 4 : 1;
    update_cursor(term_cols[session], start_y);
}

void vga_switch_session(int session) {
    if (session >= 0 && session < 12) {
        current_vga_session = session;
        vga_flush();
        int start_y = show_banner ? 4 : 1;
        int visible_rows = VGA_HEIGHT - start_y;
        int cur_y = term_rows[session];
        if (cur_y >= visible_rows) cur_y = visible_rows - 1;
        update_cursor(term_cols[session], start_y + cur_y);
    }
}

void vga_scroll_view(int lines) {
    int session = current_vga_session;
    int start_y = show_banner ? 4 : 1;
    int visible_rows = VGA_HEIGHT - start_y;
    
    int max_offset = term_rows[session] - visible_rows + 1;
    if (max_offset < 0) max_offset = 0;
    
    view_offsets[session] += lines;
    if (view_offsets[session] < 0) view_offsets[session] = 0;
    if (view_offsets[session] > max_offset) view_offsets[session] = max_offset;
    
    vga_flush();
}

void vga_init(void) {
    vga_disable_blink();
    for (int s = 0; s < 12; s++) {
        for (int y = 0; y < VGA_HISTORY; y++) {
            for (int x = 0; x < VGA_WIDTH; x++) {
                term_buffers[s][y][x] = vga_entry(' ', 0x0F);
            }
        }
        term_rows[s] = 0;
        term_cols[s] = 0;
        view_offsets[s] = 0;
    }
    vga_disable_banner();
    vga_clear();
    enable_cursor(14, 15);
    update_cursor(0, 0);
}

bool is_redirecting = false;
char redirect_buf[8192];
int redirect_len = 0;

void vga_putchar(char c) {
    if (is_redirecting && redirect_len < 8191 && c != '\b') redirect_buf[redirect_len++] = c;
    outb(0x3F8, c); /* debug to serial */
    int session = current_vga_session;
    int tr = term_rows[session];
    int tc = term_cols[session];

    if (c == '\n') {
        tc = 0;
        tr++;
    } else if (c == '\b') {
        if (tc > 0) {
            tc--;
        } else if (tr > 0) {
            tr--;
            tc = VGA_WIDTH - 1;
        }
    } else if (c == '\t') {
        int spaces = 4 - (tc % 4);
        for (int i = 0; i < spaces; i++) {
            term_buffers[session][tr][tc] = vga_entry(' ', term_color);
            tc++;
            if (tc >= VGA_WIDTH) {
                tc = 0;
                tr++;
                break;
            }
        }
    } else {
        term_buffers[session][tr][tc] = vga_entry(c, term_color);
        tc++;
        if (tc >= VGA_WIDTH) {
            tc = 0;
            tr++;
        }
    }

    if (tr >= VGA_HISTORY) {
        for (int y = 1; y < VGA_HISTORY; y++) {
            for (int x = 0; x < VGA_WIDTH; x++) {
                term_buffers[session][y-1][x] = term_buffers[session][y][x];
            }
        }
        for (int x = 0; x < VGA_WIDTH; x++) {
            term_buffers[session][VGA_HISTORY-1][x] = vga_entry(' ', term_color);
        }
        tr = VGA_HISTORY - 1;
    }
    
    term_rows[session] = tr;
    term_cols[session] = tc;
    
    view_offsets[session] = 0;
    vga_flush();
    
    int start_y = show_banner ? 4 : 1;
    int visible_rows = VGA_HEIGHT - start_y;
    int cur_y = tr;
    if (cur_y >= visible_rows) cur_y = visible_rows - 1;
    update_cursor(tc, start_y + cur_y);
}

void vga_print(const char* str) {
    for (int i = 0; str[i] != '\0'; i++)
        vga_putchar(str[i]);
}

void vga_disable_blink(void) {
    inb(0x3DA);
    
    outb(0x3C0, 0x10 | 0x20);
    
    uint8_t mode = inb(0x3C1);
    
    inb(0x3DA);
    outb(0x3C0, 0x10 | 0x20);
    
    outb(0x3C0, mode & ~0x08);
}


uint8_t vga_get_color() {
    return term_color;
}
