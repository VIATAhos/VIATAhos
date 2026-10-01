/* =========================================================================
   AHCI Driver — VIATAhos
   Supports SATA drives via AHCI HBA (PCI class 0x01, subclass 0x06).
   Uses identity-mapped physical addresses in long mode.
   ========================================================================= */
#ifndef AHCI_H
#define AHCI_H

#include <stdint.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------
   PCI helpers (implemented in kernel.c, declared here)
   ------------------------------------------------------------------------- */
extern uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
static inline void pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000u);
    outl(0xCF8, address);
    outl(0xCFC, val);
}

/* -------------------------------------------------------------------------
   AHCI HBA Memory Registers (ABAR = BAR5)
   ------------------------------------------------------------------------- */
#define AHCI_GHC_AE      (1u << 31)   /* AHCI Enable          */
#define AHCI_GHC_HR      (1u << 0)    /* HBA Reset            */
#define AHCI_PxCMD_ST    (1u << 0)    /* Start                */
#define AHCI_PxCMD_FRE   (1u << 4)    /* FIS Receive Enable   */
#define AHCI_PxCMD_FR    (1u << 14)   /* FIS Receive Running  */
#define AHCI_PxCMD_CR    (1u << 15)   /* Command List Running */
#define AHCI_PxIS_TFES   (1u << 30)   /* Task File Error      */
#define AHCI_PxTFD_BSY   (1u << 7)
#define AHCI_PxTFD_DRQ   (1u << 3)
#define AHCI_SIG_ATA     0x00000101u  /* SATA disk            */
#define AHCI_SIG_ATAPI   0xEB140101u  /* ATAPI device         */
#define AHCI_MAX_PORTS   32

/* HBA port registers (offset from ABAR + 0x100 + port*0x80) */
typedef volatile struct {
    uint32_t clb;       /* Command List Base Address (low)  */
    uint32_t clbu;      /* Command List Base Address (high) */
    uint32_t fb;        /* FIS Base Address (low)           */
    uint32_t fbu;       /* FIS Base Address (high)          */
    uint32_t is;        /* Interrupt Status                 */
    uint32_t ie;        /* Interrupt Enable                 */
    uint32_t cmd;       /* Command and Status               */
    uint32_t _res0;
    uint32_t tfd;       /* Task File Data                   */
    uint32_t sig;       /* Signature                        */
    uint32_t ssts;      /* SATA Status                      */
    uint32_t sctl;      /* SATA Control                     */
    uint32_t serr;      /* SATA Error                       */
    uint32_t sact;      /* SATA Active                      */
    uint32_t ci;        /* Command Issue                    */
    uint32_t sntf;      /* SATA Notification                */
    uint32_t fbs;       /* FIS-based Switching Control      */
    uint32_t _res1[11];
    uint32_t vendor[4];
} ahci_port_t;

/* HBA generic host registers */
typedef volatile struct {
    uint32_t cap;       /* HBA Capabilities                */
    uint32_t ghc;       /* Global HBA Control              */
    uint32_t is;        /* Interrupt Status                */
    uint32_t pi;        /* Ports Implemented               */
    uint32_t vs;        /* AHCI Version                    */
    uint32_t ccc_ctl;
    uint32_t ccc_pts;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t cap2;
    uint32_t bohc;
    uint8_t  _res[0xA0 - 0x2C];
    uint8_t  vendor[0x60];
    ahci_port_t ports[32];
} ahci_hba_t;

/* Command header (32 bytes each, 32 slots per port) */
typedef volatile struct {
    uint16_t opts;      /* bits: PRDTL[15:0], PMP[15:12], C, B, R, P, W, A, CFL[4:0] */
    uint16_t prdtl;     /* PRDT length (entries)           */
    uint32_t prdbc;     /* PRDT byte count transferred     */
    uint32_t ctba;      /* Command Table Base Address (low)  */
    uint32_t ctbau;     /* Command Table Base Address (high) */
    uint32_t _res[4];
} ahci_cmd_hdr_t;

/* Physical Region Descriptor Table entry (16 bytes) */
typedef volatile struct {
    uint32_t dba;       /* Data Base Address (low)         */
    uint32_t dbau;      /* Data Base Address (high)        */
    uint32_t _res;
    uint32_t dbc;       /* Byte Count (bit 0 must be 1)    */
} ahci_prdt_entry_t;

