extern void os_yield(void);



static bool my_mem_cmp(const void *s1, const void *s2, int n) {
    const uint8_t *p1 = (const uint8_t*)s1;
    const uint8_t *p2 = (const uint8_t*)s2;
    for (int i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return false;
    }
    return true;
}
extern uint16_t current_ata_base;
extern uint8_t current_ata_sel;

#ifndef FAT32_H
#define FAT32_H
#include <stdint.h>
#include <stdbool.h>
#include "ata.h"

/* =========================================================================
   FAT32 Driver — VIATAhos
   Partition starts at FAT32_PART_LBA (LBA 2048 on the boot disk).
   Kernel calls fat32_init() at startup; if no valid FAT32 found, it formats.
   ========================================================================= */

#define FAT32_PART_LBA   2048u
#define FAT32_PART_SECS  129024u   
#define FAT32_RSVD       32u
#define FAT32_SECS_CLUS  8u        
#define FAT32_NUM_FATS   2u
#define FAT32_FAT_SECS   128u      
#define FAT32_DATA_OFF   (FAT32_RSVD + FAT32_NUM_FATS * FAT32_FAT_SECS)  
#define FAT32_ROOT_CLUS  2u
#define FAT32_EOC        0x0FFFFFF8u
#define FAT32_FREE       0x00000000u

/* --- BPB / Volume Boot Record --- */
typedef struct {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sec;
    uint8_t  sec_per_clus;
    uint16_t rsvd_sec_cnt;
    uint8_t  num_fats;
    uint16_t root_ent_cnt;
    uint16_t tot_sec16;
    uint8_t  media;
    uint16_t fat_sz16;
    uint16_t sec_per_trk;
    uint16_t num_heads;
    uint32_t hidd_sec;
    uint32_t tot_sec32;
    uint32_t fat_sz32;
    uint16_t ext_flags;
    uint16_t fs_ver;
    uint32_t root_clus;
    uint16_t fs_info;
    uint16_t bk_boot_sec;
    uint8_t  reserved[12];
    uint8_t  drv_num;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t vol_id;
    char     vol_lab[11];
    char     fil_sys_type[8];
} __attribute__((packed)) fat32_bpb_t;

/* --- 32-byte directory entry --- */
typedef struct {
    char     name[8];
    char     ext[3];
    uint8_t  attr;
    uint8_t  nt_res;
    uint8_t  crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t fst_clus_hi;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t fst_clus_lo;
    uint32_t file_size;
} __attribute__((packed)) fat32_entry_t;

/* --- LFN directory entry --- */
typedef struct {
    uint8_t  seq;
    uint16_t name1[5];
    uint8_t  attr;      
    uint8_t  type;
    uint8_t  checksum;
    uint16_t name2[6];
    uint16_t clus_lo;
    uint16_t name3[2];
} __attribute__((packed)) fat32_lfn_t;

#define FAT32_ATTR_RO   0x01
#define FAT32_ATTR_DIR  0x10
#define FAT32_ATTR_ARC  0x20
#define FAT32_ATTR_LFN  0x0F

/* =========================================================================
   Global state (one mounted volume)
   ========================================================================= */
static bool     fat32_ready        = false;
static uint32_t fat32_fat_lba      = 0;
static uint32_t fat32_data_lba     = 0;
static uint32_t fat32_root_cluster = FAT32_ROOT_CLUS;
static uint8_t  _f32_sec[512];
static uint32_t fat32_root(void);
static bool fat32_read_sysconf(void *buf);
static bool fat32_write_sysconf(const void *buf);
static uint32_t fat32_create_phantom_dir(void);

/* =========================================================================
   Low-level helpers
   ========================================================================= */
