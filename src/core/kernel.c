#include "string.h"


#include <stdbool.h>
#include <stdint.h>
#include "vga.h"

#include "version.h"

static void vga_print_uint(uint32_t v);
#include "qr_code.h"

static int _panic_strlen(const char* s) {
    int l = 0;
    while(s[l]) l++;
    return l;
}

void kernel_panic(const char *err_msg, const char *file, int line) {

    asm volatile("cli");
    uint8_t current_color = vga_get_color();
    uint8_t user_fg = current_color & 0x0F;
    uint8_t user_bg = current_color >> 4;

    vga_set_color(user_fg, user_bg);
    vga_clear();
    vga_disable_banner();
    vga_clear();
    vga_print("\n"); 

    for (int i = 0; i < 15; i++) {
        int len = 0;
        
        if (i == 0) { vga_print("  _____    _____"); len = 16; }
        else if (i == 1) { vga_print("    |        |"); len = 14; }
        else if (i == 2) { vga_print("    |        |"); len = 14; }
        else if (i == 3) { vga_print("       ____"); len = 11; }
        else if (i == 5) { vga_print("  *** SCREEN OUTPUT OF DEATH ***"); len = 32; }
        else if (i == 6) { vga_print("  A fatal system error has"); len = 26; }
        else if (i == 7) { vga_print("  occurred and VIATAhos"); len = 23; }
        else if (i == 8) { vga_print("  has been halted."); len = 18; }
        else if (i == 10) { 
            vga_print("  Error: "); vga_print(err_msg); vga_flush(); 
            len = 9 + _panic_strlen(err_msg); 
        }
        else if (i == 11) { 
            vga_print("  Location: "); vga_print(file); vga_print(":"); vga_print_uint(line);
            len = 12 + _panic_strlen(file) + 1;
            int l = line; 
            if (l == 0) len++;
            while(l > 0) { l /= 10; len++; }
        }
        else if (i == 13) { vga_print("  Please scan the QR code"); len = 25; }
        else if (i == 14) { vga_print("  to report this bug ->"); len = 23; }
        
        // Pad with spaces up to column 45
        for (int p = len; p < 45; p++) {
            vga_putchar(' ');
        }
        
        if (i == 0) {
            vga_print("\xDA\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xBF");
        } else if (i == 14) {
            vga_print("\xC0\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xD9");
        } else {
            vga_putchar('\xB3');
            vga_putchar(' ');
            vga_print(qr_code_lines[i - 1]);
            vga_putchar(' ');
            vga_putchar('\xB3');
        }
        vga_print("\n");
    }

    while(1) { asm volatile("hlt"); }
}
#define PANIC(msg) kernel_panic(msg, __FILE__, __LINE__)
#include "io.h"
#include "ahci.h"
#include "ata.h"
#include "ahci.h"

/* Magic base value that signals "use AHCI" instead of legacy ATA PIO */
#define ATA_BASE_AHCI 0xAC1u

#include "lz77.h"
#include "exec.h"

/* =========================================================================
   Drive Detection — VIATAhos
   Probes all 4 IDE slots (primary/secondary × master/slave) via ATA IDENTIFY.
   Also reads CMOS register 0x10 for floppy drive presence.
   Maps each found device to a VIATAhos drive letter per spec:
     A = Floppy     B = CD/DVD     C = USB/Flash
     D = HDD/SSD    E = Nastro     F = Virtuale (QEMU/VM)
   ========================================================================= */

typedef enum {
    DRV_NONE = 0,
    DRV_FLOPPY,      
    DRV_CD,          
    DRV_USB_FLASH,   
    DRV_HDD,         
    DRV_TAPE,        
    DRV_VIRTUAL,     
} viata_hw_type_t;


static const char * const _sub_names[] __attribute__((unused)) = {
     "V1", "V2",
     "X1", "X2",
     "R1", "R2",
     "D1", "D2",
     "F1", "F2",
     "N1", "N2",
};





typedef struct {
    bool present;
    bool is_boot;
    viata_hw_type_t hw_type;
    uint16_t ata_base;
    uint8_t ata_sel;
    char model[41];
    uint32_t size_mb;
    char letter;
    char sub[4];
    bool is_ahci;
    int ahci_port;
} viata_drive_info_t;

viata_drive_info_t *current_fat32_drv = 0;
#include "fat32.h"
#include "iso9660.h"
#include "tar.h"
#include "fat_legacy.h"
static viata_drive_info_t _drives[5];
static int                _drive_count = 0;


static bool _is_vm_model(const char *model) {
    static const char * const vm_sigs[] = {
        "QEMU", "VBOX", "VMWARE", "BOCHS", "VIRTUAL", "HYPER", 0
    };
    for (int si = 0; vm_sigs[si]; si++) {
        const char *sig = vm_sigs[si];
        
        for (int i = 0; model[i]; i++) {
            int j = 0;
            while (sig[j] && model[i+j]) {
                char a = model[i+j], b = sig[j];
                if (a >= 'a' && a <= 'z') a -= 32;
                if (a != b) break;
                j++;
            }
            if (!sig[j]) return true;
        }
    }
    return false;
}


/* =========================================================================
   DRIVE DETECTION & MANAGEMENT
   Hardware identification for disks via ATA IDENTIFY and CMOS
   ========================================================================= */
static void _trim_spaces(char *s) {
    int l = 0; while (s[l]) l++;
    while (l > 0 && s[l-1] == ' ') l--;
    s[l] = '\0';
}


static void _decode_model(const uint16_t *buf, char *out) {
    int o = 0;
    for (int w = ATA_ID_MODEL_START; w <= ATA_ID_MODEL_END; w++) {
        out[o++] = (char)((buf[w] >> 8) & 0xFF);
        out[o++] = (char)(buf[w] & 0xFF);
    }
    out[40] = '\0';
    _trim_spaces(out);
}


static const char *viata_drive_type_name(viata_hw_type_t t) {
    switch (t) {
        case DRV_FLOPPY:   return "Floppy Disk";
        case DRV_CD:       return "CD / DVD";
        case DRV_USB_FLASH:return "Flash USB / SD";
        case DRV_HDD:      return "Disco Fisso ATA";
        case DRV_TAPE:     return "Magnetic Tape";
        case DRV_VIRTUAL:  return "RAM Disk (Live)";
        default:           return "Sconosciuto";
    }
}






static void _assign_sub_code(viata_drive_info_t *d, int same) {
    char prefix;
    switch(d->hw_type) {
        case DRV_FLOPPY:     prefix = 'F'; break;
        case DRV_CD:         prefix = 'D'; break;
        case DRV_USB_FLASH:  prefix = 'R'; break;
        case DRV_HDD:        prefix = 'X'; break;
        case DRV_TAPE:       prefix = 'N'; break;
        case DRV_VIRTUAL:    prefix = 'V'; break;
        default:             prefix = 'U'; break;
    }
    
    d->sub[0] = prefix;
    d->sub[1] = '1' + same;
    d->sub[2] = '\0';
}

static void _assign_letter(viata_drive_info_t *d, char letter) {
    d->letter = letter;
    
    int same = 0;
    for (int i = 0; i < _drive_count; i++)
        if (_drives[i].present && _drives[i].letter == letter) same++;
        
    if (same == 0) {
        d->sub[0] = '\0';  
    } else {
        _assign_sub_code(d, same - 1);
    }
}

static void _probe_ata_slot(uint16_t base, uint8_t dev_sel) {
    if (_drive_count >= 5) return;
    static uint16_t _id_buf[256];
    bool is_atapi; uint8_t atapi_type;

    if (!ata_identify(base, dev_sel, _id_buf, &is_atapi, &atapi_type)) return;

    viata_drive_info_t *d = &_drives[_drive_count];
    d->present  = true;
    d->ata_base = base;
    d->ata_sel  = dev_sel;
    d->is_boot  = false;
    _decode_model(_id_buf, d->model);

    if (is_atapi) {
        switch (atapi_type) {
            case ATAPI_TYPE_CD:
            case ATAPI_TYPE_OPTIC:
                d->hw_type = DRV_CD;
                uint32_t blocks = atapi_read_capacity(base, dev_sel);
                if (blocks > 0) d->size_mb = (blocks * 2048) / (1024 * 1024);
                else d->size_mb = 700; /* Fallback */
                break;
            case ATAPI_TYPE_TAPE:
                d->hw_type = DRV_TAPE;
                d->size_mb = 0;
                break;
            case ATAPI_TYPE_FLOPPY:
            default:
                d->hw_type = DRV_FLOPPY;
                d->size_mb = 1;
                break;
        }
    } else {
        uint64_t total_secs = 0;
        if (_id_buf[83] & (1 << 10)) { 
            total_secs = (uint64_t)_id_buf[100] | ((uint64_t)_id_buf[101] << 16) | 
                         ((uint64_t)_id_buf[102] << 32) | ((uint64_t)_id_buf[103] << 48);
        } else {
            total_secs = ((uint32_t)_id_buf[ATA_ID_W61_SECTORS] << 16) | (uint32_t)_id_buf[ATA_ID_W60_SECTORS];
        }
        
        if (total_secs > 0) d->size_mb = (uint32_t)(total_secs / 2048ULL);
        else d->size_mb = 16;

        if (_is_vm_model(d->model)) {
            d->hw_type = DRV_VIRTUAL;
        } else {
            bool removable = (_id_buf[0] & 0x0080u) != 0;
            if (removable) {
                d->hw_type = DRV_USB_FLASH;
            } else {
                d->hw_type = DRV_HDD;
            }
        }
    }
    _drive_count++;
}

static void _probe_floppy_cmos(void) {
    extern void vga_print(const char*);
    outb(0x70, 0x10);       
    uint8_t cmos = inb(0x71);
    uint8_t drv_a = (cmos >> 4) & 0x0F;


    viata_drive_info_t *d = &_drives[_drive_count];
    d->present  = true;
    d->ata_base = 0;
    d->ata_sel = 0;
    d->hw_type  = DRV_FLOPPY;
    d->is_boot  = false;
    if (drv_a == 4) d->size_mb = 1; /* 1.44 MB -> 1 MB */
    else if (drv_a == 5) d->size_mb = 2; /* 2.88 MB -> 2 MB */
    else if (drv_a == 3) d->size_mb = 0; /* 720 KB -> 0 MB */
    else d->size_mb = 0;
    d->model[0] = 'F'; d->model[1] = 'D'; d->model[2] = 'D'; d->model[3] = '\0';
    _drive_count++;
}

static void _probe_ahci_drives(void) {
    if (!ahci_init()) return;
    for (int p = 0; p < 32; p++) {
        if (!(ahci_active_ports & (1u << p))) continue;
        if (_drive_count >= 10) break;
        
        uint8_t id_buf[512];
        if (!ahci_read_sector(p, 0, id_buf)) continue;

        viata_drive_info_t *d = &_drives[_drive_count];
        d->present  = true;
        d->ata_base = ATA_BASE_AHCI;
        d->ata_sel  = (uint8_t)p;
        d->is_ahci  = true;
        d->ahci_port = p;
        d->is_boot  = false;
        uint16_t id_buf16[256];
        if (ahci_identify(p, id_buf16)) {
        uint64_t total_secs = 0;
        if (id_buf16[83] & (1 << 10)) { 
            total_secs = (uint64_t)id_buf16[100] | ((uint64_t)id_buf16[101] << 16) | 
                         ((uint64_t)id_buf16[102] << 32) | ((uint64_t)id_buf16[103] << 48);
        } else {
            total_secs = ((uint32_t)id_buf16[61] << 16) | (uint32_t)id_buf16[60];
        }
        
        if (total_secs > 0) d->size_mb = (uint32_t)(total_secs / 2048ULL);
        else d->size_mb = 16;
            
            _decode_model(id_buf16, d->model);
            if (_is_vm_model(d->model)) {
                d->hw_type = DRV_VIRTUAL;
            } else {
                bool removable = (id_buf16[0] & 0x0080u) != 0;
                if (removable) d->hw_type = DRV_USB_FLASH;
                else d->hw_type = DRV_HDD;
            }
        } else {
            d->size_mb = 16;
            char *mdl = "AHCI Drive ";
            for (int i=0; i<11; i++) d->model[i] = mdl[i];
            d->model[11] = '\0';
            d->hw_type = DRV_USB_FLASH;
        }
        
        _drive_count++;

    }

}