/* Command Table */
typedef volatile struct {
    uint8_t           cfis[64];     /* Command FIS                   */
    uint8_t           acmd[16];     /* ATAPI Command                 */
    uint8_t           _res[48];
    ahci_prdt_entry_t prdt[1];      /* Physical Region Descriptor    */
} ahci_cmd_tbl_t;

/* -------------------------------------------------------------------------
   Statically allocated DMA buffers (identity-mapped)
   We put them at a fixed physical address region the kernel controls.
   In our simple no-paging setup, stack/kernel is below 4MB; we use
   2MB..3MB for AHCI structures.
   ------------------------------------------------------------------------- */
static uint8_t ahci_cmd_list_buf[32 * 1024] __attribute__((aligned(4096)));
static uint8_t ahci_fis_buf[32 * 256] __attribute__((aligned(4096)));
static uint8_t ahci_cmd_tbl_buf[32 * 8192] __attribute__((aligned(4096)));
static uint8_t ahci_sec_buf[32 * 8192] __attribute__((aligned(4096)));

#define AHCI_CMD_LIST_BASE  ((uint64_t)(uintptr_t)ahci_cmd_list_buf)
#define AHCI_FIS_BASE       ((uint64_t)(uintptr_t)ahci_fis_buf)
#define AHCI_CMD_TBL_BASE   ((uint64_t)(uintptr_t)ahci_cmd_tbl_buf)
#define AHCI_BUF_BASE       ((uint64_t)(uintptr_t)ahci_sec_buf)

static uint64_t  ahci_abar   = 0;
static int       ahci_port   = -1;
static uint32_t ahci_active_ports = 0;        /* first usable SATA disk port */
static bool      ahci_ready  = false;

/* -------------------------------------------------------------------------
   Volatile MMIO helpers
   ------------------------------------------------------------------------- */
static inline uint32_t ahci_rd32(uint64_t addr) {
    return *(volatile uint32_t *)(uintptr_t)addr;
}
static inline void ahci_wr32(uint64_t addr, uint32_t val) {
    *(volatile uint32_t *)(uintptr_t)addr = val;
}
static inline void ahci_mem_set(uint64_t addr, uint8_t val, uint32_t n) {
    volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)addr;
    for (uint32_t i = 0; i < n; i++) p[i] = val;
}

/* Port base address */
static inline uint64_t ahci_port_base(int p) {
    return ahci_abar + 0x100ULL + (uint64_t)p * 0x80ULL;
}

/* -------------------------------------------------------------------------
   Stop / Start port DMA engine
   ------------------------------------------------------------------------- */
static void ahci_port_stop(int p) {
    uint64_t base = ahci_port_base(p);
    uint32_t cmd = ahci_rd32(base + 0x18);
    /* Clear ST and FRE */
    cmd &= ~(AHCI_PxCMD_ST | AHCI_PxCMD_FRE);
    ahci_wr32(base + 0x18, cmd);
    /* Wait for FR and CR to clear */
    for (uint32_t i = 0; i < 500000u; i++) {
        cmd = ahci_rd32(base + 0x18);
        if (!(cmd & (AHCI_PxCMD_FR | AHCI_PxCMD_CR))) break;
    }
}

static void ahci_port_start(int p) {
    uint64_t base = ahci_port_base(p);
    /* Wait for CR to clear before setting ST */
    for (uint32_t i = 0; i < 500000u; i++) {
        if (!(ahci_rd32(base + 0x18) & AHCI_PxCMD_CR)) break;
    }
    uint32_t cmd = ahci_rd32(base + 0x18);
    cmd |= AHCI_PxCMD_FRE | AHCI_PxCMD_ST;
    ahci_wr32(base + 0x18, cmd);
}

/* -------------------------------------------------------------------------
   Send a command and wait
   ------------------------------------------------------------------------- */
static bool ahci_send_cmd(int p, int slot) {
    uint64_t base = ahci_port_base(p);

    /* Wait for port to be idle (not BSY/DRQ) */
    for (uint32_t i = 0; i < 1000000u; i++) {
        uint32_t tfd = ahci_rd32(base + 0x20);
        if (!(tfd & (AHCI_PxTFD_BSY | AHCI_PxTFD_DRQ))) break;
        if (i == 999999u) return false;
    }

    /* Issue command */
    ahci_wr32(base + 0x38, (1u << slot)); /* CI */

     /* Wait for completion */
    for (uint32_t i = 0; i < 10000000u; i++) {
        if (!(ahci_rd32(base + 0x38) & (1u << slot))) break;
        if (ahci_rd32(base + 0x10) & AHCI_PxIS_TFES) {  return false; }
        if (i == 9999999u) {  return false; }
    }
    
    return !(ahci_rd32(base + 0x10) & AHCI_PxIS_TFES);
}

/* -------------------------------------------------------------------------
   IDENTIFY DEVICE command
   ------------------------------------------------------------------------- */
bool ahci_identify(int port, uint16_t *id_buf) {
    if (!ahci_ready) return false;
    int p = port;
    int slot = 0;

    ahci_mem_set((AHCI_CMD_LIST_BASE + (uint64_t)p * 1024u), 0, 32);
    ahci_mem_set((AHCI_CMD_TBL_BASE + (uint64_t)p * 8192u), 0, sizeof(ahci_cmd_tbl_t));

    volatile uint8_t *cfis = (volatile uint8_t *)(uintptr_t)(AHCI_CMD_TBL_BASE + (uint64_t)p * 8192u);
    cfis[0]  = 0x27;              /* FIS type: Register H2D      */
    cfis[1]  = 0x80;              /* C bit set (command register) */
    cfis[2]  = 0xEC;              /* ATA command: IDENTIFY DEVICE */

    volatile ahci_prdt_entry_t *prdt =
        (volatile ahci_prdt_entry_t *)(uintptr_t)(AHCI_CMD_TBL_BASE + (uint64_t)p * 8192u + 0x80);
    prdt->dba  = (uint32_t)((AHCI_BUF_BASE + (uint64_t)p * 8192u) & 0xFFFFFFFFu);
    prdt->dbau = (uint32_t)((AHCI_BUF_BASE + (uint64_t)p * 8192u) >> 32);
    prdt->dbc  = 511u;

    volatile ahci_cmd_hdr_t *hdr =
        (volatile ahci_cmd_hdr_t *)(uintptr_t)(AHCI_CMD_LIST_BASE + (uint64_t)p * 1024u);
    hdr->opts  = 5; 
    hdr->prdtl = 1;
    hdr->prdbc = 0;
    hdr->ctba  = (uint32_t)((AHCI_CMD_TBL_BASE + (uint64_t)p * 8192u) & 0xFFFFFFFFu);
    hdr->ctbau = (uint32_t)((AHCI_CMD_TBL_BASE + (uint64_t)p * 8192u) >> 32);

    uint64_t pbase = ahci_port_base(p);
    ahci_wr32(pbase + 0x10, ahci_rd32(pbase + 0x10));

    if (!ahci_send_cmd(p, slot)) return false;

    volatile uint8_t *src = (volatile uint8_t *)(uintptr_t)(AHCI_BUF_BASE + (uint64_t)p * 8192u);
    uint8_t *dst = (uint8_t*)id_buf;
    for (int i = 0; i < 512; i++) dst[i] = src[i];

    return true;
}

/* -------------------------------------------------------------------------
   Build a READ DMA command (48-bit LBA)
   ------------------------------------------------------------------------- */
