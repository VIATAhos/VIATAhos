#ifndef SHADOW_H
#define SHADOW_H

#include <stdint.h>
#include "fat32.h"


typedef struct {
    uint8_t  permissions; 
    uint16_t gui_pos_x;
    uint16_t gui_pos_y;
    char     icon_name[16];
    char     tag[16];
    char     comment[64];
} __attribute__((packed)) viata_metadata_t;

static inline void get_shadow_metadata(const char* filename, viata_metadata_t* meta) {
    char shadow_path[64] = "\\.viata\\";
    str_cat(shadow_path, filename);
    str_cat(shadow_path, ".meta");

    
    meta->permissions = 0x07; 
    meta->gui_pos_x = 100;
    meta->gui_pos_y = 100;
    str_cpy(meta->icon_name, "default.ico");
    str_cpy(meta->tag, "Generico");
    str_cpy(meta->comment, "File gestito da VIATAhos");
}

static inline void save_shadow_metadata(const char* filename, const viata_metadata_t* meta) {
    
    (void)filename;
    (void)meta;
}

#endif 
