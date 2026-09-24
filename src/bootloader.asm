; =============================================================================
; bootloader.asm — Multiboot v1 -> x86-64 Long Mode (32-bit part)
;
; GRUB передаёт управление в 32-битном protected mode.
; Структура:
;   _start -> setup_page_tables -> enable_paging -> VGA-тест
;          -> lgdt -> far jump в long_mode_start (kernelloader.asm)
; =============================================================================

MBOOT_MAGIC     equ 0x1BADB002
MBOOT_FLAGS     equ (1 << 0) | (1 << 1)     ; page-align + mem-info
MBOOT_CHECKSUM  equ -(MBOOT_MAGIC + MBOOT_FLAGS)

global _start
extern long_mode_start

global mb_magic
global mb_info
global stack_top

; -----------------------------------------------------------------------------
; Multiboot header
; -----------------------------------------------------------------------------
section .multiboot
align 4
    dd MBOOT_MAGIC
    dd MBOOT_FLAGS
    dd MBOOT_CHECKSUM

; -----------------------------------------------------------------------------
; 32-битный код
; -----------------------------------------------------------------------------
section .boot
[BITS 32]
align 4
_start:
    cli

    ; Сохраняем то, что передал GRUB (пригодится в kernel_main)
    mov [mb_magic], eax
    mov [mb_info],  ebx

    ; Собственный стек
    mov esp, stack_top
    mov ebp, esp

    ; --- Проверка CPUID и наличия Long Mode ---------------------------------
    pushfd
    pop  eax
    mov  ecx, eax
    xor  eax, 1 << 21
    push eax
    popfd
    pushfd
    pop  eax
    xor  eax, ecx
    jz   .no_long_mode

    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb  .no_long_mode

    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29           ; LM bit
    jz   .no_long_mode

    ; --- Настройка страниц и включение пейджинга ---------------------------
    call setup_page_tables
    call enable_paging

    ; --- заливаем VGA-буфер паттерном 'A' ----------------------------
    ; mov edi, 0xB8000
    ; mov eax, 0x2E412E41         ; 'A' + attr 0x2E, дважды в dword
    ; mov ecx, 80 * 25
    ; rep stosd

    ; --- GDT64 и прыжок в long mode ----------------------------------------
    lgdt [gdt64_pointer]
    jmp  0x08:long_mode_start   ; Переход в 64-битный код

.no_long_mode:
    hlt
    jmp .no_long_mode

; -----------------------------------------------------------------------------
; setup_page_tables — строит PML4/PDPT/PD, identity-map первых 1 GiB
; (512 huge pages по 2 МиБ)
; -----------------------------------------------------------------------------
setup_page_tables:
    ; Обнуляем 3 страницы
    mov edi, pml4
    xor eax, eax
    mov ecx, 3 * 1024           ; 3 × 4096 / 4
    rep stosd

    ; PML4[0] -> PDPT
    mov eax, pdpt
    or  eax, 0x3                ; Present | RW
    mov [pml4], eax

    ; PDPT[0] -> PD
    mov eax, pd
    or  eax, 0x3
    mov [pdpt], eax

    ; PD[0..511] -> 2-МиБ huge pages
    mov edi, pd
    mov eax, 0x83               ; Present | RW | PS
    mov ecx, 512
.fill_pd:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    loop .fill_pd

    ret

; -----------------------------------------------------------------------------
; enable_paging — PAE + CR3 + EFER.LME + PG
; -----------------------------------------------------------------------------
enable_paging:
    ; PAE
    mov eax, cr4
    or  eax, 1 << 5
    mov cr4, eax

    ; CR3 = PML4
    mov eax, pml4
    mov cr3, eax

    ; EFER.LME = 1
    mov ecx, 0xC0000080
    rdmsr
    or  eax, 1 << 8
    wrmsr

    ; PG = 1
    mov eax, cr0
    or  eax, 1 << 31
    mov cr0, eax

    ret

; -----------------------------------------------------------------------------
section .rodata
align 8
gdt64:
    dq 0x0000000000000000       ; 0x00: null
    dq 0x00AF9A000000FFFF       ; 0x08: 64-bit code (L=1)
    dq 0x00CF92000000FFFF       ; 0x10: data

gdt64_pointer:
    dw $ - gdt64 - 1
    dq gdt64                    ; 10 байт: в 32-бит PM lgdt прочитает 6

; -----------------------------------------------------------------------------
section .data
align 8
mb_magic: dd 0
mb_info:  dd 0

; -----------------------------------------------------------------------------
section .pagetables nobits alloc noexec nowrite
align 4096
pml4: resb 4096
pdpt: resb 4096
pd:   resb 4096

align 16
stack_bottom:
    resb 16384
stack_top:

; -----------------------------------------------------------------------------
section .bss
;

; -----------------------------------------------------------------------------
section .note.GNU-stack noalloc noexec nowrite progbits