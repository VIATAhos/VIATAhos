# VIATAhos Dual Architecture Makefile

SRC_DIRS = src/core src/drivers src/fs src/utils
C_SRCS = $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
HDRS = $(foreach dir,src src/boot src/core src/drivers src/fs src/utils,$(wildcard $(dir)/*.h $(dir)/*.inc))

# --- X86 (32-bit) Configuration ---
CC_X86 = i686-elf-gcc
LD_X86 = i686-elf-ld
AS_X86 = nasm

CFLAGS_X86 = -m32 -march=i686 -mgeneral-regs-only -mno-mmx -mno-sse -mno-sse2 -fno-asynchronous-unwind-tables -ffreestanding -O2 -Wall -Wextra -fno-pie -fno-stack-protector -nostdlib -I./src -I./src/boot -I./src/drivers -I./src/fs -I./src/core -I./src/utils
LDFLAGS_X86 = -m elf_i386 -T linker.ld -nostdlib

C_OBJS_X86 = $(patsubst src/%.c,build/x86/%.o,$(C_SRCS))
OBJS_X86 = build/x86/boot/kernel_entry_x86.o $(C_OBJS_X86)

# --- X64 (64-bit) Configuration ---
CC_X64 = x86_64-elf-gcc
LD_X64 = x86_64-elf-ld
AS_X64 = nasm

CFLAGS_X64 = -m64 -mcmodel=large -mno-red-zone -mgeneral-regs-only -mno-mmx -mno-sse -mno-sse2 -fno-asynchronous-unwind-tables -ffreestanding -O2 -Wall -Wextra -fno-pie -fno-stack-protector -nostdlib -I./src -I./src/boot -I./src/drivers -I./src/fs -I./src/core -I./src/utils
LDFLAGS_X64 = -m elf_x86_64 -T linker.ld -nostdlib

C_OBJS_X64 = $(patsubst src/%.c,build/x64/%.o,$(C_SRCS))
OBJS_X64 = build/x64/boot/kernel_entry_x64.o $(C_OBJS_X64)

# --- Targets ---
.PHONY: all clean run_x86 run_x64 viatahos_v1_x64.img

all: viatahos_x86.img viatahos_x64.img viatahos_v1.img viatahos_v1_x64.img

# --- X86 Build Rules ---
build/x86/%.o: src/%.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC_X86) $(CFLAGS_X86) -c $< -o $@

build/x86/boot/kernel_entry_x86.o: src/boot/kernel_entry_x86.asm
	@mkdir -p build/x86/boot
	$(AS_X86) -f elf32 $< -o $@

build/x86/boot.bin: src/boot/boot_x86.asm
	@mkdir -p build/x86
	$(AS_X86) -f bin $< -o $@

build/x86/kernel.bin: $(OBJS_X86)
	$(LD_X86) $(LDFLAGS_X86) -o $@ $(OBJS_X86)

viatahos_x86.img: build/x86/boot.bin build/x86/kernel.bin
	@echo "=> Generating x86 (32-bit) disk image..."
	@dd if=/dev/zero of=$@ bs=512 count=131072 2>/dev/null
	@dd if=build/x86/boot.bin of=$@ conv=notrunc 2>/dev/null
	@dd if=build/x86/kernel.bin of=$@ bs=512 seek=10 conv=notrunc 2>/dev/null

# --- X64 Build Rules ---
build/x64/%.o: src/%.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC_X64) $(CFLAGS_X64) -c $< -o $@

build/x64/boot/kernel_entry_x64.o: src/boot/kernel_entry_x64.asm
	@mkdir -p build/x64/boot
	$(AS_X64) -f elf64 $< -o $@

build/x64/boot.bin: src/boot/boot_x64.asm
	@mkdir -p build/x64
	$(AS_X64) -f bin $< -o $@

build/x64/kernel.bin: $(OBJS_X64)
	$(LD_X64) $(LDFLAGS_X64) -o $@ $(OBJS_X64)

viatahos_x64.img: build/x64/boot.bin build/x64/kernel.bin
	@echo "=> Generating x64 (64-bit) disk image..."
	@dd if=/dev/zero of=$@ bs=512 count=131072 2>/dev/null
	@dd if=build/x64/boot.bin of=$@ conv=notrunc 2>/dev/null
	@dd if=build/x64/kernel.bin of=$@ bs=512 seek=10 conv=notrunc 2>/dev/null

# --- Utilities ---
clean:
	rm -rf build viatahos*.img

viatahos_v1.img:
	@echo "=> Creating secondary virtual disk V1 (16MB)..."
	@dd if=/dev/zero of=viatahos_v1.img bs=512 count=32768 2>/dev/null

viatahos_v1_x64.img:
	@dd if=/dev/zero of=viatahos_v1_x64.img bs=512 count=32768 2>/dev/null

run_x86: viatahos_x86.img viatahos_v1.img
	qemu-system-i386 -device ahci,id=ahci0 -drive id=disk0,file=viatahos_x86.img,if=none,format=raw -device ide-hd,drive=disk0,bus=ahci0.0 -drive id=disk1,file=viatahos_v1.img,if=none,format=raw -device ide-hd,drive=disk1,bus=ahci0.1 -m 128M -boot c  -serial file:serial.log -display cocoa,zoom-to-fit=on

run_x64: viatahos_x64.img viatahos_v1_x64.img
	qemu-system-x86_64 -device ahci,id=ahci0 -drive id=disk0,file=viatahos_x64.img,if=none,format=raw -device ide-hd,drive=disk0,bus=ahci0.0 -drive id=disk1,file=viatahos_v1_x64.img,if=none,format=raw -device ide-hd,drive=disk1,bus=ahci0.1 -m 128M -boot c  -serial file:serial.log -display cocoa,zoom-to-fit=on
