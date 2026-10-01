#ifndef ATA_H
#define ATA_H

#include <stdint.h>
#include <stdbool.h>
#include "io.h"

/* =========================================================================
   ATA PIO driver — VIATAhos
   Supports Primary (0x1F0) and Secondary (0x170) IDE buses.
   ========================================================================= */

/* --- ATA register offsets from base --- */
#define ATA_DATA        0
#define ATA_ERR         1
#define ATA_SECCOUNT    2
#define ATA_LBA_LO      3
#define ATA_LBA_MID     4
#define ATA_LBA_HI      5
#define ATA_DEVSEL      6
#define ATA_CMD         7   
#define ATA_STATUS      7

/* --- Status bits --- */
#define ATA_SR_BSY  0x80
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

/* --- Commands --- */
#define ATA_CMD_PACKET 0xA0
#define ATA_CMD_READ     0x20
#define ATA_CMD_WRITE    0x30
#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_IDENTIFY_ATAPI 0xA1

/* --- ATAPI device type codes (word 0 bits 12-8) --- */
#define ATAPI_TYPE_FLOPPY 0x00   
#define ATAPI_TYPE_TAPE   0x01   
#define ATAPI_TYPE_CD     0x05   
#define ATAPI_TYPE_OPTIC  0x07   

/* --- Identify word indices --- */
#define ATA_ID_W0_DEVTYPE  0    
#define ATA_ID_W60_SECTORS 60   
#define ATA_ID_W61_SECTORS 61   
#define ATA_ID_MODEL_START 27   
#define ATA_ID_MODEL_END   46

/* -------------------------------------------------------------------------
   Small ATA I/O delay: read the alternate status register 4x
   ------------------------------------------------------------------------- */
static inline void ata_io_delay(uint16_t base) {
    inb(base + ATA_STATUS);
    inb(base + ATA_STATUS);
    inb(base + ATA_STATUS);
    inb(base + ATA_STATUS);
}

/* -------------------------------------------------------------------------
   ata_wait_bsy — wait for BSY to clear with timeout
   Returns false if timed out.
   ------------------------------------------------------------------------- */
static inline bool ata_wait_bsy_timeout(uint16_t base) {
    /* Short poll — used ONLY during drive probe (IDENTIFY).
       Empty IDE slots must be skipped quickly so boot stays fast.
       ~2M iterations = a few milliseconds max per slot. */
    for (uint32_t i = 0; i < 200000u; i++) {
        if (!(inb(base + ATA_STATUS) & ATA_SR_BSY)) return true;
    }
    return false;
}

/* -------------------------------------------------------------------------
   ata_identify — query a drive slot via IDENTIFY DEVICE or IDENTIFY PACKET
   base      : 0x1F0 (primary) or 0x170 (secondary)
   dev_sel   : 0xA0 (master) or 0xB0 (slave)
   buf       : caller-provided 256-word (512-byte) buffer
   Returns   : true if a drive is present and identification succeeded.
   is_atapi  : set to true if the device is ATAPI (CD/DVD/tape/floppy-like)
   atapi_type: ATAPI peripheral device type (valid only when is_atapi=true)
   ------------------------------------------------------------------------- */
static bool ata_identify(uint16_t base, uint8_t dev_sel,
                         uint16_t *buf, bool *is_atapi, uint8_t *atapi_type)
{
    *is_atapi  = false;
    *atapi_type = 0;

    
    outb(base + ATA_DEVSEL, dev_sel);
    ata_io_delay(base);

    
    if (inb(base + ATA_STATUS) == 0xFF) return false;

    outb(base + ATA_SECCOUNT, 0);
    outb(base + ATA_LBA_LO,   0);
    outb(base + ATA_LBA_MID,  0);
    outb(base + ATA_LBA_HI,   0);
    outb(base + ATA_CMD,      ATA_CMD_IDENTIFY);
    ata_io_delay(base);

    
    if (inb(base + ATA_STATUS) == 0) return false;

    
    if (!ata_wait_bsy_timeout(base)) return false;

    
    uint8_t mid = inb(base + ATA_LBA_MID);
    uint8_t hi  = inb(base + ATA_LBA_HI);

    if (mid == 0x14 && hi == 0xEB) {
        
        *is_atapi = true;
        outb(base + ATA_CMD, ATA_CMD_IDENTIFY_ATAPI);
        ata_io_delay(base);
        if (!ata_wait_bsy_timeout(base)) return false;
    } else if (mid != 0 || hi != 0) {
        
        return false;
    }

    
    uint32_t t = 0x100000u;
    while (--t) {
        uint8_t st = inb(base + ATA_STATUS);
        if (st & ATA_SR_DRQ) break;
        if (st & ATA_SR_ERR) return false;
    }
    if (!t) return false;

    
    for (int i = 0; i < 256; i++)
        buf[i] = inw(base + ATA_DATA);

    
    if (*is_atapi)
        *atapi_type = (uint8_t)((buf[0] >> 8) & 0x1F);

    return true;
}


