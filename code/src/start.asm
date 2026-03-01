
; ==============================================================================
; BOOTLOADER ENTRY POINT
; ==============================================================================
; This file contains the Multiboot header and the initial assembly entry point
; for the kernel. It sets up the stack and calls the C kernel main function.
; ==============================================================================

bits 32
section .multiboot
    ; Multiboot header (aligned and checksummed)
    ; This header tells the bootloader (like GRUB) that this is a valid kernel.
    align 4
    dd 0x1BADB002         ; Magic number (Multiboot 1)
    dd 0x00               ; Flags (0 means no special requests)
    dd -(0x1BADB002 + 0x00) ; Checksum (Magic + Flags + Checksum must equal 0)

section .text

global start
global read_port
global write_port
global load_idt
global keyboard_handler
global timer_handler
global mouse_handler
extern kmain
extern keyboard_handler_main
extern timer_handler_main
extern mouse_handler_main

; Function: read_port
; Description: Reads a byte from an I/O port.
; Arguments: [esp+4] = port number
read_port:
    mov edx, [esp + 4]
    in al, dx
    ret

; Function: write_port
; Description: Writes a byte to an I/O port.
; Arguments: [esp+4] = port number, [esp+8] = data
write_port:
    mov edx, [esp + 4]
    mov al, [esp + 8]
    out dx, al
    ret

; Function: load_idt
; Description: Loads the Interrupt Descriptor Table (IDT).
; Arguments: [esp+4] = pointer to IDT descriptor
load_idt:
    mov edx, [esp + 4]
    lidt [edx]
    sti                 ; Enable interrupts
    ret

; Function: keyboard_handler
; Description: ISR for keyboard interrupts. Calls C handler.
keyboard_handler:
    pusha
    call keyboard_handler_main
    popa
    iret

; Function: timer_handler
; Description: ISR for timer interrupts. Calls C handler.
timer_handler:
    pusha
    call timer_handler_main
    popa
    iret

; Function: mouse_handler
; Description: ISR for mouse interrupts. Calls C handler.
mouse_handler:
    pusha
    call mouse_handler_main
    popa
    iret

global page_fault_stub
extern page_fault_handler
; Function: page_fault_stub
; Description: ISR for Page Faults (Int 14).
page_fault_stub:
    ; Page Fault pushes an error code automatically onto the stack.
    ; We should save registers here (pusha) if we want to return to the interrupted code safely.
    pusha
    call page_fault_handler
    popa
    add esp, 4 ; Remove error code pushed by CPU
    iret

; ==============================================================================
; KERNEL ENTRY POINT
; ==============================================================================
start:
    ; Disable interrupts until we are ready
    cli

    ; set up the stack pointer
    ; FIX EXPLANATION:
    ; Previously, this was `mov esp, stack_space + 8192`.
    ; The `stack_space` label is at the END of the reserved 8192 bytes (see .bss below).
    ; `resb 8192` reserves bytes *before* the label `stack_space` appears in the file
    ; because `stack_space` is placed *after* the `resb` directive.
    ; So `stack_space` points to the *top* (highest address) of the stack region.
    ; Adding 8192 more bytes (`+ 8192`) pointed ESP way past our reserved memory,
    ; causing it to overlap with `page_directory` or other sections, causing corruption.
    ;
    ; Correct usage: Point ESP to the top of the stack (which grows down).
    ; Since `stack_space` is after the `resb`, it is already at the top.
    mov esp, stack_space
    
    ; DEBUG: write 'X' to 0x3F8 directly
    mov dx, 0x3f8
    mov al, 'X'
    out dx, al
    
    ; Jump to higher level C Kernel
    call kmain

.hang:
    ; If kmain returns, we halt the CPU.
    hlt
    jmp .hang

section .bss
    align 16
    ; Reserve 8KB for the stack
    ; Note: In valid NASM BSS sections, labels usually point to the start.
    ; Here we rely on the label being placed *after* the space.
    ; Memory layout: [ ... 8192 bytes ... ] <--- stack_space label is HERE
    resb 8192
stack_space:

