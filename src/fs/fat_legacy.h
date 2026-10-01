#ifndef FAT_LEGACY_H
#define FAT_LEGACY_H

#include <stdint.h>
#include <stdbool.h>
#include "vga.h"
#include "ata.h"
#include "string.h"

// Parse fixed root directory of FAT12 / FAT16
static void fat_legacy_print_tree(uint16_t base, uint8_t sel, const char* name_label) {
    uint8_t buf[512];
    ata_read_sector(base, sel, 0, buf);
    
    // Read BPB
    uint16_t bytes_per_sec = buf[11] | (buf[12] << 8);
    // uint8_t sec_per_clus = buf[13];
    uint16_t rsvd_sec_cnt = buf[14] | (buf[15] << 8);
    uint8_t num_fats = buf[16];
    uint16_t root_ent_cnt = buf[17] | (buf[18] << 8);
    uint16_t fat_sz16 = buf[22] | (buf[23] << 8);
    
    if (bytes_per_sec == 0 || root_ent_cnt == 0) {
// vga_set_color(8, 0);
        vga_print("    (Nessun filesystem FAT Legacy rilevato)\n");
// vga_set_color(15, 0);
        return;
    }
    
    uint32_t fat_lba = rsvd_sec_cnt;
    uint32_t root_lba = fat_lba + (num_fats * fat_sz16);
    uint32_t root_sectors = (root_ent_cnt * 32 + bytes_per_sec - 1) / bytes_per_sec;
    
    vga_print("    |-");
    vga_print(name_label);
    vga_print("\n");
    
    for (uint32_t s = 0; s < root_sectors; s++) {
        ata_read_sector(base, sel, root_lba + s, buf);
        for (int i = 0; i < bytes_per_sec; i += 32) {
            uint8_t first_char = buf[i];
            if (first_char == 0) break; // End of directory
            if (first_char == 0xE5) continue; // Deleted
            
            uint8_t attr = buf[i + 11];
            if (attr == 0x0F) continue; // LFN
            if (attr & 0x08) continue; // Volume Label
            
            char name[12];
            int n = 0;
            for (int j = 0; j < 8; j++) {
                if (buf[i + j] != ' ') name[n++] = buf[i + j];
            }
            if (buf[i + 8] != ' ') {
                name[n++] = '.';
                for (int j = 8; j < 11; j++) {
                    if (buf[i + j] != ' ') name[n++] = buf[i + j];
                }
            }
            name[n] = '\0';
            
            if (name[0] == '.') continue;
            
            vga_print("    |    |-");
            vga_print(name);
            vga_print("\n");
        }
    }
}

#endif