/* -------------------------------------------------------------------------
   ATAPI Read Capacity (Command 0x25)
   ------------------------------------------------------------------------- */
static inline uint32_t atapi_read_capacity(uint16_t base, uint8_t dev_sel) {
    if (base == 0xFFFF || base == 0xAC1u) return 0;
    
    outb(base + ATA_DEVSEL, dev_sel);
    ata_io_delay(base);
    
    outb(base + 1, 0);
    outb(base + ATA_LBA_MID, 8); // 8 bytes for capacity data
    outb(base + ATA_LBA_HI,  0);
    outb(base + ATA_CMD, 0xA0); // PACKET
    ata_io_delay(base);
    
    // Wait for BSY to clear and DRQ to set
    uint32_t t = 0x100000u;
    while (--t) {
        uint8_t st = inb(base + ATA_STATUS);
        if ((st & (ATA_SR_BSY | ATA_SR_DRQ)) == ATA_SR_DRQ) break;
        if (st & ATA_SR_ERR) return 0;
    }
    if (!t) return 0;
    
    // Send ATAPI Packet: 12 bytes
    uint8_t pkt[12] = { 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    uint16_t *p = (uint16_t*)pkt;
    for (int i=0; i<6; i++) outw(base + ATA_DATA, p[i]);
    
    // Wait for DRQ
    t = 0x100000u;
    while (--t) {
        uint8_t st = inb(base + ATA_STATUS);
        if ((st & (ATA_SR_BSY | ATA_SR_DRQ)) == ATA_SR_DRQ) break;
        if (st & ATA_SR_ERR) return 0;
    }
    if (!t) return 0;
    
    // Read 8 bytes
    uint16_t data[4];
    for (int i=0; i<4; i++) data[i] = inw(base + ATA_DATA);
    
    // Data is big endian!
    // Byte 0-3: LBA (blocks)
    uint32_t blocks = ((uint32_t)(data[0] & 0xFF) << 24) |
                      ((uint32_t)(data[0] >> 8) << 16) |
                      ((uint32_t)(data[1] & 0xFF) << 8) |
                      ((uint32_t)(data[1] >> 8));
                      
    return blocks; // Return number of blocks. Usually 2048 bytes per block.
}

/* -------------------------------------------------------------------------
   Standard PIO read/write — primary bus, master drive, 28-bit LBA
   ------------------------------------------------------------------------- */

extern void kernel_panic(const char*, const char*, int);
static inline void ata_wait_bsy(uint16_t base) {
    for (uint32_t i = 0; i < 10000000u; i++) {
        if (!(inb(base + ATA_STATUS) & ATA_SR_BSY)) return;
    }
    for (uint32_t i = 0; i < 20000000u; i++) {
        if (!(inb(base + ATA_STATUS) & ATA_SR_BSY)) return;
        ata_io_delay(base);
    }
    /* Timed out: silently return. On real hardware, a stuck BSY usually means
       the controller has stalled — returning lets the caller handle the error
       via ATA_SR_ERR, instead of crashing the whole OS. */
}
static inline void ata_wait_drq(uint16_t base) {
    for (uint32_t i = 0; i < 10000000u; i++) {
        uint8_t status = inb(base + ATA_STATUS);
        if (status & ATA_SR_DRQ) return;
        if (status & ATA_SR_ERR) return;
    }
    for (uint32_t i = 0; i < 20000000u; i++) {
        uint8_t status = inb(base + ATA_STATUS);
        if (status & ATA_SR_DRQ) return;
        if (status & ATA_SR_ERR) return;
        ata_io_delay(base);
    }
    /* Timed out: silently return — caller will read 0xFF/ERR bytes */
}



static inline void atapi_read_sector(uint16_t base, uint8_t dev_sel, uint32_t lba, uint8_t *buf) {
    if (base == 0xFFFF) { return; }
    
    // Select drive
    outb(base + ATA_DEVSEL, dev_sel);
    ata_io_delay(base);
    
    // Features = 0, Sector count = 0, LBA = 0
    outb(base + 1, 0); 
    outb(base + 2, 0);
    outb(base + 3, 0);
    // Byte count limit to 2048
    outb(base + 4, 2048 & 0xFF);
    outb(base + 5, 2048 >> 8);
    outb(base + 7, ATA_CMD_PACKET);
    ata_io_delay(base);
    
    // Wait for DRQ
    uint32_t timeout = 10000000;
    while (1) {
        uint8_t status = inb(base + ATA_STATUS);
        if (status & ATA_SR_DRQ) break;
        if (status & ATA_SR_ERR) return; 
        if (--timeout == 0) return;
    }
    
    // Send ATAPI Packet (12 bytes)
    uint8_t packet[12] = {0xA8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    packet[2] = (lba >> 24) & 0xFF;
    packet[3] = (lba >> 16) & 0xFF;
    packet[4] = (lba >> 8) & 0xFF;
    packet[5] = lba & 0xFF;
    packet[9] = 1; // 1 sector

    uint16_t *ptr = (uint16_t *)packet;
    for (int i = 0; i < 6; i++) {
        outw(base + 0, ptr[i]);
    }
    
    // Wait for DRQ to read data
    timeout = 10000000;
    while (1) {
        uint8_t status = inb(base + ATA_STATUS);
        if (status & ATA_SR_DRQ) break;
        if (status & ATA_SR_ERR) return;
        if (--timeout == 0) return;
    }
    
    // Read 2048 bytes (1024 words)
    uint16_t *buf_ptr = (uint16_t *)buf;
    for (int i = 0; i < 1024; i++) {
        buf_ptr[i] = inw(base + 0);
    }
}


/* ATA_BASE_AHCI sentinel defined in kernel.c — forward-declared here */
#ifndef ATA_BASE_AHCI
#define ATA_BASE_AHCI 0xAC1u
#endif

extern bool ahci_read_sector(int port, uint64_t lba, uint8_t *buf);
extern bool ahci_write_sector(int port, uint64_t lba, const uint8_t *buf);


#define RAMDISK_SECTORS 6144
uint8_t ramdisk_data[RAMDISK_SECTORS * 512];

static inline void ata_read_sector(uint16_t base, uint8_t dev_sel, uint32_t lba, uint8_t *buf) {
    if (base == 0xFFFF) { return; }
    if (base == 0xEEEE) {
        if (lba < RAMDISK_SECTORS) {
            uint8_t *src = ramdisk_data + (lba * 512);
            for (int i=0; i<512; i++) buf[i] = src[i];
        }
        return;
    }
    if (base == 0xAC1u) {
        /* Route through AHCI DMA — dev_sel holds the AHCI port number */
        ahci_read_sector((int)dev_sel, (uint64_t)lba, buf);
        return;
    }
    ata_wait_bsy(base);
    outb(base + ATA_DEVSEL, dev_sel | 0x40 | ((lba >> 24) & 0x0F));
    outb(base + 2, 1);
    outb(base + 3, (uint8_t)lba);
    outb(base + 4, (uint8_t)(lba >> 8));
    outb(base + 5, (uint8_t)(lba >> 16));
    outb(base + 7, ATA_CMD_READ);
    ata_wait_bsy(base);
    ata_wait_drq(base);
    uint16_t *ptr = (uint16_t *)buf;
    for (int i = 0; i < 256; i++)
        ptr[i] = inw(base + 0);
}

static inline void ata_write_sector(uint16_t base, uint8_t dev_sel, uint32_t lba, const uint8_t *buf) {
    if (base == 0xFFFF) { return; }
    if (base == 0xEEEE) {
        if (lba < RAMDISK_SECTORS) {
            uint8_t *dst = ramdisk_data + (lba * 512);
            for (int i=0; i<512; i++) dst[i] = buf[i];
        }
        return;
    }
    if (base == 0xAC1u) {
        ahci_write_sector((int)dev_sel, (uint64_t)lba, buf);
        return;
    }
    ata_wait_bsy(base);
    outb(base + ATA_DEVSEL, dev_sel | 0x40 | ((lba >> 24) & 0x0F));
    outb(base + 2, 1);
    outb(base + 3, (uint8_t)lba);
    outb(base + 4, (uint8_t)(lba >> 8));
    outb(base + 5, (uint8_t)(lba >> 16));
    outb(base + 7, ATA_CMD_WRITE);
    ata_wait_bsy(base);
    ata_wait_drq(base);
    const uint16_t *ptr = (const uint16_t *)buf;
    for (int i = 0; i < 256; i++)
        outw(base + 0, ptr[i]);
}

#endif

