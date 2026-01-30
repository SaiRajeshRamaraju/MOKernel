
bits 32
section .multiboot
    ; Multiboot header (aligned and checksummed)
    align 4
    dd 0x1BADB002         ; Magic number
    dd 0x00               ; Flags
    dd -(0x1BADB002 + 0x00) ; Checksum

section .text

global start
global read_port
global write_port
global load_idt
global keyboard_handler
extern kmain
extern keyboard_handler_main

read_port:
    mov edx, [esp + 4]
    in al, dx
    ret

write_port:
    mov edx, [esp + 4]
    mov al, [esp + 8]
    out dx, al
    ret

load_idt:
    mov edx, [esp + 4]
    lidt [edx]
    sti
    ret

keyboard_handler:
    call keyboard_handler_main
    iret

global page_fault_stub
extern page_fault_handler
page_fault_stub:
    ; Page Fault pushes an error code automatically
    ; We might want to save registers here , I don't know what is going on
    call page_fault_handler
    add esp, 4 ; Remove error code
    iret

start:
    cli
    mov esp, stack_space + 8192
    call kmain
.hang:
    hlt
    jmp .hang

section .bss
    align 16
    resb 8192
stack_space:

