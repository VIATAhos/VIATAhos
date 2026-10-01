#ifndef ISO9660_H
#define ISO9660_H

#include <stdint.h>
#include <stdbool.h>
#include "vga.h"
#include "ata.h"
#include "string.h"

// Basic ISO9660 implementation
static uint8_t iso_buf[2048];

static void iso9660_print_tree(uint16_t base, uint8_t sel) {
    // Read Primary Volume Descriptor at LBA 16
    atapi_read_sector(base, sel, 16, iso_buf);
    
    if (iso_buf[1] != 'C' || iso_buf[2] != 'D' || iso_buf[3] != '0' || iso_buf[4] != '0' || iso_buf[5] != '1') {
// vga_set_color(8, 0);
        vga_print("    (Nessun filesystem ISO9660 rilevato)\n");
// vga_set_color(15, 0);
        return;
    }
    
    // Read Root Directory Record (starts at offset 156 of PVD)
    uint8_t *root_record = &iso_buf[156];
    uint32_t root_lba = root_record[2] | (root_record[3] << 8) | (root_record[4] << 16) | (root_record[5] << 24);
    uint32_t root_size = root_record[10] | (root_record[11] << 8) | (root_record[12] << 16) | (root_record[13] << 24);
    
    vga_print("    |-[ISO9660 Volume]\n");
    
    // Read root directory sectors
    uint32_t sectors = (root_size + 2047) / 2048;
    for (uint32_t s = 0; s < sectors; s++) {
        uint8_t dir_buf[2048];
        atapi_read_sector(base, sel, root_lba + s, dir_buf);
        
        int offset = 0;
        while (offset < 2048) {
            uint8_t len = dir_buf[offset];
            if (len == 0) break; // End of sector
            
            uint8_t name_len = dir_buf[offset + 32];
            char name[64];
            for (int i = 0; i < name_len && i < 63; i++) {
                name[i] = dir_buf[offset + 33 + i];
            }
            name[name_len] = '\0';
            
            // Skip . and ..
            if (name_len == 1 && name[0] == 0) {} // .
            else if (name_len == 1 && name[0] == 1) {} // ..
            else {
                vga_print("    |    |-");
                vga_print(name);
                vga_print("\n");
            }
            
            offset += len;
        }
    }
}

#endif
