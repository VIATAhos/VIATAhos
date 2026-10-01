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
├── README.md               # Repository documentation & Official Commands Manual
├── OSinfo/                 # Additional system documentation
│   ├── comandi_os.txt      # Extended command ideas & future additions reference
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
    │   └── version.h       # Version configuration (v1.0 AlpDev 1.0.0)
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

## 📖 OFFICIAL T-CORE COMMANDS MANUAL

This section is the **definitive user and operational manual** for all native commands implemented inside the VIATAhos kernel (`src/core/commands.inc`).

Commands prefixed with an asterisk (`*`) require administrative authentication or active Kernel Native Mode (`kn`).

### 🎮 Global Navigation & Keyboard Controls

- **`F1` – `F12`**: Switch instantly between **12 concurrent terminal sessions**. Each session maintains its own working directory, active user, shell prompt, and independent 100-line history buffer.
- **`Up Arrow` / `Down Arrow`**: Scroll upward or downward through the active session's 100-line scrollback buffer.
- **`Left Arrow` / `Right Arrow`**: In-place cursor movement inside the active command line.
- **`Backspace`**: Delete character at cursor position.
- **`Enter`**: Submit command for execution.

### 📦 The Double Extension Rule (`.smol`)

VIATAhos treats `.smol` as a **secondary compression envelope** that does not replace the original file extension:
- If a file is compressed, its extension format is `ext.smol name` (e.g. `xep.smol calcolatrice` or `txt.smol diario`).
- When launching a `.smol` executable with `xp`, the kernel transparently decompresses the file into RAM before execution.
- When opening a `.smol` text file with the editor `di`, the editor decompresses it on read and automatically recompresses it on save.

### 💽 Drive Letter Hierarchy

Prompts reflect the physical drive and permission level:
- `A:>` : Floppy Disk (Read-only OS volume)
- `B:>` : Optical CD/DVD/Blu-Ray (ISO 9660 volume)
- `C:>` : Removable Storage (USB Flash / SD Card, FAT32)
- `D:>` : Fixed Internal Drive (HDD / SSD / NVMe, FAT32)
- `E:>` : Magnetic Tape Unit (TAR sequential archive)
- `F:>` : Virtual RAM Disk (Boot volume)
- `:KN>`: Suffix indicating active **Kernel Native (Admin)** mode.

---

### 1. File & Directory Management

#### `ft` — File System Navigation & Tree
Explores the FAT32 directory structure, displaying cluster numbers, file attributes, and exact byte sizes.
- `ft` : Lists all files and subdirectories in the current folder.
- `ft <ext>` : Filters and lists only files matching the given extension (e.g. `ft txt` or `ft xep`).
- `ft dir <name>` : Navigates into the specified subdirectory (e.g. `ft dir docs`).
- `ft ..` : Navigates to the parent directory.

#### `mk` — Create File or Directory
Allocates a new record in the FAT32 table.
- `mk dir <name>` : Creates a new directory (e.g. `mk dir projects`).
- `mk <ext> <name>` : Creates an empty file with the given extension (e.g. `mk txt notes` or `mk xep app`).

#### `di` — Full-Screen Text Editor
Opens the integrated multi-line screen editor (supports up to 1024 lines × 80 columns).
- `di <ext> <name>` : Opens the specified file (e.g. `di txt notes` or `di txt.smol diario`).
- **Editor Controls:**
  - `Arrow Keys` : Move cursor and scroll view.
  - `Enter` : Split line; `Backspace` : Delete character or merge line with upper line.
  - `Tab` : Insert 4 indentation spaces.
  - `ESC` or `F4` : Save changes to disk (automatically compresses if filename ends with `.smol`).
  - `Shift + ESC` : Save changes and immediately exit back to the shell.

#### `dl` — Delete File (Move to Recycle Bin)
Safely moves a file to the system Recycle Bin (`\.trash\`).
- `dl <ext> <name>` : Moves the specified file to the trash folder (e.g. `dl txt draft`).

#### `do` — Rename or Reformat File
Updates the directory record to rename a file or change its extension.
- `do <ext1> <name1> <ext2> <name2>` : Renames `<name1>.<ext1>` to `<name2>.<ext2>` (e.g. `do txt old doc new`).

#### `cf` — Copy File to Clipboard
Copies the specified file to the system-wide clipboard buffer.
- `cf <ext> <name>` : Copies the file (e.g. `cf txt report`).

#### `xf` — Cut File to Clipboard
Marks the specified file to be moved upon pasting.
- `xf <ext> <name>` : Cuts the file (e.g. `xf txt draft`).

#### `vf` — Paste File from Clipboard
Pastes the clipboard file into the current working directory.
- `vf` : Pastes the file retaining its original name.
- `vf <ext> <name>` : Pastes the file under a new name and extension (e.g. `vf txt copy`).

#### `qd` — Quick Data Append
Appends a line of text directly to a file on disk without opening the editor.
- `qd <ext> <name> ><text>` : Appends `<text>` to the file (e.g. `qd txt log >Kernel booted successfully`).

#### `fs` — Stream Command Output to File
Executes any shell command and redirects its text output directly into a file.
- `fs <ext> <name> <command>` : Runs `<command>` and writes output to `<name>.<ext>` (e.g. `fs txt sysinfo sys`).

#### `smol` — LZ77 File Compression & Extraction
Invokes the native SMOL compression engine.
- `smol <ext> <name>` : Compresses `<name>.<ext>` into `<name>.<ext>.smol` using LZ77.
- `smol smol <name>.<ext>` : Decompresses the file back to its original uncompressed state.

#### `srcsys` — Deep System Search
Recursively searches the storage volume for files, folders, or text strings.
- `srcsys <ext> <name>` : Finds all files matching the name and extension (e.g. `srcsys txt config`).
- `srcsys dir <name>` : Finds all directories matching `<name>` (e.g. `srcsys dir backup`).
- `srcsys "<text>"` : Scans inside text files and returns all occurrences containing `<text>`.

---

### 2. Recycle Bin Operations (`bnsys`)

#### `bnsys` — List Recycle Bin Contents
Displays all deleted files currently preserved in the `\.trash\` directory.
- `bnsys` : Lists items with original filenames and deletion records.

#### `unbn` — Restore File from Recycle Bin
Restores a previously deleted file back to the active working directory.
- `unbn <ext> <name>` : Restores the file (e.g. `unbn txt draft`).

#### `* dlbn` — Permanent File Shredding
Irreversibly deletes a file from the Recycle Bin by zeroing out its directory entry on disk.
- `dlbn <ext> <name>` : Permanently destroys the file (e.g. `dlbn txt secret`).

#### `* clbn` — Securely Empty Recycle Bin
Purges and sanitizes all sectors allocated to the Recycle Bin.
- `clbn` : Asks for confirmation (`Y/N`) and wipes the trash directory.

#### `srcbn` — Search Inside Recycle Bin
Searches specifically inside the deleted file records.
- `srcbn <text>` : Searches for deleted items containing `<text>` (e.g. `srcbn draft`).

---

### 3. Execution & Automation

#### `xp` — Execute Binary or Script
Loads and executes a program.
- `xp <ext> <name>` : Runs the program (e.g. `xp xep game` or `xp kvbn startup`).
- If the file has a `.smol` extension (e.g. `xp xep.smol game`), the kernel automatically uncompresses it into memory before running.

#### `rpt` — Repeat Command Loop
Executes a shell command repeatedly for a specified number of iterations.
- `rpt(<command>)<count>` : Repeats `<command>` `<count>` times (e.g. `rpt(wt)5`).

#### `w` — Timed Pause / Wait
Suspends shell execution for a deterministic duration.
- `w <seconds>` : Pauses for the given number of seconds (e.g. `w 5`).

---

### 4. User Accounts & Security

#### `* mksys` — Create System User
Initializes a new user account profile.
- `mksys` : Prompts interactively for a new username and password.

#### `* dlsys` — Delete User Account
Removes an account from the system users registry.
- `dlsys` : Prompts for confirmation and deletes the current account.

#### `* sysnm` — Rename User Account
Updates the active account's username.
- `sysnm` : Prompts for the new username.

#### `* syspw` — Change Password
Updates the account password.
- `syspw` : Prompts for the current password, the new password, and confirmation.

#### `* chgsys` — Logout
Logs out of the active user session, returning to the login prompt.
- `chgsys` : Disconnects the current user.

#### `* kn` — Toggle Kernel Native Mode (Admin)
Enables or disables persistent administrative privilege.
- `kn` : Toggles mode. When active, prompt displays `:KN` and all administrative commands bypass password verification.

#### `nm` — Toggle User Welcome Banner
Toggles the username display in the top system banner.
- `nm` : Switches banner user display on or off.

#### `shnm` — Toggle Shell Name Prompt
Toggles displaying the shell name inside the command prompt.
- `shnm` : Enables or disables shell name in prompt.

#### `chgnm` — Rename Active Terminal Session
Assigns a custom name to the active T-CORE session (F1–F12).
- `chgnm <name>` : Sets the session label (e.g. `chgnm Compiler`).

---

### 5. Display & Visual Customization

#### `cl` — Clear Screen
Clears the active session's screen buffer and resets the cursor to the top.
- `cl` : Clears the screen.

#### `cr` — Set Screen Colors
Customizes text and background colors using the 16 unlocked VGA palette colors (`black`, `blue`, `green`, `cyan`, `red`, `magenta`, `brown`, `light_gray`, `dark_gray`, `light_blue`, `light_green`, `light_cyan`, `light_red`, `light_magenta`, `yellow`, `white`).
- `cr` : Resets display to default colors.
- `cr <fg_color> <bg_color>` : Sets foreground and background colors (e.g. `cr green black` or `cr white blue`).

---

### 6. Hardware Diagnostics & Metrics

#### `sys` — System & CPU Information
Queries processor and memory hardware directly via CPUID and system tables.
- `sys` : Reports CPU vendor, model, 32-bit x86 or 64-bit x64 mode, clock rate, and detected RAM.

#### `wt` — Real-Time Date & Clock
Reads and displays the current calendar date and time from the CMOS Real-Time Clock (RTC).
- `wt` : Displays timestamp.

#### `upwt` — System Uptime
Displays elapsed time since kernel boot in hours, minutes, and seconds.
- `upwt` : Prints uptime counter.

#### `wtw` — Command Execution Duration
Reports the exact execution duration (stopwatch) of the last executed command.
- `wtw` : Displays elapsed seconds.

#### `hs` — Command History
Displays the list of recently executed commands in the active session.
- `hs` : Prints session history.

#### `by&` — Project Credits & Authorship
Displays the official project developer roster loaded directly from `src/utils/by.txt`.
- `by&` : Renders credits.

#### `h` — Display Help Summary
Prints the clean list of all available commands and their general purpose.
- `h` : Displays help list.

---

### 7. Power Management & Disk Maintenance

#### `kpt` — System Suspend (Screen Lock)
Powers down the monitor signal via VGA DPMS and locks the console.
- `kpt` : Turns off screen. Requires pressing a key and entering the account password to unlock.

#### `* scr` — Inactivity Auto-Suspend Timer
Configures automatic screen suspension after a period of keyboard inactivity.
- `scr <minutes>` : Sets timeout duration (e.g. `scr 10`).

#### `* rebsys` — Hardware Reboot
Triggers a processor hardware reset via the 8042 keyboard controller pulse (`outb 0x64, 0xFE`), with ACPI fallback.
- `rebsys` : Reboots the machine.

#### `* shtsys` — Complete Power Down
Shuts down machine power by sending the `_S5` sleep state command to the ACPI power controller (`outw 0x604, 0x2000` / `outw 0xB004, 0x2000`).
- `shtsys` : Turns off computer power.

#### `* updk` — Format Secondary Storage
Performs low-level FAT32 formatting on the connected secondary disk.
- `updk` : Prompts confirmation and initializes a fresh FAT32 volume.

#### `fixfs` — File System Verification
Performs consistency checks on FAT32 allocation tables and directory structures.
- `fixfs` : Analyzes and reports filesystem health.

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