static void detect_drives(void) {
    _drive_count = 0;

    _probe_ahci_drives();

    _probe_ata_slot(0x1F0, 0xA0);
    _probe_ata_slot(0x1F0, 0xB0);
    _probe_ata_slot(0x170, 0xA0);
    _probe_ata_slot(0x170, 0xB0);

    if (_drive_count == 0) {
        _drives[0].present = true;
        _drives[0].hw_type = DRV_USB_FLASH;
        _drives[0].ata_base = 0xEEEE;
        _drives[0].ata_sel = 0;
        _drives[0].size_mb = 3;
        const char *m = "RAM Disk";
        for (int i=0; i<9; i++) _drives[0].model[i] = m[i];
        _drive_count = 1;
        // Add visual warning immediately (though vga might not be ready yet, it'll print early in boot sequence)
        vga_print("WARNING: No physical SATA/IDE drives detected!\n");
        vga_print("         Running from volatile RAM Disk.\n");
        vga_print("         Files will NOT be saved to your USB stick!\n");
    }

    /* OSDISK Hierarchy Logic:
       1. Find the primary boot drive type to determine the global OSDISK letter.
       2. Assign that letter to ALL drives.
       3. The primary boot drive gets no sub-code.
       4. All other drives get a sub-code based on their hw_type (D1, X1, etc.)
    */
    char osdisk_letter = 'C'; 
    int boot_idx = -1;
    
    int type_prio[] = { DRV_USB_FLASH, DRV_HDD, DRV_VIRTUAL, DRV_CD, DRV_FLOPPY, DRV_TAPE };
    char letter_prio[] = { 'C', 'D', 'F', 'B', 'A', 'E' };
    
    for (int p = 0; p < 6; p++) {
        for (int i = 0; i < _drive_count; i++) {
            if (_drives[i].hw_type == (viata_hw_type_t)type_prio[p]) {
                osdisk_letter = letter_prio[p];
                boot_idx = i;
                break;
            }
        }
        if (boot_idx != -1) break;
    }

    if (boot_idx == -1) boot_idx = 0;

    int sub_counters[8] = {0};

    for (int i = 0; i < _drive_count; i++) {
        _drives[i].letter = osdisk_letter;
        
        if (i == boot_idx) {
            _drives[i].is_boot = true;
            _drives[i].sub[0] = '\0';
        } else {
            _drives[i].is_boot = false;
            char prefix = 'U';
            switch(_drives[i].hw_type) {
                case DRV_FLOPPY:     prefix = 'F'; break;
                case DRV_CD:         prefix = 'D'; break;
                case DRV_USB_FLASH:  prefix = 'R'; break;
                case DRV_HDD:        prefix = 'X'; break;
                case DRV_TAPE:       prefix = 'N'; break;
                case DRV_VIRTUAL:    prefix = 'V'; break;
                case DRV_NONE:       break;
            }
            int type_idx = _drives[i].hw_type;
            sub_counters[type_idx]++;
            
            _drives[i].sub[0] = prefix;
            _drives[i].sub[1] = '0' + sub_counters[type_idx];
            _drives[i].sub[2] = '\0';
        }
    }
}

static viata_drive_info_t *viata_get_drive(char letter, const char *sub) {
    for (int i = 0; i < _drive_count; i++) {
        if (_drives[i].present && _drives[i].letter == letter) {
            if (sub && sub[0]) {
                if (str_cmp(_drives[i].sub, sub) == 0) return &_drives[i];
            } else {
                if (_drives[i].sub[0] == '\0') return &_drives[i];
            }
        }
    }
    return 0;
}
static char viata_boot_drive_letter(void) {
    for (int i = 0; i < _drive_count; i++) {
        if (_drives[i].is_boot) return _drives[i].letter;
    }
    return 'C';
}

__asm__(
    ".pushsection .rodata\n"
    ".global by_text_start\n"
    "by_text_start:\n"
    ".incbin \"src/utils/by.txt\"\n"
    ".byte 0\n"
    ".popsection\n"
);
extern const char by_text_start[];




static uint32_t last_cmd_duration = 0; 
static uint32_t boot_timestamp = 0;

static uint32_t suspend_timer_start = 0;
static bool suspend_screen_active = false;



/* =========================================================================
   SYSTEM & HARDWARE UTILITIES
   CPU, GPU, Power Management, and Hypervisor Detection
   ========================================================================= */
void vga_screen_off(void) {
    outb(0x3C4, 0x01);
    uint8_t sr1 = inb(0x3C5);
    outb(0x3C5, sr1 | 0x20);

    inb(0x3DA);
    outb(0x3C0, 0x00);
}

void vga_screen_on(void) {
    outb(0x3C4, 0x01);
    uint8_t sr1 = inb(0x3C5);
    outb(0x3C5, sr1 & ~0x20);

    inb(0x3DA);
    outb(0x3C0, 0x20);
}

static inline void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf));
}

static bool is_hypervisor(void) {
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    return (ecx & (1u << 31)) != 0;
}

static inline uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static uint8_t bcd_to_bin(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

static void print_two_digits(uint8_t val) {
    char buf[3];
    buf[0] = '0' + (val / 10);
    buf[1] = '0' + (val % 10);
    buf[2] = '\0';
    vga_print(buf);
}

/* =========================================================================
   TIME & CLOCK
   RTC reading and boot time calculation
   ========================================================================= */
static uint32_t get_current_day_seconds() {
    uint8_t s1, m1, h1, s2, m2, h2;
    do {
        while (cmos_read(0x0A) & 0x80) {
            for (volatile int i = 0; i < 1000; i++) {}
        }
        s1 = cmos_read(0x00);
        m1 = cmos_read(0x02);
        h1 = cmos_read(0x04);
        s2 = cmos_read(0x00);
        m2 = cmos_read(0x02);
        h2 = cmos_read(0x04);
    } while (s1 != s2 || m1 != m2 || h1 != h2);

    uint8_t reg_b = cmos_read(0x0B);
    if (!(reg_b & 0x04)) { 
        s1 = bcd_to_bin(s1);
        m1 = bcd_to_bin(m1);
        h1 = bcd_to_bin(h1);
    }
    
    return (uint32_t)h1 * 3600 + (uint32_t)m1 * 60 + s1;
}

void init_boot_time() {
    boot_timestamp = get_current_day_seconds();
}

uint32_t get_uptime_seconds() {
    uint32_t current = get_current_day_seconds();
    if (current >= boot_timestamp) {
        return current - boot_timestamp;
    } else {
        return (current + 86400) - boot_timestamp;
    }
}

void print_uptime() {
    uint32_t uptime = get_uptime_seconds();
    uint32_t hours = uptime / 3600;
    uint32_t mins = (uptime % 3600) / 60;
    uint32_t secs = uptime % 60;

    vga_print("Uptime: ");
    if (hours > 0) {
        print_two_digits((uint8_t)hours);
        vga_print("h ");
    }
    print_two_digits((uint8_t)mins);
    vga_print("m ");
    print_two_digits((uint8_t)secs);
    vga_print("s\n");
}

void print_rtc_datetime() {
    uint8_t sec   = bcd_to_bin(cmos_read(0x00));
    uint8_t min   = bcd_to_bin(cmos_read(0x02));
    uint8_t hour  = bcd_to_bin(cmos_read(0x04));
    uint8_t day   = bcd_to_bin(cmos_read(0x07));
    uint8_t month = bcd_to_bin(cmos_read(0x08));
    uint8_t year  = bcd_to_bin(cmos_read(0x09));

    vga_print("Current Date & Time: 20");
    print_two_digits(year);
    vga_print("-");
    print_two_digits(month);
    vga_print("-");
    print_two_digits(day);
    vga_print(" ");
    print_two_digits(hour);
    vga_print(":");
    print_two_digits(min);
    vga_print(":");
    print_two_digits(sec);
    vga_print("\n");
}

void print_last_cmd_time() {
    vga_print("Last command execution time: ");
    print_two_digits((uint8_t)(last_cmd_duration / 60));
    vga_print("m ");
    print_two_digits((uint8_t)(last_cmd_duration % 60));
    vga_print("s\n");
}




typedef struct {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} __attribute__((packed)) rsdp_t;

typedef struct {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) acpi_header_t;

static uint32_t acpi_smi_cmd = 0;
static uint8_t acpi_enable = 0;
static uint32_t acpi_pm1a_cnt = 0;
static uint32_t acpi_pm1b_cnt = 0;
static uint16_t acpi_slp_typa = 0;
static uint16_t acpi_slp_typb = 0;

static bool acpi_reset_supported = false;
static uint8_t acpi_reset_space_id = 0;
static uint32_t acpi_reset_addr = 0;
static uint8_t acpi_reset_value = 0;

static bool acpi_initialized = false;

/* mem_cmp moved to string.c */
static bool acpi_validate_checksum(const uint8_t *ptr, uint32_t len) {
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len; i++) {
        sum += ptr[i];
    }
    return sum == 0;
}

static bool acpi_range_valid(uint32_t addr, uint32_t len) {
    if (addr == 0 || len == 0) return false;
    if (len > 0x1000000U) return false;
    if ((uint64_t)addr + len < (uint64_t)addr) return false;
    if (addr + len < addr) return false;
    return true;
}

/* =========================================================================
   ACPI MANAGEMENT
   Advanced Configuration and Power Interface setup
   ========================================================================= */
