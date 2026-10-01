#ifndef TOOLS_H
#define TOOLS_H

#include "fat32.h"
#include "vga.h"

static const char readme_txt[] = 
    "=== VIATAhos Companion Tools ===\n"
    "Usa 'unsmol' per estrarre i file .smol su sistemi operativi esterni:\n\n"
    "Windows:   unsmol.exe foto.png.smol -> foto.png\n"
    "macOS:     ./unsmol_mac foto.png.smol -> foto.png\n"
    "Linux:     ./unsmol_linux foto.png.smol -> foto.png\n";

static inline void provision_removable_drive(char drive_letter, const char* sub_drive) {
    
    if (sub_drive[0] != 'R') return;

    vga_print("[REMOVABLE] Inizializzazione drive ");
    vga_putchar(drive_letter);
    vga_print(":");
    vga_print(sub_drive);
    vga_print("\n[PROVISIONING] Creazione \\VIATA_TOOLS\\ ed estrazione unsmol binaries...
");

    
    
    
    
    
}

#endif 