bool ahci_read_sector(int port, uint64_t lba, uint8_t *buf) {
    
    if (!ahci_ready || ahci_port < 0) return false;

    int p = port;
    int slot = 0;

     /* Clear command list slot */
    ahci_mem_set((AHCI_CMD_LIST_BASE + (uint64_t)p * 1024u), 0, 32);

     /* Clear command table */
    ahci_mem_set((AHCI_CMD_TBL_BASE + (uint64_t)p * 8192u), 0, sizeof(ahci_cmd_tbl_t));

     /* Build Register-H2D FIS (27 bytes, type 0x27) in cfis */
    volatile uint8_t *cfis = (volatile uint8_t *)(uintptr_t)(AHCI_CMD_TBL_BASE + (uint64_t)p * 8192u);
    cfis[0]  = 0x27;              /* FIS type: Register H2D      */
    cfis[1]  = 0x80;              /* C bit set (command register) */
    cfis[2]  = 0x25;              /* ATA command: READ DMA EXT   */
    cfis[3]  = 0x00;              /* Features (low)              */
    cfis[4]  = (uint8_t)(lba);        /* LBA (7:0)              */
    cfis[5]  = (uint8_t)(lba >> 8);   /* LBA (15:8)             */
    cfis[6]  = (uint8_t)(lba >> 16);  /* LBA (23:16)            */
    cfis[7]  = 0x40;              /* Device: LBA mode            */
    cfis[8]  = (uint8_t)(lba >> 24);  /* LBA (31:24)            */
    cfis[9]  = (uint8_t)(lba >> 32);  /* LBA (39:32)            */
    cfis[10] = (uint8_t)(lba >> 40);  /* LBA (47:40)            */
    cfis[11] = 0x00;              /* Features (high)             */
    cfis[12] = 1;                 /* Sector count (low)          */
    cfis[13] = 0;                 /* Sector count (high)         */

     /* PRDT entry: 512 bytes into AHCI_BUF_BASE */
    volatile ahci_prdt_entry_t *prdt =
        (volatile ahci_prdt_entry_t *)(uintptr_t)(AHCI_CMD_TBL_BASE + (uint64_t)p * 8192u + 0x80);
    prdt->dba  = (uint32_t)((AHCI_BUF_BASE + (uint64_t)p * 8192u) & 0xFFFFFFFFu);
    prdt->dbau = (uint32_t)((AHCI_BUF_BASE + (uint64_t)p * 8192u) >> 32);
    prdt->dbc  = 511u; /* byte count - 1 */

    /* Command header: 5 DWORDs FIS, write=0, PRDTL=1 */
    volatile ahci_cmd_hdr_t *hdr =
        (volatile ahci_cmd_hdr_t *)(uintptr_t)(AHCI_CMD_LIST_BASE + (uint64_t)p * 1024u);
    hdr->opts  = 5; /* FIS length=5 DWORDs, read=0 */
    hdr->prdtl = 1;
    hdr->prdbc = 0;
    hdr->ctba  = (uint32_t)((AHCI_CMD_TBL_BASE + (uint64_t)p * 8192u) & 0xFFFFFFFFu);
    hdr->ctbau = (uint32_t)((AHCI_CMD_TBL_BASE + (uint64_t)p * 8192u) >> 32);

    /* Clear port interrupt status */
    uint64_t pbase = ahci_port_base(p);
    ahci_wr32(pbase + 0x10, ahci_rd32(pbase + 0x10));

    if (!ahci_send_cmd(p, slot)) return false;

    /* Copy from DMA buffer to caller buffer */
    volatile uint8_t *src = (volatile uint8_t *)(uintptr_t)(AHCI_BUF_BASE + (uint64_t)p * 8192u);
    for (int i = 0; i < 512; i++) buf[i] = src[i];
    return true;
}

/* -------------------------------------------------------------------------
   Write one 512-byte sector via AHCI DMA (WRITE DMA EXT, cmd 0x35)
   ------------------------------------------------------------------------- */
bool ahci_write_sector(int port, uint64_t lba, const uint8_t *buf) {
    if (!ahci_ready || ahci_port < 0) return false;

    int p = port;
    int slot = 0;

    /* Copy data into DMA buffer first */
    volatile uint8_t *dst = (volatile uint8_t *)(uintptr_t)(AHCI_BUF_BASE + (uint64_t)p * 8192u);
    for (int i = 0; i < 512; i++) dst[i] = buf[i];

    ahci_mem_set((AHCI_CMD_LIST_BASE + (uint64_t)p * 1024u), 0, 32);
    ahci_mem_set((AHCI_CMD_TBL_BASE + (uint64_t)p * 8192u), 0, sizeof(ahci_cmd_tbl_t));

    volatile uint8_t *cfis = (volatile uint8_t *)(uintptr_t)(AHCI_CMD_TBL_BASE + (uint64_t)p * 8192u);
    cfis[0]  = 0x27;
    cfis[1]  = 0x80;
    cfis[2]  = 0x35;              /* WRITE DMA EXT */
    cfis[4]  = (uint8_t)(lba);
    cfis[5]  = (uint8_t)(lba >> 8);
    cfis[6]  = (uint8_t)(lba >> 16);
    cfis[7]  = 0x40;
    cfis[8]  = (uint8_t)(lba >> 24);
    cfis[9]  = (uint8_t)(lba >> 32);
    cfis[10] = (uint8_t)(lba >> 40);
    cfis[12] = 1;

    volatile ahci_prdt_entry_t *prdt =
        (volatile ahci_prdt_entry_t *)(uintptr_t)(AHCI_CMD_TBL_BASE + (uint64_t)p * 8192u + 0x80);
    prdt->dba  = (uint32_t)((AHCI_BUF_BASE + (uint64_t)p * 8192u) & 0xFFFFFFFFu);
    prdt->dbau = (uint32_t)((AHCI_BUF_BASE + (uint64_t)p * 8192u) >> 32);
    prdt->dbc  = 511u;

    volatile ahci_cmd_hdr_t *hdr =
        (volatile ahci_cmd_hdr_t *)(uintptr_t)(AHCI_CMD_LIST_BASE + (uint64_t)p * 1024u);
    hdr->opts  = 5 | (1 << 6); /* W bit set for write */
    hdr->prdtl = 1;
    hdr->prdbc = 0;
    hdr->ctba  = (uint32_t)((AHCI_CMD_TBL_BASE + (uint64_t)p * 8192u) & 0xFFFFFFFFu);
    hdr->ctbau = (uint32_t)((AHCI_CMD_TBL_BASE + (uint64_t)p * 8192u) >> 32);

    uint64_t pbase = ahci_port_base(p);
    ahci_wr32(pbase + 0x10, ahci_rd32(pbase + 0x10));

    return ahci_send_cmd(p, slot);
}