void init_acpi(void) {
    if (acpi_initialized) return;

    rsdp_t *rsdp = 0;

    uint16_t ebda_seg = 0;
    if ((uintptr_t)0x040E < 0x100000) {
        ebda_seg = *(uint16_t *)(uintptr_t)0x040E;
    }
    uint32_t ebda_addr = (uint32_t)ebda_seg << 4;
    if (ebda_addr >= 0x80000 && ebda_addr < 0xA0000) {
        for (uint32_t p = ebda_addr; p < ebda_addr + 1024; p += 16) {
            if (p + 20 < 0x100000 && mem_cmp((void*)(uintptr_t)p, "RSD PTR ", 8) == 0) {
                if (acpi_validate_checksum((uint8_t*)(uintptr_t)p, 20)) {
                    rsdp = (rsdp_t *)(uintptr_t)p;
                    break;
                }
            }
        }
    }

    if (!rsdp) {
        for (uint32_t p = 0x000E0000; p < 0x000FFFFF; p += 16) {
            if (p + 20 < 0x100000 && *(uint32_t*)(uintptr_t)p == 0x20445352 /* "RSD " */) {
                if (mem_cmp((void*)(uintptr_t)p, "RSD PTR ", 8) == 0) {
                    if (acpi_validate_checksum((uint8_t*)(uintptr_t)p, 20)) {
                        rsdp = (rsdp_t *)(uintptr_t)p;
                        break;
                    }
                }
            }
        }
    }

    if (!rsdp || rsdp->rsdt_address == 0) return;
    if (!acpi_range_valid(rsdp->rsdt_address, sizeof(acpi_header_t))) return;

    acpi_header_t *rsdt = (acpi_header_t *)(uintptr_t)(rsdp->rsdt_address);
    if (!rsdt || mem_cmp(rsdt->signature, "RSDT", 4) != 0) return;
    if (rsdt->length < sizeof(acpi_header_t) || rsdt->length > 0x10000) return;
    if (!acpi_validate_checksum((uint8_t*)rsdt, rsdt->length)) return;

    int entries = (rsdt->length - sizeof(acpi_header_t)) / 4;
    if (entries <= 0 || entries > 64) return;

    uint32_t *table_ptrs = (uint32_t *)((uint8_t *)rsdt + sizeof(acpi_header_t));
    acpi_header_t *facp = 0;

    for (int i = 0; i < entries; i++) {
        uint32_t addr = table_ptrs[i];
        if (addr == 0) continue;
        if (!acpi_range_valid(addr, sizeof(acpi_header_t))) continue;
        acpi_header_t *header = (acpi_header_t *)(uintptr_t)addr;
        if (header && *(uint32_t*)header->signature == 0x50434146 /* "FACP" */) {
            if (header->length >= sizeof(acpi_header_t) && header->length <= 0x10000 &&
                acpi_validate_checksum((uint8_t*)header, header->length)) {
                facp = header;
                break;
            }
        }
    }

    if (!facp) return;

    uint8_t *facp_bytes = (uint8_t *)facp;
    if (facp->length < 72) return;

    uint32_t dsdt_addr = *(uint32_t *)(facp_bytes + 40);
    acpi_smi_cmd  = *(uint32_t *)(facp_bytes + 48);
    acpi_enable   = *(uint8_t *)(facp_bytes + 52);
    acpi_pm1a_cnt = *(uint32_t *)(facp_bytes + 64);
    acpi_pm1b_cnt = *(uint32_t *)(facp_bytes + 68);

    if (facp->length >= 129) {
        uint32_t flags = *(uint32_t*)(facp_bytes + 112);
        if (flags & (1 << 10)) {
            acpi_reset_space_id = *(uint8_t*)(facp_bytes + 116);
            uint64_t addr64 = *(uint64_t*)(facp_bytes + 116 + 4);
            acpi_reset_addr = (uint32_t)addr64;
            acpi_reset_value = *(uint8_t*)(facp_bytes + 128);
            acpi_reset_supported = true;
        }
    }

    acpi_slp_typa = (5 << 10);
    acpi_slp_typb = (5 << 10);
    acpi_initialized = true;

    if (dsdt_addr == 0) return;
    if (!acpi_range_valid(dsdt_addr, sizeof(acpi_header_t))) return;
    acpi_header_t *dsdt = (acpi_header_t *)(uintptr_t)dsdt_addr;
    if (!dsdt || *(uint32_t*)dsdt->signature != 0x54445344 /* "DSDT" */) return;
    if (dsdt->length < sizeof(acpi_header_t) || dsdt->length > 0x20000) return;

    uint8_t *dsdt_bytes = (uint8_t *)dsdt;
    uint32_t dsdt_len = dsdt->length;
    if (dsdt_len > 0x20000) dsdt_len = 0x20000;

    for (uint32_t i = 0; i < dsdt_len - 8; i++) {
        if (*(uint32_t*)(dsdt_bytes + i) == 0x5F35535F /* "_S5_" */) {
            uint32_t ptr = i + 4;
            if (dsdt_bytes[ptr] == 0x12) {
                ptr += 3;
                if (dsdt_bytes[ptr] == 0x0A) ptr++;
                uint8_t typa = dsdt_bytes[ptr++];
                if (dsdt_bytes[ptr] == 0x0A) ptr++;
                uint8_t typb = dsdt_bytes[ptr];

                acpi_slp_typa = (uint16_t)(typa & 0x07) << 10;
                acpi_slp_typb = (uint16_t)(typb & 0x07) << 10;
                return;
            }
        }
    }
}

void sys_shutdown(void) {
    __asm__ volatile ("cli");

    init_acpi();

    if (acpi_initialized && acpi_pm1a_cnt != 0 && acpi_pm1a_cnt < 0x10000) {
        if ((inw((uint16_t)acpi_pm1a_cnt) & 1) == 0) {
            if (acpi_smi_cmd != 0 && acpi_smi_cmd < 0x10000 && acpi_enable != 0) {
                outb((uint16_t)acpi_smi_cmd, acpi_enable);
                for (volatile int i = 0; i < 50000; i++) {
                    if (inw((uint16_t)acpi_pm1a_cnt) & 1) break;
                }
            }
        }

        uint16_t pm1a_val = acpi_slp_typa | (1 << 13);
        outw((uint16_t)acpi_pm1a_cnt, pm1a_val);

        if (acpi_pm1b_cnt != 0 && acpi_pm1b_cnt < 0x10000) {
            uint16_t pm1b_val = acpi_slp_typb | (1 << 13);
            outw((uint16_t)acpi_pm1b_cnt, pm1b_val);
        }

        for (volatile int i = 0; i < 50000; i++) { __asm__ volatile("nop"); }
    }

    if (is_hypervisor()) {
        outw(0x604, 0x2000);   
        outw(0x604, 0x3400);   
        outw(0x4004, 0x3400);  
        outw(0xB004, 0x2000);  
        outb(0x501, 0x00);     
    }

    vga_print("Shutdown completed. You can turn off the computer.\n");

    while (1) {
        __asm__ volatile ("hlt");
    }
}

void sys_reboot(void) {
    init_acpi();

    if (acpi_reset_supported && acpi_reset_space_id == 1 && acpi_reset_addr < 0x10000) { 
        outb((uint16_t)acpi_reset_addr, acpi_reset_value);
    }
    
    // PCI Reset
    outb(0xCF9, 0x02);
    outb(0xCF9, 0x06);

    // Fast A20 Reset
    uint8_t val = inb(0x92);
    outb(0x92, val | 1);

    // 8042 Keyboard Controller Reset (with timeout to avoid hang)
    for (int j = 0; j < 100000; j++) {
        if ((inb(0x64) & 0x02) == 0) break;
    }
    outb(0x64, 0xFE);

    while (1) {
        __asm__ volatile("cli; hlt");
    }
}


uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(0xCF8, address);
    return inl(0xCFC);
}

static void print_hex16(uint16_t val) {
    const char hex_chars[] = "0123456789ABCDEF";
    vga_putchar(hex_chars[(val >> 12) & 0xF]);
    vga_putchar(hex_chars[(val >> 8) & 0xF]);
    vga_putchar(hex_chars[(val >> 4) & 0xF]);
    vga_putchar(hex_chars[val & 0xF]);
}

void print_gpu_hardware_info() {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t vendor_device = pci_read_config((uint8_t)bus, (uint8_t)slot, func, 0x00);
                uint16_t vendor = vendor_device & 0xFFFF;
                if (vendor == 0xFFFF) continue;

                uint32_t class_reg = pci_read_config((uint8_t)bus, (uint8_t)slot, func, 0x08);
                uint8_t base_class = (class_reg >> 24) & 0xFF;

                if (base_class == 0x03) {
                    uint16_t device = (vendor_device >> 16) & 0xFFFF;
                    if (vendor == 0x10DE)      vga_print("NVIDIA Corporation GPU");
                    else if (vendor == 0x1002) vga_print("AMD/ATI Radeon Graphics");
                    else if (vendor == 0x8086) vga_print("Intel HD/UHD Graphics");
                    else if (vendor == 0x1234) {
                        if (device == 0x1111) vga_print("QEMU Standard VGA Emulator (Bochs VBE)");
                        else vga_print("QEMU Standard VGA Emulator");
                    }
                    else if (vendor == 0x15AD) {
                        if (device == 0x0405) vga_print("VMware SVGA II Adapter");
                        else vga_print("VMware Adapter");
                    }
                    else if (vendor == 0x80EE) {
                        if (device == 0xBEEF) vga_print("VirtualBox Graphics Adapter");
                        else vga_print("VirtualBox Adapter");
                    }
                    else                       vga_print("Generic VGA Compatible Controller");
                    

                    return;
                }
            }
        }
    }
    vga_print("Generic VGA Display Adapter");
}


/* =========================================================================
   SYS DYNAMIC INFO HELPERS
   Disk (FAT32 free cluster scan), RAM (CMOS), Display (VGA CRTC)
   ========================================================================= */

static void print_uint32_dec(uint32_t v) {
    if (v == 0) { vga_putchar('0'); return; }
    char buf[12]; int idx = 0;
    while (v > 0) { buf[idx++] = '0' + (v % 10); v /= 10; }
    for (int i = idx - 1; i >= 0; i--) vga_putchar(buf[i]);
}

static void print_disk_info(void) {
    /* Identify boot drive type and use the right method */
    char boot_d = viata_boot_drive_letter();
    viata_drive_info_t *drv = viata_get_drive(boot_d, "");

    if (!drv) {
        vga_print("N/A");
        return;
    }

    uint32_t total_kb = drv->size_mb * 1024u;
    uint32_t used_kb  = 0;

    switch (drv->hw_type) {
        case DRV_HDD:
        case DRV_VIRTUAL:
        case DRV_USB_FLASH: {
            total_kb = fat32_get_total_kb();
            if (total_kb == 0) total_kb = drv->size_mb * 1024u;
            uint32_t tot_sec = (total_kb * 1024u) / 512u;
            if (tot_sec > FAT32_PART_LBA) tot_sec -= FAT32_PART_LBA;
            uint32_t total_clusters = tot_sec / 8;
            uint32_t free_kb = fat32_get_free_space_kb(total_clusters);
            used_kb = total_kb > free_kb ? total_kb - free_kb : 0;
            break;
        }
        case DRV_FLOPPY:
            /* FAT12: size is tiny and fixed — 1.44MB = 1440KB. No free scan, show size only. */
            total_kb = 1440u;
            used_kb  = 0;   /* can't scan FAT12 without legacy DMA */
            break;
        case DRV_CD:
            /* ISO9660: read-only. Size is fixed at 700MB. Disk "used" = full capacity. */
            total_kb = 700u * 1024u;
            used_kb  = total_kb;
            break;
        case DRV_TAPE:
            /* TAR / Magnetic tape: sequential, no random access to FAT. Show raw size. */
            total_kb = drv->size_mb > 0 ? drv->size_mb * 1024u : 2048u;
            used_kb  = 0;
            break;
        default:
            vga_print("N/A");
            return;
    }

    if (total_kb >= 1024u) {
        if (used_kb > 0) { print_uint32_dec(used_kb / 1024u); vga_print(" MB Used / "); }
        else               vga_print("N/A Used / ");
        print_uint32_dec(total_kb / 1024u); vga_print(" MB Total");
    } else {
        if (used_kb > 0) { print_uint32_dec(used_kb); vga_print(" KB Used / "); }
        else               vga_print("N/A Used / ");
        print_uint32_dec(total_kb); vga_print(" KB Total");
    }
}


/* =========================================================================
   PHYSICAL MEMORY MANAGER (PMM)
   Tracks 4KB pages in a bitmap.
   ========================================================================= */
#define PMM_BLOCK_SIZE 4096
#define PMM_MAX_BLOCKS 32768  /* 128 MB max for our simple bitmap */
static uint32_t pmm_bitmap[PMM_MAX_BLOCKS / 32];
static uint32_t pmm_used_frames = 0;
static uint32_t pmm_total_frames = 0;

static void pmm_set(uint32_t bit) { pmm_bitmap[bit / 32] |= (1 << (bit % 32)); }
static void pmm_clear(uint32_t bit) { pmm_bitmap[bit / 32] &= ~(1 << (bit % 32)); }
static bool pmm_test(uint32_t bit) { return pmm_bitmap[bit / 32] & (1 << (bit % 32)); }

extern uint8_t bss_end[];

static void pmm_init(uint32_t total_kb) {
    pmm_total_frames = (total_kb * 1024) / PMM_BLOCK_SIZE;
    if (pmm_total_frames > PMM_MAX_BLOCKS) pmm_total_frames = PMM_MAX_BLOCKS;
    for (uint32_t i = 0; i < PMM_MAX_BLOCKS / 32; i++) pmm_bitmap[i] = 0;
    pmm_used_frames = 0;
    
    /* Mark first 4MB (or up to bss_end) as reserved (kernel + BIOS/VGA memory + kernel BSS) */
    uintptr_t max_reserved = (uintptr_t)bss_end;
    if (max_reserved < 4 * 1024 * 1024) max_reserved = 4 * 1024 * 1024;
    uint32_t reserved_frames = (uint32_t)((max_reserved + PMM_BLOCK_SIZE - 1) / PMM_BLOCK_SIZE);
    for (uint32_t i = 0; i < reserved_frames; i++) {
        pmm_set(i);
        pmm_used_frames++;
    }
}

