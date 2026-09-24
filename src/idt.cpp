#include "idt.h"

static IDTEntry g_idt[256];
static IDTR     g_idtr;

extern "C" void isr0();
extern "C" void isr6();
extern "C" void isr13();
extern "C" void isr14();
extern "C" void isr29();

// Внешний вывод для отладки
extern "C" void hv_debug(const char* msg);

extern "C" void isr0_handler()  { hv_debug("#DE - divide error");   for (;;) __asm__ volatile("hlt"); }
extern "C" void isr6_handler()  { hv_debug("#UD - invalid opcode"); for (;;) __asm__ volatile("hlt"); }
extern "C" void isr13_handler() { hv_debug("#GP - general protection"); for (;;) __asm__ volatile("hlt"); }
extern "C" void isr14_handler() { hv_debug("#PF - page fault");     for (;;) __asm__ volatile("hlt"); }
extern "C" void isr29_handler() { hv_debug("#VC - VMM communication"); for (;;) __asm__ volatile("hlt"); }

static void set_entry(int idx, uint64_t handler) {
    g_idt[idx].offset_low  = (uint16_t)(handler & 0xFFFF);
    g_idt[idx].selector    = 0x08;
    g_idt[idx].ist         = 0;
    g_idt[idx].type_attr   = 0x8E;
    g_idt[idx].offset_mid  = (uint16_t)((handler >> 16) & 0xFFFF);
    g_idt[idx].offset_high = (uint32_t)((handler >> 32) & 0xFFFFFFFF);
    g_idt[idx].zero        = 0;
}

extern "C" void idt_init() {
    for (int i = 0; i < 256; i++) {
        g_idt[i].offset_low  = 0;
        g_idt[i].selector    = 0;
        g_idt[i].ist         = 0;
        g_idt[i].type_attr   = 0;
        g_idt[i].offset_mid  = 0;
        g_idt[i].offset_high = 0;
        g_idt[i].zero        = 0;
    }

    set_entry(0,  (uint64_t)isr0);
    set_entry(6,  (uint64_t)isr6);
    set_entry(13, (uint64_t)isr13);
    set_entry(14, (uint64_t)isr14);
    set_entry(29, (uint64_t)isr29);

    g_idtr.limit = sizeof(g_idt) - 1;
    g_idtr.base  = (uint64_t)&g_idt;

    __asm__ volatile("lidt %0" : : "m"(g_idtr));
}