/* -------------------------------------------------------------------------
   Initialize AHCI: find HBA via PCI, pick first SATA disk port
   Returns true if at least one disk was found.
   ------------------------------------------------------------------------- */
static bool ahci_init(void) {
    ahci_ready = false;
    ahci_port  = -1;

    /* Scan PCI for AHCI controller: class=0x01 subclass=0x06 */
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            uint32_t id = pci_read_config((uint8_t)bus, (uint8_t)slot, 0, 0x00);
            if ((id & 0xFFFF) == 0xFFFF) continue; /* no device */

            uint32_t cls = pci_read_config((uint8_t)bus, (uint8_t)slot, 0, 0x08);
            uint8_t base_class = (uint8_t)(cls >> 24);
            uint8_t sub_class  = (uint8_t)(cls >> 16);
            if (base_class != 0x01 || sub_class != 0x06) continue;

            /* Found AHCI HBA — read ABAR (BAR5) */
            uint32_t bar5 = pci_read_config((uint8_t)bus, (uint8_t)slot, 0, 0x24);
            if (!bar5 || (bar5 & 1)) continue; /* not MMIO */
            ahci_abar = (uint64_t)(bar5 & 0xFFFFF000u);

            /* Enable AHCI mode and bus mastering */
            uint32_t cmd_reg = pci_read_config((uint8_t)bus, (uint8_t)slot, 0, 0x04);
            pci_write_config((uint8_t)bus, (uint8_t)slot, 0, 0x04,
                             cmd_reg | 0x0006u); /* Memory Space + Bus Master */

            /* Enable AHCI global */
             uint32_t ghc = ahci_rd32(ahci_abar + 0x04); 
            ahci_wr32(ahci_abar + 0x04, ghc | AHCI_GHC_AE);

            /* Scan implemented ports */
            uint32_t pi = ahci_rd32(ahci_abar + 0x0C);
            for (int p = 0; p < AHCI_MAX_PORTS; p++) {
                if (!(pi & (1u << p))) continue;

                uint64_t pbase = ahci_port_base(p);
                uint32_t ssts  = ahci_rd32(pbase + 0x28);
                uint8_t  det   = ssts & 0x0F;
                uint8_t  ipm   = (ssts >> 8) & 0x0F;
                if (det != 3 || ipm != 1) continue; /* not active SATA */

                uint32_t sig = ahci_rd32(pbase + 0x24);
                if (sig != AHCI_SIG_ATA) continue; /* only plain SATA disks */

                /* Stop port, set up command list and FIS buffers */
                ahci_port_stop(p);

                uint64_t cl_addr  = AHCI_CMD_LIST_BASE + (uint64_t)p * 1024u;
                uint64_t fis_addr = AHCI_FIS_BASE      + (uint64_t)p * 256u;
                ahci_mem_set(cl_addr,  0, 1024);
                ahci_mem_set(fis_addr, 0, 256);

                ahci_wr32(pbase + 0x00, (uint32_t)(cl_addr  & 0xFFFFFFFFu));
                ahci_wr32(pbase + 0x04, (uint32_t)(cl_addr  >> 32));
                ahci_wr32(pbase + 0x08, (uint32_t)(fis_addr & 0xFFFFFFFFu));
                ahci_wr32(pbase + 0x0C, (uint32_t)(fis_addr >> 32));

                /* Clear errors and IS */
                ahci_wr32(pbase + 0x30, 0xFFFFFFFFu);
                ahci_wr32(pbase + 0x10, 0xFFFFFFFFu);

                ahci_port_start(p);

                if (ahci_port < 0) ahci_port = p;
                ahci_active_ports |= (1u << p);
            }

            if (ahci_port >= 0) { ahci_ready = true; return true; }
        }
    }
    return false;
}

#endif /* AHCI_H */