static uint32_t pmm_alloc_page(void) {
    for (uint32_t i = 0; i < pmm_total_frames; i++) {
        if (!pmm_test(i)) {
            pmm_set(i);
            pmm_used_frames++;
            return i * PMM_BLOCK_SIZE;
        }
    }
    return 0; // Out of memory
}

static void pmm_free_page(uint32_t addr) {
    uint32_t frame = addr / PMM_BLOCK_SIZE;
    if (pmm_test(frame)) {
        pmm_clear(frame);
        pmm_used_frames--;
    }
}

static void print_ram_info(void) {
    /* CMOS 0x30/0x31: extended memory in KB (below 16MB) */
    uint32_t ext_low  = (uint32_t)cmos_read(0x30) | ((uint32_t)cmos_read(0x31) << 8);
    uint32_t ext_high = (uint32_t)cmos_read(0x34) | ((uint32_t)cmos_read(0x35) << 8);

    uint32_t total_kb = 640u + ext_low + ext_high * 64u;
    
    // Fallback: Probe memory if CMOS returns suspiciously low value (e.g., < 64MB)
    // We mapped the first 4GB, so we can probe up to 4GB.
    if (total_kb < 64000u) {
        uint32_t max_mb = 16;
        for (uint32_t mb = 16; mb < 4000; mb += 4) {
            volatile uint32_t *ptr = (volatile uint32_t *)(uintptr_t)(mb * 1024u * 1024u);
            uint32_t old_val = *ptr;
            *ptr = 0xAA55AA55;
            if (*ptr == 0xAA55AA55) {
                *ptr = ~0xAA55AA55;
                if (*ptr == ~0xAA55AA55) {
                    *ptr = old_val;
                    max_mb = mb + 4;
                    continue;
                }
            }
            break;
        }
        total_kb = max_mb * 1024u;
    }

    uint32_t total_mb = total_kb / 1024u;

    if (pmm_total_frames == 0) pmm_init(total_kb);

    uint32_t used_kb = pmm_used_frames * (PMM_BLOCK_SIZE / 1024u);

    if (total_mb > 0) {
        print_uint32_dec(used_kb / 1024u); vga_print(" MB Used / ");
        print_uint32_dec(total_mb); vga_print(" MB Total");
    } else {
        print_uint32_dec(used_kb); vga_print(" KB Used / ");
        print_uint32_dec(total_kb); vga_print(" KB Total");
    }
}


static void print_display_info(void) {
    /*
     * Read VGA CRTC registers via port 0x3D4/0x3D5.
     * Horizontal Total (reg 0x00): (value+5)*8 = pixel width (approx).
     * Vertical Display End (reg 0x12): lower 8 bits. Bit 1&6 of Overflow (0x07) for top bits.
     * These give the active display resolution.
     */
    outb(0x3D4, 0x01); /* Horizontal Display End */
    uint8_t h_end = inb(0x3D5);
    uint32_t width = ((uint32_t)h_end + 1u) * 8u;

    outb(0x3D4, 0x12); /* Vertical Display End */
    uint8_t v_end = inb(0x3D5);
    outb(0x3D4, 0x07); /* Overflow register */
    uint8_t overflow = inb(0x3D5);
    uint32_t height = (uint32_t)v_end
                    | (((uint32_t)(overflow >> 1) & 1u) << 8)
                    | (((uint32_t)(overflow >> 6) & 1u) << 9);
    height += 1u;

    /* Sanity clamp: QEMU text 80x25, graphics 640x480, 800x600, 1024x768 */
    if (width  < 320u || width  > 2048u) width  = 1024u;
    if (height < 200u || height > 1600u) height = 768u;

    /* Colour depth: read DAC state — always 32-bit in QEMU VGA emulation */
    print_uint32_dec(width); vga_putchar('x');
    print_uint32_dec(height); vga_print(" 32-bit");
}

void print_cpu_hardware_info() {
    uint32_t eax, ebx, ecx, edx;

    char vendor[13];
    cpuid(0, &eax, &ebx, &ecx, &edx);
    ((uint32_t*)vendor)[0] = ebx;
    ((uint32_t*)vendor)[1] = edx;
    ((uint32_t*)vendor)[2] = ecx;
    vendor[12] = '\0';

    uint32_t max_ext;
    cpuid(0x80000000, &max_ext, &ebx, &ecx, &edx);

    if (max_ext >= 0x80000004) {
        uint32_t brand[13];
        cpuid(0x80000002, &brand[0], &brand[1], &brand[2], &brand[3]);
        cpuid(0x80000003, &brand[4], &brand[5], &brand[6], &brand[7]);
        cpuid(0x80000004, &brand[8], &brand[9], &brand[10], &brand[11]);
        brand[12] = 0;
        char* b = (char*)brand;
        while (*b == ' ' || *b == '\t') b++;
        vga_print(b);
    } else {
        vga_print(vendor);
    }
}




#define MAX_USERS 10
#define STR_LEN 21
#define KBD_BUF_SIZE 70
#define MAX_HISTORY 15

/* =========================================================================
   USER & SESSION MANAGEMENT
   Authentication, multi-session state, and history
   ========================================================================= */
char users[MAX_USERS][STR_LEN];
char passwords[MAX_USERS][STR_LEN];
uint8_t user_bg_colors[MAX_USERS];
uint8_t user_fg_colors[MAX_USERS];
uint8_t user_flags[MAX_USERS];
uint32_t user_home_clusters[MAX_USERS];
char user_shell_names[MAX_USERS][12][16];

void get_default_shell_name(int session_idx, char *dest) {
    dest[0] = 's';
    dest[1] = 'h';
    if (session_idx + 1 < 10) {
        dest[2] = '0' + (session_idx + 1);
        dest[3] = '\0';
    } else {
        dest[2] = '1';
        dest[3] = '0' + ((session_idx + 1) - 10);
        dest[4] = '\0';
    }
}

void init_user_shell_names(int user_idx) {
    if (user_idx < 0 || user_idx >= MAX_USERS) return;
    for (int s = 0; s < 12; s++) {
        get_default_shell_name(s, user_shell_names[user_idx][s]);
    }
}

int users_count = 0;
int cur_user_index = -1;
bool is_welcome_back_status = false;
uint16_t current_ata_base = 0x1F0;
uint8_t current_ata_sel = 0xA0;
static uint32_t inactivity_timeout = 300;
static uint32_t last_activity_time = 0;

typedef struct {
    int s_state;
    char c_drive;
    bool k_mode;
    bool show_user_flag;
    char k_buffer[KBD_BUF_SIZE];
    int k_index;
    int k_cursor;
    char t_user[STR_LEN];
    char t_pass[STR_LEN];
    char t_extra[STR_LEN];
    int t_scr_mins;
    char history[MAX_HISTORY][KBD_BUF_SIZE];
    int history_count;
    
    uint32_t fs_cluster_stack[16]; 
    char     fs_name_stack[16][32];
    int      fs_depth;             
    char     fs_sub_drive[8];      
    char     shell_name[16];
    bool     show_shell_flag;
} terminal_session_t;

terminal_session_t sessions[12];
int current_session = 0;

#define sys_state      (sessions[current_session].s_state)
#define current_drive  (sessions[current_session].c_drive)
#define kn_mode        (sessions[current_session].k_mode)
#define show_user      (sessions[current_session].show_user_flag)
#define show_shell     (sessions[current_session].show_shell_flag)
#define kbd_buffer     (sessions[current_session].k_buffer)
#define kbd_index      (sessions[current_session].k_index)
#define kbd_cursor     (sessions[current_session].k_cursor)
#define temp_user      (sessions[current_session].t_user)
#define temp_pass      (sessions[current_session].t_pass)
#define temp_extra     (sessions[current_session].t_extra)
#define temp_scr_mins  (sessions[current_session].t_scr_mins)

#define ses            (sessions[current_session])
#define cur_dir_clus   (ses.fs_cluster_stack[ses.fs_depth])


static uint8_t _smol_src[131072];
static uint8_t _smol_dst[131072];
static uint8_t _xep_buf[131072];


static void vga_print_uint(uint32_t v) {
    if (!v) { vga_putchar('0'); return; }
    char buf[12]; int i = 11; buf[i] = '\0';
    while (v) { buf[--i] = '0' + (v % 10); v /= 10; }
    vga_print(buf + i);
}


static void _ft_get_ext(const char *name, char *ext4) {
    for (int i=0; i<4; i++) ext4[i] = ' '; 
    ext4[4] = '\0';
    const char *dot = 0;
    for (int i = 0; name[i]; i++) if (name[i] == '.') dot = name + i + 1;
    if (!dot) return;
    int k = 0;
    while (dot[k] && k < 4) {
        char c = dot[k];
        ext4[k++] = (c >= 'a' && c <= 'z') ? c - 32 : c;
    }
}



static void _ft_print_size(uint32_t sz);
static uint8_t _ft_ext_color(const char *ext4);
static void _print_tree_indent(int depth) {
    for (int i = 0; i < depth; i++) vga_print("    |");
    vga_print("    |-");
}

static uint32_t fat32_get_dir_size(uint32_t cluster, int depth) {
    if (depth > 10 || cluster == 0) return 0;
    uint32_t total = 0;
    uint32_t clus = cluster;
    uint8_t sec_buf[512];
    
    while (clus < 0x0FFFFFF8u) {
        uint32_t lba = _f32_cluster_to_lba(clus);
        for (uint32_t s = 0; s < 8; s++) {
            if (current_fat32_drv && current_fat32_drv->is_ahci) {
                ahci_read_sector(current_fat32_drv->ahci_port, lba + s, sec_buf);
            } else {
                ata_read_sector(current_ata_base, current_ata_sel, lba + s, sec_buf);
            }
            for (int slot = 0; slot < 16; slot++) {
                fat32_entry_t *e = (fat32_entry_t*)(sec_buf + slot * 32);
                if (e->name[0] == 0) return total;
                if ((uint8_t)e->name[0] == 0xE5) continue;
                if (e->attr == FAT32_ATTR_LFN) continue;
                if (e->attr & 0x08) continue; // Volume ID
                
                if (e->name[0] == '.' && e->name[1] == ' ') continue;
                if (e->name[0] == '.' && e->name[1] == '.' && e->name[2] == ' ') continue;
                
                if (e->attr & FAT32_ATTR_DIR) {
                    uint32_t next_clus = ((uint32_t)e->fst_clus_hi << 16) | e->fst_clus_lo;
                    total += fat32_get_dir_size(next_clus, depth + 1);
                    if (current_fat32_drv && current_fat32_drv->is_ahci) ahci_read_sector(current_fat32_drv->ahci_port, lba + s, sec_buf);
                    else ata_read_sector(current_ata_base, current_ata_sel, lba + s, sec_buf);
                } else {
                    total += e->file_size;
                }
            }
        }
        
        _f32_read_sec(fat32_fat_lba + ((clus * 4) / 512));
        clus = *(uint32_t*)(_f32_sec + ((clus * 4) % 512)) & 0x0FFFFFFF;
    }
    return total;
}

