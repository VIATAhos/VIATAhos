[BITS 16]
[ORG 0x7c00]

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    mov [0x500], dl
    sti

    mov ah, 0x0E
    mov al, 'L'
    int 0x10

    mov cx, 8               
.read_loop:
    push dx
    mov ah, 0x42
    mov si, dap
    int 0x13
    jc disk_error
    pop dx

    mov ax, [dap_segment]
    add ax, 0x0FE0          
    mov [dap_segment], ax
    
    add dword [dap_lba], 127
    loop .read_loop

    jmp 0x0000:stage2

disk_error:
    mov ah, 0x0E
    mov al, 'E'
    int 0x10
    hlt

align 4
dap:
    db 0x10
    db 0
    dw 127
    dw 0x0000
dap_segment:
    dw 0x07E0
dap_lba:
    dq 1

times 446-($-$$) db 0

db 0x80
db 0x20, 0x21, 0x00
db 0x0C
db 0xFE, 0xFF, 0xFF
dd 0x00000800
dd 0x0001F800

times 48 db 0
dw 0xaa55

stage2:
    cli
    lgdt [gdt32_descriptor]
    mov eax, cr0
    or eax, 0x1
    mov cr0, eax
    jmp 0x08:pm_start

[BITS 32]
pm_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x800000

    mov edi, 0x1000
    mov ecx, 6144
    xor eax, eax
    rep stosd

    mov dword [0x1000], 0x2003
    mov dword [0x2000], 0x3003
    mov dword [0x2008], 0x4003
    mov dword [0x2010], 0x5003
    mov dword [0x2018], 0x6003

    mov edi, 0x3000
    mov eax, 0x00000083
    mov ecx, 2048
.build_pd:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    loop .build_pd

    mov eax, 0x1000
    mov cr3, eax

    ; Make the 2MB page at 0x200000 (AHCI DMA buffers) uncacheable
    ; PD entry for 0x200000 is at 0x3008 (PCD=1, PWT=1 -> 0x9B)
    mov dword [0x3008], 0x0020009B

    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    lgdt [gdt64_descriptor]
    jmp 0x08:lm_start

[BITS 64]
lm_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rax, 0x9000
    call rax

.hang:
    cli
    hlt
    jmp .hang

align 8
gdt32_start:
    dd 0x0, 0x0
gdt32_code:
    dw 0xffff, 0x0000, 0x9a00, 0x00cf
gdt32_data:
    dw 0xffff, 0x0000, 0x9200, 0x00cf
gdt32_descriptor:
    dw gdt32_descriptor - gdt32_start - 1
    dd gdt32_start

align 8
gdt64_start:
    dq 0x0
gdt64_code:
    dd 0x00000000, 0x00209a00
gdt64_data:
    dd 0x00000000, 0x00009200
gdt64_descriptor:
    dw gdt64_descriptor - gdt64_start - 1
    dq gdt64_start
