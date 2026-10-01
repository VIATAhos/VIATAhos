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

    ; Jump to kernel loaded at 0x9000
    mov eax, 0x9000
    call eax

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