static void _fat32_print_tree(uint32_t dir_cluster, int depth) {
    if (depth > 5) return;
    uint32_t clus = dir_cluster;
    
    // We need to count valid entries first to know which is last.
    // Or we just don't care about perfect tree lines and always print |- 
    // Wait, let's just do a simple tree.
    
    uint8_t sec_buf[512];
    fat32_lfn_t lfn_buf[20]; int lfn_count = 0;
    
    while (clus < 0x0FFFFFF8u) {
        uint32_t lba = _f32_cluster_to_lba(clus);
        for (uint32_t s = 0; s < 8; s++) { // FAT32_SECS_CLUS
            if (current_fat32_drv && current_fat32_drv->is_ahci) {
                ahci_read_sector(current_fat32_drv->ahci_port, lba + s, sec_buf);
            } else {
                ata_read_sector(current_ata_base, current_ata_sel, lba + s, sec_buf);
            }
            for (int slot = 0; slot < 16; slot++) {
                fat32_entry_t *e = (fat32_entry_t*)(sec_buf + slot * 32);
                if ((uint8_t)e->name[0] == 0x00) return;
                if ((uint8_t)e->name[0] == 0xE5) { lfn_count = 0; continue; }
                if (e->attr == FAT32_ATTR_LFN) {
                    if (lfn_count < 20) {
                        uint8_t *dest = (uint8_t*)&lfn_buf[lfn_count++];
                        for (int k=0; k<32; k++) dest[k] = ((uint8_t*)e)[k];
                    }
                    continue;
                }
                if (e->name[0] == '.') { lfn_count = 0; continue; }
                if ((uint8_t)e->name[0] < 0x20) { lfn_count = 0; continue; }
                
                char display[280];
                if (lfn_count > 0) {
                    int k = 0;
                    for (int i = lfn_count - 1; i >= 0; i--) {
                        for (int j=0; j<5; j++) { uint16_t c = lfn_buf[i].name1[j]; if (c && c != 0xFFFF) display[k++] = (char)c; }
                        for (int j=0; j<6; j++) { uint16_t c = lfn_buf[i].name2[j]; if (c && c != 0xFFFF) display[k++] = (char)c; }
                        for (int j=0; j<2; j++) { uint16_t c = lfn_buf[i].name3[j]; if (c && c != 0xFFFF) display[k++] = (char)c; }
                    }
                    display[k] = '\0';
                } else {
                    int k = 0;
                    for (int i=0; i<8 && e->name[i] != ' '; i++) display[k++] = e->name[i];
                    if (e->ext[0] != ' ') {
                        display[k++] = '.';
                        for (int i=0; i<3 && e->ext[i] != ' '; i++) display[k++] = e->ext[i];
                    }
                    display[k] = '\0';
                }
                lfn_count = 0;

                // Hide system files requested by user
                if (str_cmp(display, ".TRASH") == 0 || str_starts_with(display, ".TRASH ")) continue;
                if (str_cmp(display, "SYSUSERS.DAT") == 0) continue;
                if (str_cmp(display, "users.dat") == 0) continue;
                
                _print_tree_indent(depth);
                
                if (e->attr & FAT32_ATTR_DIR) {
// vga_set_color(14, 0);
                    uint32_t next_clus = ((uint32_t)e->fst_clus_hi << 16) | e->fst_clus_lo;
                    vga_print("[DIR] "); vga_print(display); vga_print(" "); _ft_print_size(fat32_get_dir_size(next_clus, 0)); vga_print("\n");
// vga_set_color(15, 0);
                    if (next_clus) _fat32_print_tree(next_clus, depth + 1);
                } else {
                    char ext4[5] = {0};
                    _ft_get_ext(display, ext4);
// vga_set_color(_ft_ext_color(ext4), 0);
                    vga_print("[");
                    vga_print(ext4[0] != ' ' ? ext4 : "FILE");
                    vga_print("] ");
                    vga_print(display);
// vga_set_color(15, 0);
                    vga_print(" ");
                    _ft_print_size(e->file_size);
                    vga_print("\n");
                }
                
                lfn_count = 0;
                // re-read sector because recursive call might have clobbered sec_buf
                if (current_fat32_drv && current_fat32_drv->is_ahci) {
                    ahci_read_sector(current_fat32_drv->ahci_port, lba + s, sec_buf);
                } else {
                    ata_read_sector(current_ata_base, current_ata_sel, lba + s, sec_buf);
                }
            }
        }
        // read next cluster from FAT
        // wait, we can't easily read FAT without corrupting _f32_sec if we don't restore it.
        // Actually ata_read_sector uses PIO and doesn't corrupt anything but the destination buffer!
        // But fat32_read_fat uses _f32_sec! 
        clus = fat32_read_fat(clus);
    }
}

static void _ft_print_size(uint32_t sz) {
    if (!sz) { vga_print("0 B"); return; }
    uint32_t val; const char *unit;
    if (sz >= 1048576u)      { val = sz / 1048576u; unit = "MB"; }
    else if (sz >= 1024u)    { val = sz / 1024u;    unit = "KB"; }
    else                     { val = sz;             unit = "B"; }
    vga_print_uint(val); vga_putchar(' '); vga_print(unit);
}


static uint8_t _ft_ext_color(const char *ext4) {
    
    if (ext4[0]=='X' && ext4[1]=='E' && ext4[2]=='P') return 11;
    if (ext4[0]=='K' && ext4[1]=='V' && ext4[2]=='B') return 11;
    if (ext4[0]=='S' && ext4[1]=='M' && ext4[2]=='O') return 13;
    if (ext4[0]=='V' && ext4[1]=='L' && ext4[2]=='I') return 10;
    if (ext4[0]=='T' && ext4[1]=='X' && ext4[2]=='T') return 7;
    return 15;
}


static void _ft_list_cb(const char *name, uint8_t attr, uint32_t sz, uint32_t clus) {
    (void)clus;
    if (str_cmp(name, "SYSUSERS.DAT") == 0 || str_cmp(name, ".TRASH") == 0) return;
    if (attr & FAT32_ATTR_DIR) {
// vga_set_color(14, 0);
        vga_print("  >  [DIR ]  ");
        vga_print(name); vga_putchar('\\');
        
        int col = 13 + f32_str_len(name) + 1;
        while (col < 52) { vga_putchar(' '); col++; }
        vga_print("         -");
        vga_putchar('\n');
    } else {
        char ext4[5]; _ft_get_ext(name, ext4);
        vga_print("  >  [");
        if (ext4[0] != ' ') vga_print(ext4); else vga_print("FILE");
        vga_print("]  ");
        vga_print(name);
        
        int col = 13 + f32_str_len(name);
        while (col < 52) { vga_putchar(' '); col++; }
        _ft_print_size(sz);
        vga_putchar('\n');
    }
// vga_set_color(15, 0);
}


static void fat32_switch_drive(viata_drive_info_t *drv);
void save_system_users() {
    static uint8_t buf[4096];
    if (!fat32_read_sysconf(buf)) {
        f32_mem_set(buf, 0, 4096);
    }
    
    // Preserve Trash cluster at bytes 4-7
    buf[0] = 'V'; buf[1] = 'O'; buf[2] = 'S'; buf[3] = '3';
    buf[8] = users_count;
    
    int offset = 9;
    for (int i=0; i<users_count; i++) {
        f32_mem_cpy(buf+offset, users[i], STR_LEN); offset += STR_LEN;
        f32_mem_cpy(buf+offset, passwords[i], STR_LEN); offset += STR_LEN;
        buf[offset++] = user_bg_colors[i];
        buf[offset++] = user_fg_colors[i];
        buf[offset++] = user_flags[i];
        uint32_t hc = user_home_clusters[i];
        buf[offset++] = (hc >> 24) & 0xFF;
        buf[offset++] = (hc >> 16) & 0xFF;
        buf[offset++] = (hc >> 8) & 0xFF;
        buf[offset++] = hc & 0xFF;
        for (int s = 0; s < 12; s++) {
            const char *sh = user_shell_names[i][s];
            if (sh[0] == '\0') {
                char def_sh[16];
                get_default_shell_name(s, def_sh);
                f32_mem_cpy(buf+offset, def_sh, 16);
            } else {
                f32_mem_cpy(buf+offset, sh, 16);
            }
            offset += 16;
        }
    }
    
    char old_cdrv = current_drive;
    char boot_d = viata_boot_drive_letter();
    viata_drive_info_t *boot_drv = viata_get_drive(boot_d, "");
    fat32_switch_drive(boot_drv);
    
    if (current_ata_base == 0xFFFF) {
        vga_print("Warning: Cannot save settings to a Read-Only or Unsupported boot drive.\n");
        fat32_switch_drive(viata_get_drive(old_cdrv, ses.fs_sub_drive));
        return;
    }

    fat32_write_sysconf(buf);
    fat32_switch_drive(viata_get_drive(old_cdrv, ses.fs_sub_drive));
}

void load_system_users() {
    static uint8_t buf[4096]; f32_mem_set(buf, 0, 4096);
    char old_cdrv = current_drive;
    char boot_d = viata_boot_drive_letter();
    viata_drive_info_t *boot_drv = viata_get_drive(boot_d, "");
    fat32_switch_drive(boot_drv);

    if (current_ata_base == 0xFFFF) {
        fat32_switch_drive(viata_get_drive(old_cdrv, ses.fs_sub_drive));
        return;
    }

    bool success = fat32_read_sysconf(buf);
    if (success && buf[0] == 'V' && buf[1] == 'O' && buf[2] == 'S') {
        bool is_v3 = (buf[3] == '3');
        bool is_v2 = (buf[3] == '2');
        users_count = buf[8];
        if (users_count > MAX_USERS) users_count = MAX_USERS;
        int offset = 9;
        for (int i=0; i<users_count; i++) {
            f32_mem_cpy(users[i], buf+offset, STR_LEN); offset += STR_LEN;
            f32_mem_cpy(passwords[i], buf+offset, STR_LEN); offset += STR_LEN;
            user_bg_colors[i] = buf[offset++];
            user_fg_colors[i] = buf[offset++];
            user_flags[i] = buf[offset++];
            uint32_t hc = 0;
            hc |= ((uint32_t)buf[offset++]) << 24;
            hc |= ((uint32_t)buf[offset++]) << 16;
            hc |= ((uint32_t)buf[offset++]) << 8;
            hc |= ((uint32_t)buf[offset++]);
            user_home_clusters[i] = hc;
            if (is_v3) {
                for (int s = 0; s < 12; s++) {
                    f32_mem_cpy(user_shell_names[i][s], buf+offset, 16); offset += 16;
                    user_shell_names[i][s][15] = '\0';
                    if (user_shell_names[i][s][0] == '\0') {
                        get_default_shell_name(s, user_shell_names[i][s]);
                    }
                }
            } else if (is_v2) {
                char legacy_sh[16];
                f32_mem_cpy(legacy_sh, buf+offset, 16); offset += 16;
                legacy_sh[15] = '\0';
                for (int s = 0; s < 12; s++) {
                    if (s == 0 && legacy_sh[0] != '\0' && str_cmp(legacy_sh, "sh") != 0) {
                        str_cpy(user_shell_names[i][s], legacy_sh);
                    } else {
                        get_default_shell_name(s, user_shell_names[i][s]);
                    }
                }
            } else {
                init_user_shell_names(i);
            }
        }
    } else {
        users_count = 0;
    }
    fat32_switch_drive(viata_get_drive(old_cdrv, ses.fs_sub_drive));
}

bool is_empty_or_spaces(const char *str) {
    if (!str || str[0] == '\0') return true;
    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] != ' ') return false;
    }
    return true;
}

int find_user_index(const char* name) {
    for (int i = 0; i < users_count; i++) {
        if (str_cmp(users[i], name) == 0) return i;
    }
    return -1;
}

static void fat32_switch_drive(viata_drive_info_t *drv) {
    if (!drv) {
        current_ata_base = 0xFFFF;
        return;
    }
    if (drv->hw_type != DRV_HDD && drv->hw_type != DRV_VIRTUAL && drv->hw_type != DRV_USB_FLASH) {
        // Prevent FAT32 initialization on non-ATA drives (CD/Floppy)
        // Set invalid base to prevent accidental reads
        current_ata_base = 0xFFFF;
        return;
    }
    current_ata_base = drv->ata_base;
    current_ata_sel = drv->ata_sel;
    current_fat32_drv = drv;
    fat32_init();
}