static inline void f32_str_cpy(char *d, const char *s) {
    while ((*d++ = *s++));
}
static inline int f32_str_len(const char *s) {
    int l = 0; while (s[l]) l++; return l;
}
static inline int f32_str_cmp(const char *a, const char *b) {
    while (*a && (*a == *b)) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static inline void f32_mem_set(void *p, uint8_t v, uint32_t n) {
    uint8_t *b = (uint8_t*)p; for (uint32_t i=0;i<n;i++) b[i]=v;
}
static inline void f32_mem_cpy(void *d, const void *s, uint32_t n) {
    uint8_t *dd=(uint8_t*)d; const uint8_t *ss=(const uint8_t*)s;
    for (uint32_t i=0;i<n;i++) dd[i]=ss[i];
}

void _f32_read_sec(uint32_t lba) {
    if (current_fat32_drv && current_fat32_drv->is_ahci) {
        ahci_read_sector(current_fat32_drv->ahci_port, lba, _f32_sec);
    } else {
        ata_read_sector(current_ata_base, current_ata_sel, lba, _f32_sec);
    }
}
static void f32_write_sec(uint32_t lba) {
    if (current_fat32_drv && current_fat32_drv->is_ahci) {
        ahci_write_sector(current_fat32_drv->ahci_port, lba, _f32_sec);
    } else {
        ata_write_sector(current_ata_base, current_ata_sel, lba, _f32_sec);
    }
}
uint32_t _f32_cluster_to_lba(uint32_t clus) {
    return fat32_data_lba + (clus - 2u) * FAT32_SECS_CLUS;
}

/* =========================================================================
   FAT table operations
   ========================================================================= */
static uint32_t fat32_read_fat(uint32_t cluster) {
    if (!fat32_ready) return 0xFFFFFFFFu;
    uint32_t fat_byte = cluster * 4u;
    uint32_t sec_off  = fat_byte / 512u;
    uint32_t byt_off  = fat_byte % 512u;
    _f32_read_sec(fat32_fat_lba + sec_off);
    uint32_t val;
    f32_mem_cpy(&val, _f32_sec + byt_off, 4);
    return val & 0x0FFFFFFFu;
}

uint32_t fat32_total_clusters = 0;

static void fat32_write_fat(uint32_t cluster, uint32_t value) {
    if (!fat32_ready) return;
    uint32_t fat_byte = cluster * 4u;
    uint32_t sec_off  = fat_byte / 512u;
    uint32_t byt_off  = fat_byte % 512u;
    value &= 0x0FFFFFFFu;

    
    for (uint32_t f = 0; f < FAT32_NUM_FATS; f++) {
        _f32_read_sec(fat32_fat_lba + f * FAT32_FAT_SECS + sec_off);
        uint32_t existing;
        f32_mem_cpy(&existing, _f32_sec + byt_off, 4);
        existing = (existing & 0xF0000000u) | value;
        f32_mem_cpy(_f32_sec + byt_off, &existing, 4);
        f32_write_sec(fat32_fat_lba + f * FAT32_FAT_SECS + sec_off);
    }
}

static uint32_t fat32_get_total_kb(void) {
    if (!fat32_ready) return 0;
    _f32_read_sec(FAT32_PART_LBA);
    fat32_bpb_t *bpb = (fat32_bpb_t*)_f32_sec;
    uint32_t tot_sec = bpb->tot_sec32;
    if (tot_sec == 0) tot_sec = bpb->tot_sec16;
    return (tot_sec * 512u) / 1024u;
}

static uint32_t fat32_get_free_space_kb(uint32_t total_clusters) {
    uint32_t free_clusters = 0;
    uint32_t clusters_checked = 0;
    uint32_t max_clusters = total_clusters;
    
    // Read the FAT table sector by sector
    for (uint32_t s = 0; s < FAT32_FAT_SECS; s++) {
        _f32_read_sec(fat32_fat_lba + s);
        uint32_t *fat_entries = (uint32_t*)_f32_sec;
        for (int i = 0; i < 128; i++) {
            if (clusters_checked >= max_clusters) break;
            // First 2 clusters are reserved, skip them in count logic if needed, 
            // but reading them is fine, they aren't FREE (0) anyway.
            if ((fat_entries[i] & 0x0FFFFFFF) == FAT32_FREE) {
                free_clusters++;
            }
            clusters_checked++;
        }
        if (clusters_checked >= max_clusters) break;
    }
    
    return (free_clusters * (FAT32_SECS_CLUS * 512u)) / 1024u;
}

static uint32_t fat32_alloc_cluster(void) {
    uint32_t limit = fat32_total_clusters > 0 ? fat32_total_clusters + 2 : 16332u;
    for (uint32_t c = 3; c < limit; c++) {
        if (fat32_read_fat(c) == FAT32_FREE) {
            fat32_write_fat(c, 0x0FFFFFFFu); 
            
            uint32_t lba = _f32_cluster_to_lba(c);
            f32_mem_set(_f32_sec, 0, 512);
            for (uint32_t s = 0; s < FAT32_SECS_CLUS; s++)
                f32_write_sec(lba + s);
            return c;
        }
    }
    return 0; 
}

/* =========================================================================
   Directory entry helpers
   ========================================================================= */


static void f32_to_83(const char *name, char *n8, char *e3) {
    f32_mem_set(n8, ' ', 8);
    f32_mem_set(e3, ' ', 3);
    int i = 0, o = 0;
    while (name[i] && name[i] != '.' && o < 8) {
        char c = name[i++];
        n8[o++] = (c >= 'a' && c <= 'z') ? c - 32 : c;
    }
    int last_dot = -1;
    for (int k = 0; name[k]; k++) {
        if (name[k] == '.') last_dot = k;
    }
    if (last_dot >= 0) {
        int k = last_dot + 1;
        o = 0;
        while (name[k] && o < 3) {
            char c = name[k++];
            e3[o++] = (c >= 'a' && c <= 'z') ? c - 32 : c;
        }
    }
}


static void f32_from_83(const char *n8, const char *e3, char *out) {
    int i = 0, o = 0;
    while (i < 8 && n8[i] != ' ') out[o++] = n8[i++];
    
    if (e3[0] != ' ') {
        out[o++] = '.';
        for (int j = 0; j < 3 && e3[j] != ' '; j++) out[o++] = e3[j];
    }
    out[o] = '\0';
}


static uint8_t f32_lfn_checksum(const char *n8, const char *e3) {
    uint8_t buf[11]; f32_mem_cpy(buf, n8, 8); f32_mem_cpy(buf+8, e3, 3);
    uint8_t s = 0;
    for (int i = 0; i < 11; i++)
        s = ((s & 1) ? 0x80u : 0u) + (s >> 1) + buf[i];
    return s;
}


static char f32_lfn_char(uint16_t v) {
    if (v == 0xFFFF || v == 0x0000) return 0;
    return (v < 128) ? (char)v : '?';
}



static void f32_lfn_assemble(const fat32_lfn_t *entries, int count, char *out) {
    
    int o = 0;
    for (int i = count - 1; i >= 0 && o < 127; i--) {
        const fat32_lfn_t *e = &entries[i];
        char c;
        for (int k = 0; k < 5 && o < 127; k++) { c = f32_lfn_char(e->name1[k]); if (!c) goto done; out[o++] = c; }
        for (int k = 0; k < 6 && o < 127; k++) { c = f32_lfn_char(e->name2[k]); if (!c) goto done; out[o++] = c; }
        for (int k = 0; k < 2 && o < 127; k++) { c = f32_lfn_char(e->name3[k]); if (!c) goto done; out[o++] = c; }
    }
done:
    out[o] = '\0';
}


static int f32_write_lfn(uint8_t *sector, int slot_in_sector,
                          const char *name, uint8_t chksum)
{
    int nlen = f32_str_len(name);
    int n_entries = (nlen + 12) / 13; 
    if (n_entries > 21) n_entries = 21;
    if (n_entries == 0) return 0;

    
    for (int ei = 0; ei < n_entries; ei++) {
        int seq_num   = n_entries - ei;          
        bool is_last  = (ei == 0);               
        int char_base = (seq_num - 1) * 13;      
        int slot      = slot_in_sector + ei;

        fat32_lfn_t lfn;
        f32_mem_set(&lfn, 0xFF, sizeof(lfn));    
        lfn.seq      = (uint8_t)(seq_num | (is_last ? 0x40 : 0));
        lfn.attr     = FAT32_ATTR_LFN;
        lfn.type     = 0;
        lfn.checksum = chksum;
        lfn.clus_lo  = 0;

        
        for (int k = 0; k < 5; k++) {
            int ci = char_base + k;
            lfn.name1[k] = (ci < nlen) ? (uint16_t)(unsigned char)name[ci] : ((ci == nlen) ? 0x0000u : 0xFFFFu);
        }
        for (int k = 0; k < 6; k++) {
            int ci = char_base + 5 + k;
            lfn.name2[k] = (ci < nlen) ? (uint16_t)(unsigned char)name[ci] : ((ci == nlen) ? 0x0000u : 0xFFFFu);
        }
        for (int k = 0; k < 2; k++) {
            int ci = char_base + 11 + k;
            lfn.name3[k] = (ci < nlen) ? (uint16_t)(unsigned char)name[ci] : ((ci == nlen) ? 0x0000u : 0xFFFFu);
        }

        f32_mem_cpy(sector + slot * 32, &lfn, 32);
    }
    return n_entries;
}

/* =========================================================================
   Directory traversal: find file/dir by name (supports LFN)
   Returns first cluster of entry, or 0 if not found.
   Sets *is_dir if the entry is a directory.
   ========================================================================= */
static uint32_t fat32_find(uint32_t dir_cluster, const char *name, bool *is_dir, uint32_t *out_size) {
    
    char n8[8], e3[3];
    f32_to_83(name, n8, e3);

    uint32_t clus = dir_cluster;
    fat32_lfn_t lfn_buf[20];
    int lfn_count = 0;

    while (clus < 0x0FFFFFF8u) {
        uint32_t lba = _f32_cluster_to_lba(clus);
        for (uint32_t s = 0; s < FAT32_SECS_CLUS; s++) {
            os_yield();
            _f32_read_sec(lba + s);
            for (int slot = 0; slot < 16; slot++) {
                fat32_entry_t *e = (fat32_entry_t*)(_f32_sec + slot * 32);
                if ((uint8_t)e->name[0] == 0x00) return 0xFFFFFFFF; 
                if ((uint8_t)e->name[0] == 0xE5) { lfn_count = 0; continue; } 

                if (e->attr == FAT32_ATTR_LFN) {
                    
                    if (lfn_count < 20)
                        f32_mem_cpy(&lfn_buf[lfn_count++], e, 32);
                    continue;
                }

                
                bool matched = false;
                if (lfn_count > 0) {
                    char lfn_name[280];
                    f32_lfn_assemble(lfn_buf, lfn_count, lfn_name);
                    
                    int ni = 0;
                    while (lfn_name[ni] && name[ni]) {
                        char a = lfn_name[ni], b = name[ni];
                        if (a >= 'A' && a <= 'Z') a += 32;
                        if (b >= 'A' && b <= 'Z') b += 32;
                        if (a != b) break;
                        ni++;
                    }
                    if (!lfn_name[ni] && !name[ni]) matched = true;
                } else {
                    bool nm = true, em = true;
                    for (int k = 0; k < 8; k++) {
                        char c = e->name[k];
                        if (c >= 'a' && c <= 'z') c -= 32;
                        if (c != n8[k]) { nm = false; break; }
                    }
                    for (int k = 0; k < 3; k++) {
                        char c = e->ext[k];
                        if (c >= 'a' && c <= 'z') c -= 32;
                        if (c != e3[k]) { em = false; break; }
                    }
                    if (nm && em) matched = true;
                }

                if (matched) {
                    if (is_dir) *is_dir = (e->attr & FAT32_ATTR_DIR) != 0;
                    if (out_size) *out_size = e->file_size;
                    uint32_t c = ((uint32_t)e->fst_clus_hi << 16) | e->fst_clus_lo;
                    return c;
                }
                lfn_count = 0;
            }
        }
        clus = fat32_read_fat(clus);
    }
    return 0xFFFFFFFF;
}

/* =========================================================================
   List directory (callback receives human-readable name, attr, size, clus)
   ========================================================================= */
typedef void (*fat32_ls_cb)(const char *name, uint8_t attr, uint32_t size, uint32_t clus);



static void fat32_list_dir(uint32_t dir_cluster, fat32_ls_cb cb) {
    uint32_t clus = dir_cluster;
    fat32_lfn_t lfn_buf[20]; int lfn_count = 0;

    while (clus < 0x0FFFFFF8u) {
        uint32_t lba = _f32_cluster_to_lba(clus);
        for (uint32_t s = 0; s < FAT32_SECS_CLUS; s++) {
            os_yield();
            _f32_read_sec(lba + s);
            for (int slot = 0; slot < 16; slot++) {
                fat32_entry_t *e = (fat32_entry_t*)(_f32_sec + slot * 32);
                if ((uint8_t)e->name[0] == 0x00) return;
                if ((uint8_t)e->name[0] == 0xE5) { lfn_count = 0; continue; }
                if (e->attr == FAT32_ATTR_LFN) {
                    if (lfn_count < 20) f32_mem_cpy(&lfn_buf[lfn_count++], e, 32);
                    continue;
                }
                
                if (e->name[0] == '.') { lfn_count = 0; continue; }

                char display[280];
                if (lfn_count > 0) {
                    f32_lfn_assemble(lfn_buf, lfn_count, display);
                } else {
                    f32_from_83(e->name, e->ext, display);
                }
                uint32_t c = ((uint32_t)e->fst_clus_hi << 16) | e->fst_clus_lo;
                cb(display, e->attr, e->file_size, c);
                lfn_count = 0;
            }
        }
        clus = fat32_read_fat(clus);
    }
}

/* =========================================================================
   Create a directory entry (with optional LFN).
   Finds a free slot (or extends the directory with a new cluster).
   ========================================================================= */
static bool fat32_create_entry(uint32_t dir_cluster, const char *name,
                                uint8_t attr, uint32_t first_clus, uint32_t size)
{
    char n8[8], e3[3];
    f32_to_83(name, n8, e3);
    uint8_t chksum = f32_lfn_checksum(n8, e3);
    int nlen = f32_str_len(name);
    int need_lfn = (nlen > 11) || (nlen > 0 && 1); 
    int n_lfn = need_lfn ? ((nlen + 12) / 13) : 0;
    if (n_lfn > 21) n_lfn = 21;
    int slots_needed = n_lfn + 1;

    uint32_t clus = dir_cluster;
    while (clus < 0x0FFFFFF8u) {
        uint32_t lba = _f32_cluster_to_lba(clus);
        for (uint32_t s = 0; s < FAT32_SECS_CLUS; s++) {
            os_yield();
            _f32_read_sec(lba + s);
            
            int consecutive = 0, start_slot = -1;
            for (int slot = 0; slot < 16; slot++) {
                uint8_t first = (uint8_t)_f32_sec[slot * 32];
                if (first == 0x00 || first == 0xE5) {
                    if (consecutive == 0) start_slot = slot;
                    consecutive++;
                    if (consecutive >= slots_needed) goto found_slot;
                } else {
                    consecutive = 0; start_slot = -1;
                }
            }
            continue;
found_slot:
            
            if (n_lfn > 0)
                f32_write_lfn(_f32_sec, start_slot, name, chksum);

            
            fat32_entry_t *e = (fat32_entry_t*)(_f32_sec + (start_slot + n_lfn) * 32);
            f32_mem_set(e, 0, 32);
            f32_mem_cpy(e->name, n8, 8);
            f32_mem_cpy(e->ext,  e3, 3);
            e->attr       = attr;
            e->fst_clus_hi = (uint16_t)(first_clus >> 16);
            e->fst_clus_lo = (uint16_t)(first_clus & 0xFFFF);
            e->file_size  = size;
            e->crt_date   = 0x5521; 
            e->wrt_date   = 0x5521;
            f32_write_sec(lba + s);
            return true;
        }
        clus = fat32_read_fat(clus);
    }

    
    uint32_t prev_clus = dir_cluster;
    uint32_t c = dir_cluster;
    while (fat32_read_fat(c) < 0x0FFFFFF8u) { prev_clus = c; c = fat32_read_fat(c); }
    prev_clus = c;

    uint32_t new_clus = fat32_alloc_cluster();
    if (!new_clus) return false;
    fat32_write_fat(prev_clus, new_clus);
    fat32_write_fat(new_clus, 0x0FFFFFFFu);

    uint32_t base_lba = _f32_cluster_to_lba(new_clus);
    f32_mem_set(_f32_sec, 0, 512);
    for (uint32_t s = 0; s < FAT32_SECS_CLUS; s++) {
        f32_write_sec(base_lba + s);
    }
    
    _f32_read_sec(_f32_cluster_to_lba(new_clus));
    if (n_lfn > 0) f32_write_lfn(_f32_sec, 0, name, chksum);
    fat32_entry_t *e = (fat32_entry_t*)(_f32_sec + n_lfn * 32);
    f32_mem_set(e, 0, 32);
    f32_mem_cpy(e->name, n8, 8);
    f32_mem_cpy(e->ext,  e3, 3);
    e->attr       = attr;
    e->fst_clus_hi = (uint16_t)(first_clus >> 16);
    e->fst_clus_lo = (uint16_t)(first_clus & 0xFFFF);
    e->file_size  = size;
    e->crt_date   = 0x5521;
    e->wrt_date   = 0x5521;
    f32_write_sec(_f32_cluster_to_lba(new_clus));
    return true;
}

/* =========================================================================
   High-level API
   ========================================================================= */



static bool fat32_rename(uint32_t dir_cluster, const char *old_name, const char *new_name) {
    bool is_dir = false;
    uint32_t file_size = 0;
    uint32_t target_clus = fat32_find(dir_cluster, old_name, &is_dir, &file_size);
    if (target_clus == 0xFFFFFFFF) return false;
    
    if (fat32_find(dir_cluster, new_name, 0, 0) != 0xFFFFFFFF) return false;
    
    char n8[8], e3[3];
    f32_to_83(old_name, n8, e3);
    
    uint32_t c = dir_cluster;
    fat32_lfn_t lfn_buf[20];
    int lfn_count = 0;
    
    while (c < 0x0FFFFFF8u) {
        uint32_t lba = _f32_cluster_to_lba(c);
        for (uint32_t s = 0; s < FAT32_SECS_CLUS; s++) {
            os_yield();
            _f32_read_sec(lba + s);
            for (int slot = 0; slot < 16; slot++) {
                fat32_entry_t *e = (fat32_entry_t*)(_f32_sec + slot * 32);
                if ((uint8_t)e->name[0] == 0x00) goto end_search;
                if ((uint8_t)e->name[0] == 0xE5) { lfn_count = 0; continue; }
                
                if (e->attr == FAT32_ATTR_LFN) {
                    if (lfn_count < 20) f32_mem_cpy(&lfn_buf[lfn_count++], e, 32);
                    continue;
                }
                
                bool matched = false;
                if (lfn_count > 0) {
                    char lfn_name[280];
                    f32_lfn_assemble(lfn_buf, lfn_count, lfn_name);
                    if (str_cmp(lfn_name, old_name) == 0) matched = true;
                } else {
                    if (my_mem_cmp(e->name, n8, 8) && my_mem_cmp(e->ext, e3, 3)) matched = true;
                }
                
                if (matched) {
                    e->name[0] = 0xE5;
                    f32_write_sec(lba + s);
                    
                    if (lfn_count > 0) {
                        for (int i = 1; i <= lfn_count; i++) {
                            int prev_slot = slot - i;
                            if (prev_slot >= 0) {
                                _f32_sec[prev_slot * 32] = 0xE5;
                            }
                        }
                        f32_write_sec(lba + s);
                    }
                    
                    return fat32_create_entry(dir_cluster, new_name, is_dir ? FAT32_ATTR_DIR : FAT32_ATTR_ARC, target_clus, file_size);
                }
                lfn_count = 0;
            }
        }
        c = fat32_read_fat(c);
    }
end_search:
    return false;
}

static bool fat32_mkdir(uint32_t parent_cluster, const char *name) {
    if (fat32_find(parent_cluster, name, 0, 0) != 0xFFFFFFFF) return false; 
    uint32_t new_clus = fat32_alloc_cluster();
    if (!new_clus) return false;

    uint32_t base_lba = _f32_cluster_to_lba(new_clus);
    f32_mem_set(_f32_sec, 0, 512);
    for (uint32_t s = 0; s < FAT32_SECS_CLUS; s++) {
        f32_write_sec(base_lba + s);
    }
    
    _f32_read_sec(_f32_cluster_to_lba(new_clus));
    fat32_entry_t *dot  = (fat32_entry_t*)_f32_sec;
    fat32_entry_t *ddot = (fat32_entry_t*)(_f32_sec + 32);
    f32_mem_set(dot,  0, 32); f32_mem_set(ddot, 0, 32);
    f32_mem_set(dot->name,  ' ', 8); dot->name[0]  = '.';
    f32_mem_set(ddot->name, ' ', 8); ddot->name[0] = '.'; ddot->name[1] = '.';
    f32_mem_set(dot->ext,   ' ', 3); f32_mem_set(ddot->ext, ' ', 3);
    dot->attr  = FAT32_ATTR_DIR; dot->fst_clus_hi  = (uint16_t)(new_clus    >> 16); dot->fst_clus_lo  = (uint16_t)new_clus;
    ddot->attr = FAT32_ATTR_DIR; ddot->fst_clus_hi = (uint16_t)(parent_cluster >> 16); ddot->fst_clus_lo = (uint16_t)parent_cluster;
    f32_write_sec(_f32_cluster_to_lba(new_clus));

    return fat32_create_entry(parent_cluster, name, FAT32_ATTR_DIR, new_clus, 0);
}




static bool fat32_move(uint32_t src_dir, uint32_t dest_dir, const char *src_name, const char *dest_name) {
    if (fat32_find(dest_dir, dest_name, 0, 0) != 0xFFFFFFFF) return false; // Already exists in dest

    uint32_t c = src_dir;
    char n8[8], e3[3];
    f32_to_83(src_name, n8, e3);
    fat32_lfn_t lfn_buf[20];
    int lfn_count = 0;
    
    uint8_t attr = 0;
    uint32_t first_clus = 0;
    uint32_t size = 0;
    bool found = false;

    while (c < 0x0FFFFFF8u && !found) {
        uint32_t lba = _f32_cluster_to_lba(c);
        for (uint32_t s = 0; s < FAT32_SECS_CLUS && !found; s++) {
            _f32_read_sec(lba + s);
            for (int slot = 0; slot < 16; slot++) {
                fat32_entry_t *e = (fat32_entry_t*)(_f32_sec + slot * 32);
                if ((uint8_t)e->name[0] == 0x00) goto end_search;
                if ((uint8_t)e->name[0] == 0xE5) { lfn_count = 0; continue; }
                if (e->attr == FAT32_ATTR_LFN) {
                    if (lfn_count < 20) f32_mem_cpy(&lfn_buf[lfn_count++], e, 32);
                    continue;
                }
                bool matched = false;
                if (lfn_count > 0) {
                    char lfn_name[280];
                    f32_lfn_assemble(lfn_buf, lfn_count, lfn_name);
                    if (str_cmp(lfn_name, src_name) == 0) matched = true;
                } else {
                    if (my_mem_cmp(e->name, n8, 8) && my_mem_cmp(e->ext, e3, 3)) matched = true;
                }
                if (matched) {
                    first_clus = ((uint32_t)e->fst_clus_hi << 16) | e->fst_clus_lo;
                    size = e->file_size;
                    attr = e->attr;
                    e->name[0] = 0xE5;
                    f32_write_sec(lba + s);
                    
                    if (lfn_count > 0) {
                        for (int i = 1; i <= lfn_count; i++) {
                            int prev_slot = slot - i;
                            if (prev_slot >= 0) {
                                _f32_sec[prev_slot * 32] = 0xE5;
                            }
                        }
                        f32_write_sec(lba + s);
                    }
                    
                    found = true;
                    break;
                }
                lfn_count = 0;
            }
        }
        if (!found) c = fat32_read_fat(c);
    }
end_search:
    
    if (!found) return false;

    return fat32_create_entry(dest_dir, dest_name, attr, first_clus, size);
}

static void fat32_free_chain(uint32_t cluster) {
    while (cluster >= 2 && cluster < 0x0FFFFFF8u) {
        uint32_t next = fat32_read_fat(cluster);
        fat32_write_fat(cluster, 0); // Free it
        cluster = next;
    }
}

static bool fat32_hard_delete(uint32_t dir_cluster, const char *name) {
    uint32_t c = dir_cluster;
    char n8[8], e3[3];
    f32_to_83(name, n8, e3);
    fat32_lfn_t lfn_buf[20];
    int lfn_count = 0;
    while (c < 0x0FFFFFF8u) {
        uint32_t lba = _f32_cluster_to_lba(c);
        for (uint32_t s = 0; s < FAT32_SECS_CLUS; s++) {
            os_yield();
            _f32_read_sec(lba + s);
            for (int slot = 0; slot < 16; slot++) {
                fat32_entry_t *e = (fat32_entry_t*)(_f32_sec + slot * 32);
                if ((uint8_t)e->name[0] == 0x00) return false;
                if ((uint8_t)e->name[0] == 0xE5) { lfn_count = 0; continue; }
                if (e->attr == FAT32_ATTR_LFN) {
                    if (lfn_count < 20) f32_mem_cpy(&lfn_buf[lfn_count++], e, 32);
                    continue;
                }
                bool matched = false;
                if (lfn_count > 0) {
                    char lfn_name[280];
                    f32_lfn_assemble(lfn_buf, lfn_count, lfn_name);
                    if (str_cmp(lfn_name, name) == 0) matched = true;
                } else {
                    if (my_mem_cmp(e->name, n8, 8) && my_mem_cmp(e->ext, e3, 3)) matched = true;
                }
                if (matched) {
                    uint32_t fst_clus = ((uint32_t)e->fst_clus_hi << 16) | e->fst_clus_lo;
                    uint32_t sz = e->file_size;
                    e->name[0] = 0xE5;
                    f32_write_sec(lba + s);
                    
                    if (lfn_count > 0) {
                        for (int i = 1; i <= lfn_count; i++) {
                            int prev_slot = slot - i;
                            if (prev_slot >= 0) {
                                _f32_sec[prev_slot * 32] = 0xE5;
                            }
                        }
                        f32_write_sec(lba + s);
                    }
                    
                    // Secure wipe / Zero fill
                    uint32_t cl = fst_clus;
                    uint32_t wiped = 0;
                    while (cl >= 2 && cl < 0x0FFFFFF8u) {
                        uint32_t clba = _f32_cluster_to_lba(cl);
                        f32_mem_set(_f32_sec, 0, 512);
                        for (uint32_t ws = 0; ws < FAT32_SECS_CLUS; ws++) {
                            f32_write_sec(clba + ws);
                            wiped += 512;
                            if (wiped >= sz && sz != 0) break;
                        }
                        cl = fat32_read_fat(cl);
                    }
                    fat32_free_chain(fst_clus);
                    return true;
                }
                lfn_count = 0;
            }
        }
        c = fat32_read_fat(c);
    }
    return false;
}

static uint32_t fat32_get_trash_dir() {
    if (!fat32_ready) return 0xFFFFFFFF;
    _f32_read_sec(FAT32_PART_LBA + 7);
    uint32_t trash_clus = *(uint32_t*)(&_f32_sec[4]);
    if (trash_clus == 0 || trash_clus == 0xFFFFFFFF) {
        trash_clus = fat32_create_phantom_dir();
        if (trash_clus != 0xFFFFFFFF) {
            _f32_read_sec(FAT32_PART_LBA + 7);
            *(uint32_t*)(&_f32_sec[4]) = trash_clus;
            f32_write_sec(FAT32_PART_LBA + 7);
        }
    }
    return trash_clus;
}

static void fat32_empty_trash(void) {
    uint32_t trash_clus = fat32_get_trash_dir();
    if (trash_clus == 0xFFFFFFFFu) return;

    uint32_t c = trash_clus;
    while (c >= 2 && c < 0x0FFFFFF8u) {
        uint32_t lba = _f32_cluster_to_lba(c);
        for (uint32_t s = 0; s < FAT32_SECS_CLUS; s++) {
            _f32_read_sec(lba + s);
            bool modified = false;
            for (int slot = 0; slot < 16; slot++) {
                fat32_entry_t *e = (fat32_entry_t*)(_f32_sec + slot * 32);
                if ((uint8_t)e->name[0] == 0x00) continue;
                if ((uint8_t)e->name[0] == 0xE5) continue;
                if (e->name[0] == '.') continue; // skip . and ..
                
                if (e->attr == FAT32_ATTR_LFN) {
                    e->name[0] = 0xE5;
                    modified = true;
                    continue;
                }
                
                uint32_t fst_clus = ((uint32_t)e->fst_clus_hi << 16) | e->fst_clus_lo;
                uint32_t sz = e->file_size;
                e->name[0] = 0xE5;
                modified = true;
                
                // Save sector before wiping data
                f32_write_sec(lba + s);
                
                uint32_t cl = fst_clus;
                uint32_t wiped = 0;
                while (cl >= 2 && cl < 0x0FFFFFF8u) {
                    uint32_t clba = _f32_cluster_to_lba(cl);
                    f32_mem_set(_f32_sec, 0, 512);
                    for (uint32_t ws = 0; ws < FAT32_SECS_CLUS; ws++) {
                        f32_write_sec(clba + ws);
                        wiped += 512;
                        if (wiped >= sz && sz != 0) break;
                    }
                    cl = fat32_read_fat(cl);
                }
                fat32_free_chain(fst_clus);
                
                // Restore sector to continue loop
                _f32_read_sec(lba + s);
            }
            if (modified) f32_write_sec(lba + s);
        }
        c = fat32_read_fat(c);
    }
}

static bool fat32_delete(uint32_t dir_cluster, const char *name) {
    uint32_t c = dir_cluster;
    char n8[8], e3[3];
    f32_to_83(name, n8, e3);
    fat32_lfn_t lfn_buf[20];
    int lfn_count = 0;
    while (c < 0x0FFFFFF8u) {
        uint32_t lba = _f32_cluster_to_lba(c);
        for (uint32_t s = 0; s < FAT32_SECS_CLUS; s++) {
            os_yield();
            _f32_read_sec(lba + s);
            for (int slot = 0; slot < 16; slot++) {
                fat32_entry_t *e = (fat32_entry_t*)(_f32_sec + slot * 32);
                if ((uint8_t)e->name[0] == 0x00) return false;
                if ((uint8_t)e->name[0] == 0xE5) { lfn_count = 0; continue; }
                if (e->attr == FAT32_ATTR_LFN) {
                    if (lfn_count < 20) f32_mem_cpy(&lfn_buf[lfn_count++], e, 32);
                    continue;
                }
                bool matched = false;
                if (lfn_count > 0) {
                    char lfn_name[280];
                    f32_lfn_assemble(lfn_buf, lfn_count, lfn_name);
                    if (str_cmp(lfn_name, name) == 0) matched = true;
                } else {
                    if (my_mem_cmp(e->name, n8, 8) && my_mem_cmp(e->ext, e3, 3)) matched = true;
                }
                if (matched) {
                    uint32_t fst_clus = ((uint32_t)e->fst_clus_hi << 16) | e->fst_clus_lo;
                    e->name[0] = 0xE5;
                    f32_write_sec(lba + s);
                    
                    if (lfn_count > 0) {
                        for (int i = 1; i <= lfn_count; i++) {
                            int prev_slot = slot - i;
                            if (prev_slot >= 0) {
                                _f32_sec[prev_slot * 32] = 0xE5;
                            }
                        }
                        f32_write_sec(lba + s);
                    }
                    
                    fat32_free_chain(fst_clus);
                    return true;
                }
                lfn_count = 0;
            }
        }
        c = fat32_read_fat(c);
    }
    return false;
}

static bool fat32_mkfile(uint32_t parent_cluster, const char *name,
                          const uint8_t *data, uint32_t size)
{
    if (fat32_find(parent_cluster, name, 0, 0) != 0xFFFFFFFF) return false;

    uint32_t first_clus = 0;
    if (size > 0) {
        first_clus = fat32_alloc_cluster();
        if (!first_clus) return false;

        uint32_t written = 0, cur = first_clus;
        while (written < size) {
            uint32_t lba = _f32_cluster_to_lba(cur);
            for (uint32_t s = 0; s < FAT32_SECS_CLUS && written < size; s++) {
                uint32_t chunk = size - written; if (chunk > 512u) chunk = 512u;
                f32_mem_set(_f32_sec, 0, 512);
                f32_mem_cpy(_f32_sec, data + written, chunk);
                f32_write_sec(lba + s);
                written += chunk;
            }
            if (written < size) {
                uint32_t nc = fat32_alloc_cluster();
                if (!nc) break;
                fat32_write_fat(cur, nc);
                fat32_write_fat(nc, 0x0FFFFFFFu);
                cur = nc;
            }
        }
    }
    return fat32_create_entry(parent_cluster, name, FAT32_ATTR_ARC, first_clus, size);
}


static uint32_t fat32_readfile(uint32_t parent_cluster, const char *name,
                                uint8_t *buf, uint32_t max_size)
{
    uint32_t file_size = 0;
    uint32_t clus = fat32_find(parent_cluster, name, 0, &file_size);
    if (clus == 0xFFFFFFFF) return 0xFFFFFFFF;
    if (file_size > max_size) file_size = max_size;

    uint32_t read = 0;
    while (clus < 0x0FFFFFF8u && read < file_size) {
        uint32_t lba = _f32_cluster_to_lba(clus);
        for (uint32_t s = 0; s < FAT32_SECS_CLUS && read < file_size; s++) {
            _f32_read_sec(lba + s);
            uint32_t chunk = file_size - read; if (chunk > 512u) chunk = 512u;
            f32_mem_cpy(buf + read, _f32_sec, chunk);
            read += chunk;
        }
        clus = fat32_read_fat(clus);
    }
    return read;
}

/* =========================================================================
   Format partition as FAT32 (first boot)
   ========================================================================= */
static void fat32_format(void) {
    /* --- Write BPB at partition start --- */
    f32_mem_set(_f32_sec, 0, 512);
    fat32_bpb_t *bpb = (fat32_bpb_t*)_f32_sec;
    bpb->jmp[0] = 0xEB; bpb->jmp[1] = 0x58; bpb->jmp[2] = 0x90;
    f32_mem_cpy(bpb->oem, "VIATAHOS", 8);
    bpb->bytes_per_sec = 512;
    bpb->sec_per_clus  = FAT32_SECS_CLUS;
    bpb->rsvd_sec_cnt  = FAT32_RSVD;
    bpb->num_fats      = FAT32_NUM_FATS;
    bpb->root_ent_cnt  = 0;
    bpb->tot_sec16     = 0;
    bpb->media         = 0xF8;
    bpb->fat_sz16      = 0;
    bpb->sec_per_trk   = 63;
    bpb->num_heads     = 255;
    bpb->hidd_sec      = FAT32_PART_LBA;
    
    uint32_t disk_secs = (current_fat32_drv && current_fat32_drv->size_mb > 0) ?
                         (current_fat32_drv->size_mb * 2048u) : (FAT32_PART_SECS + FAT32_PART_LBA);
    uint32_t tot_sec = (disk_secs > FAT32_PART_LBA) ? (disk_secs - FAT32_PART_LBA) : FAT32_PART_SECS;
    bpb->tot_sec32 = tot_sec;
    
    uint32_t fat_sz = FAT32_FAT_SECS;
    if (current_fat32_drv && current_fat32_drv->size_mb > 0) {
        uint32_t num_clus = (tot_sec - FAT32_RSVD) / FAT32_SECS_CLUS;
        fat_sz = ((num_clus * 4) + 511) / 512;
    }
    bpb->fat_sz32 = fat_sz;
    bpb->root_clus     = FAT32_ROOT_CLUS;
    bpb->fs_info       = 1;
    bpb->bk_boot_sec   = 6;
    bpb->drv_num       = 0x80;
    bpb->boot_sig      = 0x29;
    bpb->vol_id        = 0x56494154u; 
    f32_mem_cpy(bpb->vol_lab, "VIATAHOS   ", 11);
    f32_mem_cpy(bpb->fil_sys_type, "FAT32   ", 8);
    _f32_sec[510] = 0x55; _f32_sec[511] = 0xAA;
    bpb->fat_sz32 = fat_sz;
    f32_write_sec(FAT32_PART_LBA);

    /* --- Zero both FAT copies --- */
    f32_mem_set(_f32_sec, 0, 512);
    for (uint32_t s = 0; s < fat_sz; s++) {
        f32_write_sec(FAT32_PART_LBA + FAT32_RSVD + s);
        f32_write_sec(FAT32_PART_LBA + FAT32_RSVD + fat_sz + s);
    }

    /* --- Write FAT entries 0, 1, 2 --- */
    fat32_fat_lba      = FAT32_PART_LBA + FAT32_RSVD;
    fat32_data_lba     = FAT32_PART_LBA + FAT32_RSVD + (FAT32_NUM_FATS * fat_sz);
    fat32_root_cluster = FAT32_ROOT_CLUS;

    _f32_read_sec(fat32_fat_lba);
    uint32_t v0 = 0x0FFFFFF8u, v1 = 0x0FFFFFFFu, v2 = 0x0FFFFFFFu;
    f32_mem_cpy(_f32_sec + 0,  &v0, 4);
    f32_mem_cpy(_f32_sec + 4,  &v1, 4);
    f32_mem_cpy(_f32_sec + 8,  &v2, 4);
    f32_write_sec(fat32_fat_lba);
    f32_write_sec(fat32_fat_lba + fat_sz);

    /* --- Zero root directory cluster --- */
    uint32_t root_lba = _f32_cluster_to_lba(FAT32_ROOT_CLUS);
    f32_mem_set(_f32_sec, 0, 512);
    for (uint32_t s = 0; s < FAT32_SECS_CLUS; s++)
        f32_write_sec(root_lba + s);

    
}

/* =========================================================================
   Mount / init
   ========================================================================= */
static void fat32_init(void) {
    fat32_fat_lba      = FAT32_PART_LBA + FAT32_RSVD;
    fat32_data_lba     = FAT32_PART_LBA + FAT32_DATA_OFF;
    fat32_root_cluster = FAT32_ROOT_CLUS;

    _f32_read_sec(FAT32_PART_LBA);
    
    fat32_bpb_t *bpb = (fat32_bpb_t*)_f32_sec;
    bool valid_bpb = (_f32_sec[510] == 0x55 && _f32_sec[511] == 0xAA &&
                      _f32_sec[82] == 'F' && _f32_sec[83] == 'A' && _f32_sec[84] == 'T' &&
                      bpb->sec_per_clus != 0 && bpb->bytes_per_sec == 512 &&
                      bpb->num_fats != 0 && bpb->fat_sz32 != 0);

    if (valid_bpb) {
        fat32_fat_lba  = FAT32_PART_LBA + bpb->rsvd_sec_cnt;
        fat32_data_lba = FAT32_PART_LBA + bpb->rsvd_sec_cnt + bpb->num_fats * bpb->fat_sz32;
        fat32_root_cluster = bpb->root_clus ? bpb->root_clus : FAT32_ROOT_CLUS;
        uint32_t tot_sec = bpb->tot_sec32 == 0 ? bpb->tot_sec16 : bpb->tot_sec32;
        uint32_t overhead = bpb->rsvd_sec_cnt + bpb->num_fats * bpb->fat_sz32;
        if (tot_sec > overhead && bpb->sec_per_clus != 0) {
            fat32_total_clusters = (tot_sec - overhead) / bpb->sec_per_clus;
        } else {
            fat32_total_clusters = 0;
        }
        fat32_ready = true;
    } else {
        fat32_format();
        _f32_read_sec(FAT32_PART_LBA);
        fat32_bpb_t *fbpb = (fat32_bpb_t*)_f32_sec;
        uint32_t spc   = fbpb->sec_per_clus ? fbpb->sec_per_clus : FAT32_SECS_CLUS;
        uint32_t rsvd  = fbpb->rsvd_sec_cnt ? fbpb->rsvd_sec_cnt : FAT32_RSVD;
        uint32_t fsz   = fbpb->fat_sz32     ? fbpb->fat_sz32     : FAT32_FAT_SECS;
        uint32_t nfats = fbpb->num_fats     ? fbpb->num_fats     : FAT32_NUM_FATS;
        fat32_fat_lba  = FAT32_PART_LBA + rsvd;
        fat32_data_lba = FAT32_PART_LBA + rsvd + nfats * fsz;
        fat32_root_cluster = fbpb->root_clus ? fbpb->root_clus : FAT32_ROOT_CLUS;
        uint32_t tot_sec = fbpb->tot_sec32 == 0 ? fbpb->tot_sec16 : fbpb->tot_sec32;
        if (tot_sec == 0) tot_sec = FAT32_PART_SECS;
        uint32_t overhead = rsvd + nfats * fsz;
        if (tot_sec > overhead && spc != 0) {
            fat32_total_clusters = (tot_sec - overhead) / spc;
        } else {
            fat32_total_clusters = 0;
        }
        fat32_ready = true;
    }
}

static uint32_t fat32_root(void) { return fat32_root_cluster; }

#define FAT32_SYSCONF_SECS 8

static bool fat32_read_sysconf(void *buf) {
    if (!fat32_ready) return false;
    for (uint32_t i = 0; i < FAT32_SYSCONF_SECS; i++) {
        _f32_read_sec(FAT32_PART_LBA + 7 + i);
        f32_mem_cpy((uint8_t*)buf + (i * 512), _f32_sec, 512);
    }
    return true;
}

static bool fat32_write_sysconf(const void *buf) {
    if (!fat32_ready) return false;
    for (uint32_t i = 0; i < FAT32_SYSCONF_SECS; i++) {
        f32_mem_cpy(_f32_sec, (const uint8_t*)buf + (i * 512), 512);
        f32_write_sec(FAT32_PART_LBA + 7 + i);
    }
    return true;
}

static uint32_t fat32_create_phantom_dir() {
    uint32_t new_clus = fat32_alloc_cluster();
    if (!new_clus) return 0xFFFFFFFF;

    uint32_t base_lba = _f32_cluster_to_lba(new_clus);
    f32_mem_set(_f32_sec, 0, 512);
    for (uint32_t s = 0; s < FAT32_SECS_CLUS; s++) {
        f32_write_sec(base_lba + s);
    }
    
    _f32_read_sec(base_lba);
    fat32_entry_t *dot  = (fat32_entry_t*)_f32_sec;
    fat32_entry_t *ddot = (fat32_entry_t*)(_f32_sec + 32);
    f32_mem_set(dot,  0, 32); f32_mem_set(ddot, 0, 32);
    f32_mem_set(dot->name,  ' ', 8); dot->name[0]  = '.';
    f32_mem_set(ddot->name, ' ', 8); ddot->name[0] = '.'; ddot->name[1] = '.';
    
    dot->attr = FAT32_ATTR_DIR;
    ddot->attr = FAT32_ATTR_DIR;
    
    dot->fst_clus_hi = (new_clus >> 16) & 0xFFFF;
    dot->fst_clus_lo = new_clus & 0xFFFF;
    
    uint32_t p_clus = fat32_root();
    ddot->fst_clus_hi = (p_clus >> 16) & 0xFFFF;
    ddot->fst_clus_lo = p_clus & 0xFFFF;
    
    f32_write_sec(base_lba);
    return new_clus;
}

#endif 
