; =============================================================================
; kernelloader.asm — 64-битная точка входа в Long Mode + ISR + VM-Exit stub
; =============================================================================

global long_mode_start
extern kernel_main
extern _bss_start
extern _bss_end
extern mb_magic
extern mb_info
extern stack_top

section .text
bits 64

long_mode_start:
    mov ax, 0x10
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov rsp, stack_top
    xor rbp, rbp

    ; Обнуление .bss
    mov rdi, _bss_start
    mov rcx, _bss_end
    sub rcx, rdi
    xor al, al
    rep stosb

    mov edi, [mb_magic]
    mov esi, [mb_info]

    call kernel_main

.hang:
    hlt
    jmp .hang

; --- ISR Stubs ---
global isr0
global isr6
global isr13
global isr14
global isr29
extern isr0_handler
extern isr6_handler
extern isr13_handler
extern isr14_handler
extern isr29_handler

isr0:
    call isr0_handler
    iretq
isr6:
    call isr6_handler
    iretq
isr13:
    call isr13_handler
    iretq
isr14:
    call isr14_handler
    iretq
isr29:
    call isr29_handler
    iretq

; --- Заглушка VM-Exit ---
global vm_exit_stub
vm_exit_stub:
    cli
.hang_vmx:
    hlt
    jmp .hang_vmx

; =============================================================================
section .bss
align 4096

global vmxon_region_storage
global vmcs_region_storage
global ept_pages

vmxon_region_storage: resb 4096
vmcs_region_storage:  resb 4096
ept_pages:            resb 4096 * 4