void format_banner_line(char* dest, const char* src) {
    int i = 0;
    while (src[i] != '\0' && i < 80) {
        dest[i] = src[i];
        i++;
    }
    while (i < 80) {
        dest[i] = ' ';
        i++;
    }
    dest[80] = '\0';
}

void restore_current_banner() {
    if (cur_user_index >= 0 && cur_user_index < users_count) {
        char raw_buf[100];
        char banner_buf[81];
        
        if (show_user) {
            if (is_welcome_back_status) str_cpy(raw_buf, "Welcome back '");
            else str_cpy(raw_buf, "Welcome '");
            str_cat(raw_buf, users[cur_user_index]);
            str_cat(raw_buf, "' to VIATAhos " OS_VERSION_STRING "!!!");
        } else {
            if (is_welcome_back_status) str_cpy(raw_buf, "Welcome back to VIATAhos " OS_VERSION_STRING "!!!");
            else str_cpy(raw_buf, "Welcome to VIATAhos " OS_VERSION_STRING "!!!");
        }
        
        format_banner_line(banner_buf, raw_buf);

        char line2_raw[100];
        char line2_buf[81];
        if (sessions[current_session].show_shell_flag) {
            str_cpy(line2_raw, "Current Shell Name: '");
            if (cur_user_index >= 0 && cur_user_index < users_count && user_shell_names[cur_user_index][current_session][0] != '\0') {
                str_cat(line2_raw, user_shell_names[cur_user_index][current_session]);
            } else {
                str_cat(line2_raw, sessions[current_session].shell_name);
            }
            str_cat(line2_raw, "'");
            format_banner_line(line2_buf, line2_raw);
            vga_set_banner(banner_buf, line2_buf, "Type 'h' to open the commands' list");
        } else {
            vga_set_banner(banner_buf, "Type 'h' to open the commands' list", "");
        }
    }
}

void add_to_history(const char* cmd) {
    if (cmd[0] == '\0') return;
    int count = sessions[current_session].history_count;
    if (count < MAX_HISTORY) {
        str_cpy(sessions[current_session].history[count], cmd);
        sessions[current_session].history_count++;
    } else {
        for (int i = 0; i < MAX_HISTORY - 1; i++) {
            str_cpy(sessions[current_session].history[i], sessions[current_session].history[i + 1]);
        }
        str_cpy(sessions[current_session].history[MAX_HISTORY - 1], cmd);
    }
}

extern bool shift_pressed;
extern bool is_redirecting;

void print_prompt() {
    if (is_redirecting) return;

    shift_pressed = false;

    vga_putchar(current_drive);
    if (kn_mode || ses.fs_sub_drive[0]) {
        vga_putchar(':');
    }
    if (kn_mode) vga_print("KN");
    if (ses.fs_sub_drive[0]) { 
        if (kn_mode) vga_putchar(':');
        vga_print(ses.fs_sub_drive); 
    }
    vga_putchar('\\');
    
    for (int d = 1; d <= sessions[current_session].fs_depth; d++) {
        vga_print(sessions[current_session].fs_name_stack[d]);
        vga_putchar('\\');
    }
    vga_print("> ");
}

void clear_buffer() {
    for (int i = 0; i < KBD_BUF_SIZE; i++) kbd_buffer[i] = 0;
    kbd_index = 0;
    kbd_cursor = 0;
}

void print_users_list() {
    vga_print("Registered Users:\n");
    for (int i = 0; i < users_count; i++) {
        vga_print(users[i]); vga_print("\n");
    }
    vga_print("Username: ");
}

void global_logout(int state) {
    cur_user_index = -1;
    vga_disable_banner();
    vga_set_color(15, 0);
    vga_recolor_screen();
    for (int i = 0; i < 12; i++) {
        sessions[i].s_state = state;
        sessions[i].k_mode = false;
        sessions[i].show_user_flag = true;
        sessions[i].show_shell_flag = false;
        get_default_shell_name(i, sessions[i].shell_name);
        sessions[i].k_index = 0;
        sessions[i].k_cursor = 0;
        sessions[i].fs_depth = 0;
        sessions[i].fs_cluster_stack[0] = fat32_root();
        for (int j = 0; j < KBD_BUF_SIZE; j++) sessions[i].k_buffer[j] = 0;
        
        int old = current_session;
        current_session = i;
        vga_switch_session(i);
        vga_clear();
        if (state == 4) {
            print_users_list();
        } else if (state == 1) {
            vga_print("Create Admin Account\nUsername: ");
        } else if (state == 7) {
            vga_print("Password: ");
        }
        current_session = old;
    }
    vga_switch_session(current_session);
}

void global_login(int user_index, bool is_welcome_back) {
    cur_user_index = user_index;
    is_welcome_back_status = is_welcome_back;
    
    if (cur_user_index >= 0 && cur_user_index < users_count) {
        vga_set_color(user_fg_colors[cur_user_index], user_bg_colors[cur_user_index]);
    } else {
        vga_set_color(15, 0); // Default White on Black
    }
    vga_recolor_screen();

    uint32_t user_home_clus = fat32_root();
    if (cur_user_index >= 0 && cur_user_index < users_count) {
        if (user_home_clusters[cur_user_index] == 0) {
            uint32_t p = fat32_create_phantom_dir();
            if (p != 0xFFFFFFFF) {
                user_home_clusters[cur_user_index] = p;
                save_system_users();
            }
        }
        if (user_home_clusters[cur_user_index] != 0) {
            user_home_clus = user_home_clusters[cur_user_index];
        }
    }

    for (int i = 0; i < 12; i++) {
        sessions[i].s_state = 0;
        sessions[i].show_user_flag = true;
        if (cur_user_index >= 0 && cur_user_index < users_count) {
            str_cpy(sessions[i].shell_name, user_shell_names[cur_user_index][i]);
        } else {
            get_default_shell_name(i, sessions[i].shell_name);
        }
        sessions[i].k_index = 0;
        sessions[i].k_cursor = 0;
        sessions[i].fs_depth = 0;
        sessions[i].fs_cluster_stack[0] = user_home_clus;
        for (int j = 0; j < KBD_BUF_SIZE; j++) sessions[i].k_buffer[j] = 0;

        int old = current_session;
        current_session = i;
        vga_switch_session(i);
        restore_current_banner();
        vga_clear();
        print_prompt();
        current_session = old;
    }
    vga_switch_session(current_session);
    restore_current_banner();

    /* Flush PS/2 keyboard buffer: discard any spurious keys that arrived during login */
    for (int _fi = 0; _fi < 64; _fi++) {
        if (inb(0x64) & 1) inb(0x60); else break;
    }
    /* Also reset the current session's software buffer */
    for (int i = 0; i < KBD_BUF_SIZE; i++) sessions[current_session].k_buffer[i] = 0;
    sessions[current_session].k_index = 0;
    sessions[current_session].k_cursor = 0;
}

void delete_user_by_index(int idx) {
    if (idx < 0 || idx >= users_count) return;

    bool deleting_current = (idx == cur_user_index);

    for (int i = idx; i < users_count - 1; i++) {
        str_cpy(users[i], users[i + 1]);
        str_cpy(passwords[i], passwords[i + 1]);
        user_bg_colors[i] = user_bg_colors[i + 1];
        user_fg_colors[i] = user_fg_colors[i + 1];
        user_flags[i] = user_flags[i + 1];
        user_home_clusters[i] = user_home_clusters[i + 1];
        for (int s = 0; s < 12; s++) {
            str_cpy(user_shell_names[i][s], user_shell_names[i + 1][s]);
        }
    }
    init_user_shell_names(users_count - 1);
    users_count--;

    if (deleting_current) {
        global_logout(4);
    } else {
        if (cur_user_index > idx) {
            cur_user_index--;
        }
    }
    save_system_users();
}

#include "commands.inc"




#define KEY_UP    0x100
#define KEY_DOWN  0x101
#define KEY_LEFT  0x102
#define KEY_RIGHT 0x103
#define KEY_F1    0x201
#define KEY_F2    0x202
#define KEY_F3    0x203
#define KEY_F4    0x204
#define KEY_F5    0x205
#define KEY_F6    0x206
#define KEY_F7    0x207
#define KEY_F8    0x208
#define KEY_F9    0x209
#define KEY_F10   0x20A
#define KEY_F11   0x20B
#define KEY_F12   0x20C
#define KEY_ESC   0x104

bool shift_pressed = false;
bool caps_lock = false;
bool extended_key = false;

uint16_t read_kbd() {
    if (inb(0x64) & 1) {
        uint8_t scancode = inb(0x60);
        
        if (scancode == 0xE0) {
            extended_key = true;
            return 0;
        }

        if (scancode & 0x80) {
            uint8_t released = scancode & 0x7F;
            if (released == 0x2A || released == 0x36) {
                shift_pressed = false;
            }
            extended_key = false;
            return 0;
        } 
        else {
            extended_key = false;

            switch (scancode) {
                case 0x48: return KEY_UP;
                case 0x50: return KEY_DOWN;
                case 0x4B: return KEY_LEFT;
                case 0x4D: return KEY_RIGHT;
                case 0x01: return KEY_ESC;
            }

            if (scancode == 0x2A || scancode == 0x36) {
                shift_pressed = true;
                return 0;
            }
            if (scancode == 0x3A) {
                caps_lock = !caps_lock;
                return 0;
            }
            
            if (scancode >= 0x3B && scancode <= 0x44) return KEY_F1 + (scancode - 0x3B);
            if (scancode == 0x57) return KEY_F11;
            if (scancode == 0x58) return KEY_F12;
            
            char scan_map[128] = {
                0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
                '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
                0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
                0, '\\','z','x','c','v','b','n','m',',','.','/', 0,
                '*', 0, ' '
            };
            
            char scan_map_shift[128] = {
                0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
                '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
                0, 'A','S','D','F','G','H','J','K','L',':','"','~',
                0, '|','Z','X','C','V','B','N','M','<','>','?', 0,
                '*', 0, ' '
            };
            
            bool is_alpha = (scan_map[scancode] >= 'a' && scan_map[scancode] <= 'z');
            bool upper = shift_pressed;
            if (is_alpha && caps_lock) upper = !upper;
            
            if (upper) {
                return scan_map_shift[scancode];
            } else {
                return scan_map[scancode];
            }
        }
    }
    return 0;
}

/* =========================================================================
   KERNEL ENTRY POINT
   Initialization loop and system loop
   ========================================================================= */

