[BITS 32]
global _start
extern kernel_main
extern bss_start
extern bss_end

section .text
_start:
    ; Zero out the BSS section so all static variables start at 0
    mov edi, bss_start
    mov ecx, bss_end
    sub ecx, edi
    xor al, al
    cld
    rep stosb

    call kernel_main
    cli
.hang:
    hlt
    jmp .hang
