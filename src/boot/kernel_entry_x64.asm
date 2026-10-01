[BITS 64]
global _start
extern kernel_main
extern bss_start
extern bss_end

section .text
_start:
    mov dx, 0x3F8
    mov al, 'Y'
    out dx, al
    and rsp, 0xFFFFFFFFFFFFFFF0

    ; Zero out the BSS section so all static variables start at 0
    mov rdi, bss_start
    mov rcx, bss_end
    sub rcx, rdi
    xor al, al
    cld
    rep stosb

    call kernel_main
    cli
.hang:
    hlt
    jmp .hang