#ifdef __x86_64__
struct interrupt_frame { uint64_t ip; uint64_t cs; uint64_t flags; uint64_t sp; uint64_t ss; };
__attribute__((interrupt)) void isr_div_by_zero(struct interrupt_frame *frame) { PANIC("CPU Crash: Divide by Zero (#DE)"); }
__attribute__((interrupt)) void isr_overflow(struct interrupt_frame *frame) { PANIC("CPU Crash: Overflow (#OF)"); }
__attribute__((interrupt)) void isr_invalid_op(struct interrupt_frame *frame) { PANIC("CPU Crash: Invalid Opcode (#UD)"); }
__attribute__((interrupt)) void isr_double_fault(struct interrupt_frame *frame, uint64_t err) { PANIC("CPU Crash: Double Fault (#DF)"); }
__attribute__((interrupt)) void isr_gpf(struct interrupt_frame *frame, uint64_t err) { PANIC("CPU Crash: Gen Protect Fault (#GP)"); }
__attribute__((interrupt)) void isr_page_fault(struct interrupt_frame *frame, uint64_t err) { PANIC("CPU Crash: Page Fault (#PF)"); }
__attribute__((interrupt)) void isr_generic(struct interrupt_frame *frame) { PANIC("CPU Crash: Unhandled Exception"); }
__attribute__((interrupt)) void isr_debug(struct interrupt_frame *frame) { PANIC("CPU Crash: Debug (#DB)"); }
__attribute__((interrupt)) void isr_nmi(struct interrupt_frame *frame) { PANIC("CPU Crash: NMI Interrupt"); }
__attribute__((interrupt)) void isr_breakpoint(struct interrupt_frame *frame) { PANIC("CPU Crash: Breakpoint (#BP)"); }
__attribute__((interrupt)) void isr_bound_range(struct interrupt_frame *frame) { PANIC("CPU Crash: Bound Range (#BR)"); }
__attribute__((interrupt)) void isr_device_not_avail(struct interrupt_frame *frame) { PANIC("CPU Crash: Device Not Available (#NM)"); }
__attribute__((interrupt)) void isr_coproc_segment(struct interrupt_frame *frame) { PANIC("CPU Crash: Coprocessor Segment Overrun"); }
__attribute__((interrupt)) void isr_invalid_tss(struct interrupt_frame *frame, uint64_t err) { PANIC("CPU Crash: Invalid TSS (#TS)"); }
__attribute__((interrupt)) void isr_segment_not_present(struct interrupt_frame *frame, uint64_t err) { PANIC("CPU Crash: Segment Not Present (#NP)"); }
__attribute__((interrupt)) void isr_stack_segment(struct interrupt_frame *frame, uint64_t err) { PANIC("CPU Crash: Stack Segment Fault (#SS)"); }
__attribute__((interrupt)) void isr_x87_fp(struct interrupt_frame *frame) { PANIC("CPU Crash: x87 Floating-Point (#MF)"); }
__attribute__((interrupt)) void isr_alignment(struct interrupt_frame *frame, uint64_t err) { PANIC("CPU Crash: Alignment Check (#AC)"); }
__attribute__((interrupt)) void isr_machine_check(struct interrupt_frame *frame) { PANIC("CPU Crash: Machine Check (#MC)"); }
__attribute__((interrupt)) void isr_simd_fp(struct interrupt_frame *frame) { PANIC("CPU Crash: SIMD Floating-Point (#XM)"); }
struct idt_entry { uint16_t isr_low; uint16_t kernel_cs; uint8_t ist; uint8_t attributes; uint16_t isr_mid; uint32_t isr_high; uint32_t reserved; } __attribute__((packed));
struct idtr { uint16_t limit; uint64_t base; } __attribute__((packed));
static struct idt_entry sys_idt[256];
static struct idtr sys_idtr;
static void idt_set_gate(int num, void *isr, uint8_t flags) {
    uint64_t addr = (uint64_t)isr;
    sys_idt[num].isr_low = addr & 0xFFFF; sys_idt[num].kernel_cs = 0x08; sys_idt[num].ist = 0; sys_idt[num].attributes = flags;
    sys_idt[num].isr_mid = (addr >> 16) & 0xFFFF; sys_idt[num].isr_high = (addr >> 32) & 0xFFFFFFFF; sys_idt[num].reserved = 0;
}
#else
struct interrupt_frame { uint32_t ip; uint32_t cs; uint32_t flags; uint32_t sp; uint32_t ss; };
__attribute__((interrupt)) void isr_div_by_zero(struct interrupt_frame *frame) { (void)frame; PANIC("CPU Crash: Divide by Zero (#DE)"); }
__attribute__((interrupt)) void isr_overflow(struct interrupt_frame *frame) { (void)frame; PANIC("CPU Crash: Overflow (#OF)"); }
__attribute__((interrupt)) void isr_invalid_op(struct interrupt_frame *frame) { (void)frame; PANIC("CPU Crash: Invalid Opcode (#UD)"); }
__attribute__((interrupt)) void isr_double_fault(struct interrupt_frame *frame, uint32_t err) { (void)frame; (void)err; PANIC("CPU Crash: Double Fault (#DF)"); }
__attribute__((interrupt)) void isr_gpf(struct interrupt_frame *frame, uint32_t err) { (void)frame; (void)err; PANIC("CPU Crash: Gen Protect Fault (#GP)"); }
__attribute__((interrupt)) void isr_page_fault(struct interrupt_frame *frame, uint32_t err) { (void)frame; (void)err; PANIC("CPU Crash: Page Fault (#PF)"); }
__attribute__((interrupt)) void isr_generic(struct interrupt_frame *frame) { (void)frame; PANIC("CPU Crash: Unhandled Exception"); }
__attribute__((interrupt)) void isr_debug(struct interrupt_frame *frame) { (void)frame; PANIC("CPU Crash: Debug (#DB)"); }
__attribute__((interrupt)) void isr_nmi(struct interrupt_frame *frame) { (void)frame; PANIC("CPU Crash: NMI Interrupt"); }
__attribute__((interrupt)) void isr_breakpoint(struct interrupt_frame *frame) { (void)frame; PANIC("CPU Crash: Breakpoint (#BP)"); }
__attribute__((interrupt)) void isr_bound_range(struct interrupt_frame *frame) { (void)frame; PANIC("CPU Crash: Bound Range (#BR)"); }
__attribute__((interrupt)) void isr_device_not_avail(struct interrupt_frame *frame) { (void)frame; PANIC("CPU Crash: Device Not Available (#NM)"); }
__attribute__((interrupt)) void isr_coproc_segment(struct interrupt_frame *frame) { (void)frame; PANIC("CPU Crash: Coprocessor Segment Overrun"); }
__attribute__((interrupt)) void isr_invalid_tss(struct interrupt_frame *frame, uint32_t err) { (void)frame; (void)err; PANIC("CPU Crash: Invalid TSS (#TS)"); }
__attribute__((interrupt)) void isr_segment_not_present(struct interrupt_frame *frame, uint32_t err) { (void)frame; (void)err; PANIC("CPU Crash: Segment Not Present (#NP)"); }
__attribute__((interrupt)) void isr_stack_segment(struct interrupt_frame *frame, uint32_t err) { (void)frame; (void)err; PANIC("CPU Crash: Stack Segment Fault (#SS)"); }
__attribute__((interrupt)) void isr_x87_fp(struct interrupt_frame *frame) { (void)frame; PANIC("CPU Crash: x87 Floating-Point (#MF)"); }
__attribute__((interrupt)) void isr_alignment(struct interrupt_frame *frame, uint32_t err) { (void)frame; (void)err; PANIC("CPU Crash: Alignment Check (#AC)"); }
__attribute__((interrupt)) void isr_machine_check(struct interrupt_frame *frame) { (void)frame; PANIC("CPU Crash: Machine Check (#MC)"); }
__attribute__((interrupt)) void isr_simd_fp(struct interrupt_frame *frame) { (void)frame; PANIC("CPU Crash: SIMD Floating-Point (#XM)"); }
struct idt_entry { uint16_t isr_low; uint16_t kernel_cs; uint8_t reserved; uint8_t attributes; uint16_t isr_high; } __attribute__((packed));
struct idtr { uint16_t limit; uint32_t base; } __attribute__((packed));
static struct idt_entry sys_idt[256];
static struct idtr sys_idtr;
static void idt_set_gate(int num, void *isr, uint8_t flags) {
    uint32_t addr = (uint32_t)isr;
    sys_idt[num].isr_low = addr & 0xFFFF; sys_idt[num].kernel_cs = 0x08; sys_idt[num].reserved = 0;
    sys_idt[num].attributes = flags; sys_idt[num].isr_high = (addr >> 16) & 0xFFFF;
}
#endif


static void init_idt(void) {
    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, (void*)isr_generic, 0x8E); // 64-bit Interrupt Gate
    }
    
    idt_set_gate(0, (void*)isr_div_by_zero, 0x8E);
    idt_set_gate(1, (void*)isr_debug, 0x8E);
    idt_set_gate(2, (void*)isr_nmi, 0x8E);
    idt_set_gate(3, (void*)isr_breakpoint, 0x8E);
    idt_set_gate(4, (void*)isr_overflow, 0x8E);
    idt_set_gate(5, (void*)isr_bound_range, 0x8E);
    idt_set_gate(6, (void*)isr_invalid_op, 0x8E);
    idt_set_gate(7, (void*)isr_device_not_avail, 0x8E);
    idt_set_gate(8, (void*)isr_double_fault, 0x8E);
    idt_set_gate(9, (void*)isr_coproc_segment, 0x8E);
    idt_set_gate(10, (void*)isr_invalid_tss, 0x8E);
    idt_set_gate(11, (void*)isr_segment_not_present, 0x8E);
    idt_set_gate(12, (void*)isr_stack_segment, 0x8E);
    idt_set_gate(13, (void*)isr_gpf, 0x8E);
    idt_set_gate(14, (void*)isr_page_fault, 0x8E);
    idt_set_gate(16, (void*)isr_x87_fp, 0x8E);
    idt_set_gate(17, (void*)isr_alignment, 0x8E);
    idt_set_gate(18, (void*)isr_machine_check, 0x8E);
    idt_set_gate(19, (void*)isr_simd_fp, 0x8E);
    
    // IRQ0 (Timer)
    
    
    sys_idtr.limit = sizeof(sys_idt) - 1;
    #ifdef __x86_64__
    sys_idtr.base = (uint64_t)&sys_idt;
#else
    sys_idtr.base = (uint32_t)&sys_idt;
#endif
    
    __asm__ volatile("lidt %0" : : "m"(sys_idtr));
}

extern uint8_t bss_start[];
extern uint8_t bss_end[];

