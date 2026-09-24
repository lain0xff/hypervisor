#include "idt.h"
#include "vmx.h"
#include "ept.h"

static int g_debug_row = 6;

extern "C" void hv_debug(const char* msg) {
    volatile char* vga = (volatile char*)0xB8000;
    int row = g_debug_row;
    if (row > 24) row = 24;
    g_debug_row++;
    for (int i = 0; msg[i] && i < 79; ++i) {
        vga[(row * 80 + i) * 2]     = msg[i];
        vga[(row * 80 + i) * 2 + 1] = 0x0E;   // жёлтый
    }
}

static void vga_print(int row, const char* msg, uint8_t color) {
    volatile char* vga = (volatile char*)0xB8000;
    for (int i = 0; msg[i]; ++i) {
        vga[(row * 80 + i) * 2]     = msg[i];
        vga[(row * 80 + i) * 2 + 1] = color;
    }
}

extern "C" void kernel_main(unsigned int magic, unsigned int mb_info) {
    (void)magic;
    (void)mb_info;

    vga_print(0, "Hypervisor: long mode OK", 0x0A);

    idt_init();
    vga_print(1, "IDT initialized", 0x0A);

    if (!ept_init()) {
        vga_print(2, "EPT init failed", 0x0C);
        for (;;) __asm__ volatile("hlt");
    }
    vga_print(2, "EPT initialized", 0x0A);

    hv_debug("Calling vmx_init...");
    if (!vmx_init()) {
        vga_print(3, "VMX init failed", 0x0C);
        for (;;) __asm__ volatile("hlt");
    }
    vga_print(3, "VMXON OK", 0x0A);

    hv_debug("Calling vmx_setup_vmcs...");
    if (!vmx_setup_vmcs()) {
        vga_print(4, "VMCS setup failed", 0x0C);
        for (;;) __asm__ volatile("hlt");
    }
    vga_print(4, "VMCS configured", 0x0A);

    vga_print(5, "Hypervisor ready. Halting.", 0x0E);
    for (;;) __asm__ volatile("hlt");
}