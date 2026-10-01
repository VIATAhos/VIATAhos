# VIATAhos

<div align="center">

### The Independently Developed, Human-Driven, Homebrew Operating System

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Architectures](https://img.shields.io/badge/Architectures-x86%20%7C%20x64-teal.svg)](https://github.com/VIATAhos/VIATAhos)
[![Status](https://img.shields.io/badge/Version-v1.0%20AlpDev%201.0.0%20(Alpha)-orange.svg)](https://github.com/VIATAhos/VIATAhos)
[![Website](https://img.shields.io/badge/Website-VIATAhos.github.io-008080.svg)](https://VIATAhos.github.io)

</div>

---

## 🖥️ What is VIATAhos?

**VIATAhos** is a bare-metal, monolithic operating system engineered **completely from scratch**.

It contains zero lines of code from Linux, Minix, BSD, Windows, or MS-DOS. It is designed to restore absolute digital control and hardware ownership to users, rejecting telemetry, planned obsolescence, and black-box abstractions.

- **Monolithic Ring 0 Kernel:** Direct, unfiltered access to hardware registers and memory.
- **Dual-Architecture Support:** Built-in boot pipelines for **32-bit x86** and **64-bit x64 Long Mode**.
- **T-CORE Multitasking:** 12 concurrent, independent terminal sessions accessible instantly via **F1–F12**.
- **Dual Storage Engine:** Legacy **ATA/IDE PIO** mode + native high-performance **AHCI SATA** bus mastering.
- **Physical Drive Letter Matrix:** Fixed physical hardware mapping (`A:` Floppy, `B:` Optical, `C:` Removable USB, `D:` Hard Disk, `E:` Tape, `F:` RAM Disk).
- **FAT32 + Shadow Files (`\.viata\`):** 100% Plug & Play interoperability with Windows, Linux, and macOS while preserving rich system metadata.
- **SMOL LZ77 Compression:** Native compression envelope (`.smol`) supporting on-the-fly decompression.
- **Integrated Full-Screen Text Editor (`di`):** In-kernel multi-line text editor with automatic `.smol` compression.
- **VGA QR-Code Kernel Panic:** Diagnostic engine rendering scannable QR codes directly on CRT/LCD text displays.

---

## 📂 Repository Structure

```
VIATAhos/
├── LICENSE                 # GNU General Public License v3.0
├── Makefile                # Dual-architecture (x86 / x64) build pipeline
├── linker.ld               # Kernel linker script (0x9000 text, 1MB BSS)
├── setup_images.sh         # Helper to generate floppy, CD-ROM, and tape test media
├── .gitignore              # Ignores build artifacts, disk images, and logs
├── README.md               # Repository documentation and quick start
├── OSinfo/                 # Official documentation
│   ├── comandi_os.txt      # Official Complete Commands Manual
│   └── guida_viatahos.md   # Architectural overview & programmer's guide
└── src/                    # Kernel Source Tree
    ├── boot/               # Bootloaders & entry points
    │   ├── boot_x86.asm    # 32-bit Protected Mode MBR bootloader (INT 13h LBA)
    │   ├── boot_x64.asm    # 64-bit Long Mode MBR bootloader (4-level paging)
    │   ├── kernel_entry_x86.asm # BSS clearing and C call (x86)
    │   └── kernel_entry_x64.asm # BSS clearing, serial heartbeat, C call (x64)
    ├── core/               # Kernel core & shell engine
    │   ├── kernel.c        # Main kernel loop, IDT, session dispatch, ACPI
    │   ├── commands.inc    # T-CORE shell command processor
    │   ├── exec.h          # File execution contract (.xep, .kvbn, .smol)
    │   ├── qr_code.h       # VGA text mode QR code bitmap generator
    │   └── version.h       # Version configuration
    ├── drivers/            # Bare-metal hardware drivers
    │   ├── io.h            # In-line assembly for port I/O (inb, outb, inw, outw, inl, outl)
    │   ├── vga.h / vga.c   # 80x25 VGA driver, 12-session history, DPMS blanking
    │   ├── ata.h           # ATA/ATAPI PIO mode driver (IDE Primary/Secondary)
    │   └── ahci.h          # Native PCI AHCI SATA controller driver
    ├── fs/                 # Filesystem implementations
    │   ├── fat32.h         # FAT32 driver (format, read, write, trash, tree)
    │   ├── shadow.h        # VIATA Shadow Files metadata architecture (\.viata\)
    │   ├── fat_legacy.h    # FAT12 / FAT16 legacy floppy parser
    │   ├── iso9660.h       # ISO 9660 optical disc directory parser
    │   └── tar.h           # Sequential TAR archive tape reader
    └── utils/              # Algorithms, strings & credits
        ├── lz77.h          # SMOL LZ77/LZSS compression engine & Fletcher-16
        ├── string.h / .c   # Freestanding string manipulation routines
        ├── tools.h         # Removable drive auto-provisioning
        └── by.txt          # Contributor roster & authorship registry
```

---

## ⚡ Quick Start

### 1. Prerequisites

Install the bare-metal cross-compilers, NASM, and QEMU:

```bash
# macOS (using Homebrew):
brew install i686-elf-gcc x86_64-elf-gcc nasm qemu

# Ubuntu / Debian:
sudo apt install gcc-multilib nasm qemu-system-x86
```

### 2. Building the Kernel

To compile the kernel and generate bootable disk images for both architectures:

```bash
make all
```

This compiles `src/` and generates:
- `viatahos_x86.img` (64 MB bootable 32-bit disk image)
- `viatahos_x64.img` (64 MB bootable 64-bit Long Mode disk image)
- `viatahos_v1.img` / `viatahos_v1_x64.img` (16 MB secondary data disks)

### 3. Running under QEMU

Run the **32-bit x86** build:
```bash
make run_x86
```

Run the **64-bit x64 Long Mode** build:
```bash
make run_x64
```

To clean all compiled objects and generated images:
```bash
make clean
```

---

## ⌨️ T-CORE Commands & Usage

All shell commands, operational syntax, drive prompt hierarchies, and keybindings must be learned directly from the official commands manual:

📖 **[`OSinfo/comandi_os.txt`](OSinfo/comandi_os.txt)** — *Lista Completa dei Comandi di Sistema*

---

## 📖 System Documentation

The official system documentation is located in the `OSinfo/` directory:
- [Manuale Ufficiale dei Comandi](OSinfo/comandi_os.txt) — The definitive reference for all system commands, drive hierarchies, double-extension rules, and utilities.
- [Guida del Programmatore & Architettura Hardware](OSinfo/guida_viatahos.md) — Comprehensive technical reference covering hardware specifications, memory models, file contracts, and Anti-UNIX philosophy.

---

## 🤝 Contributing

We welcome contributions from independent developers, low-level programmers, and digital sovereignty advocates.

### The Mandatory `by.txt` Rule
To preserve authorship and ensure everyone receives credit for their work:
1. Every Pull Request **must** add the contributor's name to [`src/utils/by.txt`](src/utils/by.txt).
2. Pull Requests without an updated `by.txt` entry will not be merged.

Format:
```
AQUILtheeagle - Founder - 2026
```

---

## 📄 License

VIATAhos is released under the **GNU General Public License Version 3 (GPLv3)**.

You are free to run, study, modify, and redistribute this software. Any derivative works or distributions must also be released under the GNU GPL v3 license with complete source code provided. Proprietary forks are strictly prohibited.

See the complete [LICENSE](LICENSE) file for legal details.

---

<div align="center">
  <sub>Designed and built in-house. A reflection of people, shaped by their own hands.<br>Free forever.</sub>
</div>