void kernel_main(void) {
    uint8_t *p = bss_start;
    while (p < bss_end) {
        *p++ = 0;
    }
    
    rpt_magic = 0;
    rpt_password_given = false;
    rpt_count_remaining = 0;

    init_idt();

    vga_print("KERNEL STARTING\n");
    vga_set_color(15, 0);
    vga_init();
    init_boot_time();
    init_acpi();
    last_activity_time = get_uptime_seconds();

    
    detect_drives();
    char boot_drive = viata_boot_drive_letter();
    fat32_switch_drive(viata_get_drive(boot_drive, ""));


    for (int i = 0; i < MAX_USERS; i++) {
        init_user_shell_names(i);
    }

    load_system_users();
    if (users_count > 0) {
        // If we loaded users, skip the create admin screen for all sessions!
        // Wait, sys_state is set to 1 below. We can override it after the loop!
    }

    for (int i = 0; i < 12; i++) {
        sessions[i].s_state = (users_count > 0) ? 4 : 1;
        sessions[i].c_drive = boot_drive;
        sessions[i].k_mode = false;
        sessions[i].show_user_flag = true;
        sessions[i].show_shell_flag = false;
        get_default_shell_name(i, sessions[i].shell_name);
        sessions[i].k_index = 0;
        sessions[i].k_cursor = 0;
        sessions[i].history_count = 0;
        sessions[i].fs_depth = 0;
        sessions[i].fs_sub_drive[0] = '\0';
        for (int j = 0; j < KBD_BUF_SIZE; j++) sessions[i].k_buffer[j] = 0;
    }

    
    
    
    for (int i = 0; i < 12; i++)
        sessions[i].fs_cluster_stack[0] = fat32_root();

    current_session = 0;
// vga_set_color(15, 0);
    if (users_count > 0) {
        vga_print("Registered Users:\n");
        for (int i = 0; i < users_count; i++) {
            vga_print(users[i]); vga_print("\n");
        }
        vga_print("Username: ");
    } else {
        vga_print("Create Admin Account\nUsername: ");
    }
    /* Flush any leftover keypresses from BIOS/bootloader (e.g. the Enter used to boot) */
    for (int _i = 0; _i < 32; _i++) {
        if (inb(0x64) & 1) inb(0x60); else break;
    }

    while (1) {
        check_and_run_rpt();
        uint16_t key = read_kbd();
        static uint32_t loop_counter = 0;
		static uint32_t current_time = 0;
		if (current_time == 0) current_time = get_uptime_seconds();
		if (++loop_counter >= 1000000) {
		    current_time = get_uptime_seconds();
		    loop_counter = 0;
		}

        if (key != 0) {			last_activity_time = current_time;
			
            if (sys_state == 30) {
                if (key == KEY_ESC || key == 0x104) {
                    int bpos = 0;
                    for (int i=0; i<ed_line_count; i++) {
                        for (int j=0; j<80 && ed_lines[i][j]; j++) {
                            if (bpos < (int)sizeof(editor_buf)-2) editor_buf[bpos++] = ed_lines[i][j];
                        }
                        if (i < ed_line_count - 1 && bpos < (int)sizeof(editor_buf)-2) editor_buf[bpos++] = '\n';
                    }
                    editor_buf[bpos] = '\0';
                    int fnl = str_len(editor_fname);
                    bool is_smol = (fnl > 5 && str_cmp(editor_fname + fnl - 5, ".smol") == 0);
                    fat32_delete(cur_dir_clus, editor_fname);
                    if (is_smol) {
                        char oext[16] = {0};
                        int dot = -1;
                        for (int k = fnl - 6; k >= 0; k--) {
                            if (editor_fname[k] == '.') { dot = k; break; }
                        }
                        if (dot >= 0) {
                            int oi = 0;
                            for (int k = dot + 1; k < fnl - 5 && oi < 13; k++) oext[oi++] = editor_fname[k];
                            oext[oi] = '\0';
                        }
                        uint32_t csz = smol_compress((const uint8_t*)editor_buf, bpos, _smol_dst, sizeof(_smol_dst), oext);
                        if (csz > 0) {
                            fat32_mkfile(cur_dir_clus, editor_fname, _smol_dst, csz);
                        } else {
                            fat32_mkfile(cur_dir_clus, editor_fname, (uint8_t*)editor_buf, bpos);
                        }
                    } else {
                        fat32_mkfile(cur_dir_clus, editor_fname, (uint8_t*)editor_buf, bpos);
                    }
                    ed_saved = true;

                    if (shift_pressed) {
                        sys_state = 0;
                        clear_buffer();
                        vga_recolor_screen(); // restore colors
                        vga_clear();
                        print_prompt();
                        continue;
                    }
                }
                else if (key == KEY_UP && ed_cy > 0) {
                    ed_cy--;
                    if (ed_cy < ed_sy) ed_sy = ed_cy;
                    int len = 0; while (len < 79 && ed_lines[ed_cy][len]) len++;
                    if (ed_cx > len) ed_cx = len;
                }
                else if (key == KEY_DOWN && ed_cy < ed_line_count - 1) {
                    ed_cy++;
                    if (ed_cy > ed_sy + 20) ed_sy = ed_cy - 20;
                    int len = 0; while (len < 79 && ed_lines[ed_cy][len]) len++;
                    if (ed_cx > len) ed_cx = len;
                }
                else if (key == KEY_LEFT && ed_cx > 0) {
                    ed_cx--;
                }
                else if (key == KEY_RIGHT && ed_cx < 79 && ed_lines[ed_cy][ed_cx]) {
                    ed_cx++;
                }
                else if (key == '\b') {
                    if (ed_cx > 0) {
                        ed_saved = false;
                        for (int i=ed_cx-1; i<79; i++) ed_lines[ed_cy][i] = ed_lines[ed_cy][i+1];
                        ed_cx--;
                    } else if (ed_cy > 0) {
                        ed_saved = false;
                        int prev_len = 0;
                        while (prev_len < 79 && ed_lines[ed_cy-1][prev_len]) prev_len++;
                        int cur_len = 0;
                        while (cur_len < 79 && ed_lines[ed_cy][cur_len]) cur_len++;
                        
                        int new_cx = prev_len;
                        for (int i=0; i < cur_len && (prev_len + i) < 79; i++) {
                            ed_lines[ed_cy-1][prev_len + i] = ed_lines[ed_cy][i];
                        }
                        
                        for (int i=ed_cy; i < ed_line_count - 1; i++) {
                            for (int j=0; j<80; j++) ed_lines[i][j] = ed_lines[i+1][j];
                        }
                        for (int j=0; j<80; j++) ed_lines[ed_line_count - 1][j] = '\0';
                        
                        ed_line_count--;
                        ed_cy--;
                        ed_cx = new_cx;
                        if (ed_cy < ed_sy) ed_sy = ed_cy;
                    }
                }
                else if (key == '\n' || key == '\r') {
                    if (ed_line_count < 1024) {
                        ed_saved = false;
                        for (int i=ed_line_count; i>ed_cy+1; i--) {
                            for (int j=0; j<80; j++) ed_lines[i][j] = ed_lines[i-1][j];
                        }
                        for (int j=0; j<80; j++) ed_lines[ed_cy+1][j] = '\0';
                        // move rest of line down
                        int dst = 0;
                        for (int src=ed_cx; src<80 && ed_lines[ed_cy][src]; src++) {
                            ed_lines[ed_cy+1][dst++] = ed_lines[ed_cy][src];
                            ed_lines[ed_cy][src] = '\0';
                        }
                        ed_line_count++;
                        ed_cy++;
                        ed_cx = 0;
                        if (ed_cy > ed_sy + 20) ed_sy = ed_cy - 20;
                    }
                }
                           else if (key == '\t' && ed_cx < 75) {
                    ed_saved = false;
                    for (int i=78; i>ed_cx+3; i--) ed_lines[ed_cy][i] = ed_lines[ed_cy][i-4];
                    for (int i=0; i<4; i++) {
                        ed_lines[ed_cy][ed_cx] = ' ';
                        ed_cx++;
                    }
                }
                else if (key >= 32 && key <= 126 && ed_cx < 79) {
                    ed_saved = false;
                    for (int i=78; i>ed_cx; i--) ed_lines[ed_cy][i] = ed_lines[ed_cy][i-1];
                    ed_lines[ed_cy][ed_cx] = (char)key;
                    ed_cx++;
                }
                ed_draw();
                continue;
            }
			
            if (sys_state == 21) {
                vga_screen_on();
                suspend_screen_active = true;
                suspend_timer_start = get_uptime_seconds();
            }

            if (key >= KEY_F1 && key <= KEY_F12) {
                int new_session = key - KEY_F1;
                if (new_session != current_session) {
                    current_session = new_session;
                    vga_switch_session(current_session);
                    restore_current_banner();
                    
                    if (sys_state == 1 && users_count > 0) {
                        sys_state = 4;
                        vga_clear();
                        vga_print("Registered Users:\n");
                        for (int i = 0; i < users_count; i++) {
                            vga_print(users[i]); vga_print("\n");
                        }
                        vga_print("Username: ");
                    }
                }
            }
            else if (key == '\n' || key == '\r') {
                /* Prevent empty Enters (bounces) from resetting password/login prompts */
                bool allow_empty = (sys_state == 0 || sys_state == 21 || sys_state == 27 || 
                                    sys_state == 30 || sys_state == 32 || sys_state == 33);
                if (kbd_index > 0 || allow_empty) {
                    handle_enter();
                }
            }
            else if (key == '\b') { 
                if (kbd_cursor > 0) {
                    kbd_cursor--;
                    kbd_index--;
                    for (int i = kbd_cursor; i < kbd_index; i++) {
                        kbd_buffer[i] = kbd_buffer[i+1];
                    }
                    
                    vga_putchar('\b');
                    
                    for (int i = kbd_cursor; i < kbd_index; i++) {
                        bool is_pass = (sys_state == 2 || sys_state == 3 || sys_state == 5 || 
                                        sys_state == 6 || sys_state == 7 || sys_state == 8 || 
                                        sys_state == 10 || sys_state == 12 || sys_state == 13 || 
                                        sys_state == 15 || sys_state == 17 || sys_state == 18 ||
                                        sys_state == 19 || sys_state == 20 || sys_state == 21 ||
                                        sys_state == 22 || sys_state == 24 || sys_state == 25 || sys_state == 26 || sys_state == 27 ||
                                        sys_state == 80 || sys_state == 81 || sys_state == 84 || sys_state == 85);
                        char print_char = is_pass ? '*' : kbd_buffer[i];
                        vga_putchar(print_char);
                    }
                    vga_putchar(' ');
                    
                    for (int i = kbd_index + 1; i > kbd_cursor; i--) {
                        vga_putchar('\b');
                    }
                }
            }
            else if (key == KEY_LEFT) {
                if (kbd_cursor > 0) {
                    kbd_cursor--;
                    vga_putchar('\b');
                }
            }
            else if (key == KEY_RIGHT) {
                if (kbd_cursor < kbd_index) {
                    bool is_pass = (sys_state == 2 || sys_state == 3 || sys_state == 5 || 
                                    sys_state == 6 || sys_state == 7 || sys_state == 8 || 
                                    sys_state == 10 || sys_state == 12 || sys_state == 13 || 
                                    sys_state == 15 || sys_state == 17 || sys_state == 18 || 
                                    sys_state == 22 || sys_state == 24 || sys_state == 25 || sys_state == 26 || sys_state == 27 ||
                                    sys_state == 80 || sys_state == 81 || sys_state == 84 || sys_state == 85);
                    vga_putchar(is_pass ? '*' : kbd_buffer[kbd_cursor]);
                    kbd_cursor++;
                }
            }
            else if (key == KEY_UP) {
                vga_scroll_view(1);
            }
            else if (key == KEY_DOWN) {
                vga_scroll_view(-1);
            }
            else if (key >= 32 && key <= 126 && kbd_index < KBD_BUF_SIZE - 1) {
                int limit = 20;
                if (sys_state == 0) {
                    limit = 60;
                } else if (sys_state == 1 || sys_state == 4 || sys_state == 9 || 
                           sys_state == 14 || sys_state == 16) {
                    limit = 12;
                }

                if (kbd_index < limit) {
                    for (int i = kbd_index; i > kbd_cursor; i--) {
                        kbd_buffer[i] = kbd_buffer[i-1];
                    }
                    kbd_buffer[kbd_cursor] = key;
                    kbd_index++;
                    
                    for (int i = kbd_cursor; i < kbd_index; i++) {
                        bool is_pass = (sys_state == 2 || sys_state == 3 || sys_state == 5 || 
                                        sys_state == 6 || sys_state == 7 || sys_state == 8 || 
                                        sys_state == 10 || sys_state == 12 || sys_state == 13 || 
                                        sys_state == 15 || sys_state == 17 || sys_state == 18 ||
                                        sys_state == 19 || sys_state == 20 || sys_state == 21 ||
                                        sys_state == 22 || sys_state == 24 || sys_state == 25 || sys_state == 26 || sys_state == 27 ||
                                        sys_state == 80 || sys_state == 81 || sys_state == 84 || sys_state == 85);
                        char print_char = is_pass ? '*' : kbd_buffer[i];
                        vga_putchar(print_char);
                    }
                    kbd_cursor++;
                    
                    for (int i = kbd_index; i > kbd_cursor; i--) {
                        vga_putchar('\b');
                    }
                }
            }
        }
		
		if (sys_state == 0 && inactivity_timeout > 0) {
		    uint32_t now = current_time;
		    uint32_t idle_elapsed = 0;
		    if (now >= last_activity_time) {
		        idle_elapsed = now - last_activity_time;
		    } else {
		        idle_elapsed = 0;
		        last_activity_time = now;
		    }

		    if (idle_elapsed >= inactivity_timeout) {
		        sys_state = 21;
		        vga_disable_banner();
		        vga_clear();
		        vga_print("=== SYSTEM SUSPENDED ===\nEnter password to wake up: ");
		        vga_screen_off();
		        suspend_screen_active = false;
		    }
		}		

        if (sys_state == 21 && suspend_screen_active) {
            uint32_t now = current_time;
            uint32_t elapsed = 0;
            if (now >= suspend_timer_start) {
                elapsed = now - suspend_timer_start;
            } else {
                elapsed = 0;
                suspend_timer_start = now;
            }

            if (elapsed >= 10) {
                vga_screen_off();
                suspend_screen_active = false;
            }
        }
    }
}