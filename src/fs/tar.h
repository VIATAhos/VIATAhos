#ifndef TAR_H
#define TAR_H

#include <stdint.h>
#include <stdbool.h>
#include "vga.h"
#include "ata.h"
#include "string.h"

static void tar_print_tree(uint16_t base, uint8_t sel) {
    uint8_t buf[512];
    uint32_t lba = 0;
    
    vga_print("    |-[TAR Archive (Tape)]\n");
    
    while (lba < 100000) { // arbitrary limit to prevent infinite loops
        // In VIATAhos, TAPE might not support PIO read easily, but we'll try PIO.
        // If it hangs, ata_read_sector will abort.
        ata_read_sector(base, sel, lba, buf);
        
        if (buf[0] == '\0') {
            // Check second block just in case
            ata_read_sector(base, sel, lba + 1, buf);
            if (buf[0] == '\0') break; // Two zero blocks = end of tar
        }
        
        char name[101];
        for (int i=0; i<100; i++) name[i] = buf[i];
        name[100] = '\0';
        
        // Parse octal size
        char size_str[12];
        for (int i=0; i<11; i++) size_str[i] = buf[124 + i];
        size_str[11] = '\0';
        
        uint32_t size = 0;
        for (int i=0; i<11; i++) {
            if (size_str[i] >= '0' && size_str[i] <= '7') {
                size = (size * 8) + (size_str[i] - '0');
            }
        }
        
        vga_print("    |    |-");
        vga_print(name);
        vga_print("\n");
        
        // Advance LBA (1 header block + payload blocks)
        uint32_t blocks = (size + 511) / 512;
        lba += 1 + blocks;
    }
}

#endif
