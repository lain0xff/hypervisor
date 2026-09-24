#include "vmx.h"
#include "ept.h"

// Объявления из kernelloader.asm
extern "C" void     vm_exit_stub();
extern "C" uint8_t  vmxon_region_storage[4096];
extern "C" uint8_t  vmcs_region_storage[4096];
extern "C" void hv_debug(const char* msg);

// MSRs
#define IA32_FEATURE_CONTROL        0x3A
#define IA32_VMX_BASIC              0x480
#define IA32_VMX_PINBASED_CTLS      0x481
#define IA32_VMX_PROCBASED_CTLS     0x482
#define IA32_VMX_EXIT_CTLS          0x483
#define IA32_VMX_ENTRY_CTLS         0x484
#define IA32_VMX_PROCBASED_CTLS2    0x48B
#define IA32_EFER                   0xC0000080

// VMCS field encodings
#define VMCS_CTRL_PIN_BASED         0x4000
#define VMCS_CTRL_PROC_BASED        0x4002
#define VMCS_CTRL_EXIT              0x400C
#define VMCS_CTRL_ENTRY             0x4012
#define VMCS_CTRL_PROC_BASED2       0x401E
#define VMCS_CTRL_EPT_POINTER       0x201A
#define VMCS_HOST_CR0               0x6C00
#define VMCS_HOST_CR3               0x6C02
#define VMCS_HOST_CR4               0x6C04
#define VMCS_HOST_RSP               0x6C14
#define VMCS_HOST_RIP               0x6C16
#define VMCS_HOST_IA32_EFER         0x2C02
#define VMCS_HOST_CS_SEL            0x0C02
#define VMCS_HOST_SS_SEL            0x0C04
#define VMCS_HOST_DS_SEL            0x0C06
#define VMCS_HOST_ES_SEL            0x0C00
#define VMCS_HOST_FS_SEL            0x0C08
#define VMCS_HOST_GS_SEL            0x0C0A

#define VMCS_GUEST_CR0              0x6800
#define VMCS_GUEST_CR3              0x6802
#define VMCS_GUEST_CR4              0x6804
#define VMCS_GUEST_RSP              0x681C
#define VMCS_GUEST_RIP              0x681E
#define VMCS_GUEST_RFLAGS           0x6820
#define VMCS_GUEST_IA32_EFER        0x2806
#define VMCS_GUEST_CS_SEL           0x0802
#define VMCS_GUEST_DS_SEL           0x0806
#define VMCS_GUEST_ES_SEL           0x0800
#define VMCS_GUEST_SS_SEL           0x0804
#define VMCS_GUEST_FS_SEL           0x0808
#define VMCS_GUEST_GS_SEL           0x080A
#define VMCS_GUEST_CS_LIMIT         0x4802
#define VMCS_GUEST_DS_LIMIT         0x4806
#define VMCS_GUEST_ES_LIMIT         0x4800
#define VMCS_GUEST_SS_LIMIT         0x4804
#define VMCS_GUEST_CS_ACCESS        0x4816
#define VMCS_GUEST_SS_ACCESS        0x4818
#define VMCS_GUEST_DS_ACCESS        0x481A
#define VMCS_GUEST_ES_ACCESS        0x481C
#define VMCS_GUEST_GDTR_BASE        0x6816
#define VMCS_GUEST_GDTR_LIMIT       0x4810
#define VMCS_GUEST_IDTR_BASE        0x6818
#define VMCS_GUEST_IDTR_LIMIT       0x4812

static uint64_t g_vmxon_phys = 0;
static uint64_t g_vmcs_phys  = 0;

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t v) {
    __asm__ volatile("wrmsr" : : "c"(msr),
                     "a"((uint32_t)v), "d"((uint32_t)(v >> 32)));
}

static bool cpu_has_vmx() {
    uint32_t eax = 1, ebx = 0, ecx = 0, edx = 0;
    __asm__ volatile("cpuid" : "+a"(eax), "+b"(ebx), "+c"(ecx), "+d"(edx));
    return (ecx & (1u << 5)) != 0;
}

uint64_t vmcs_read(uint64_t field) {
    uint64_t v = 0;
    __asm__ volatile("vmread %1, %0" : "=r"(v) : "r"(field) : "cc");
    return v;
}

void vmcs_write(uint64_t field, uint64_t value) {
    __asm__ volatile("vmwrite %0, %1" : : "r"(value), "r"(field) : "cc");
}

static uint64_t adjust_ctrl(uint32_t msr, uint64_t desired) {
    uint64_t caps = rdmsr(msr);
    uint32_t allowed1 = (uint32_t)(caps >> 32);
    uint32_t must_be1 = (uint32_t)(caps & 0xFFFFFFFF);
    return (uint32_t)((desired | must_be1) & allowed1);
}

bool vmx_init() {
    hv_debug("vmx_init: CPUID check...");
    if (!cpu_has_vmx()) {
        hv_debug("vmx_init: no VMX in CPUID");
        return false;
    }
    hv_debug("vmx_init: CPUID OK");

    // IA32_FEATURE_CONTROL
    uint64_t fc = rdmsr(IA32_FEATURE_CONTROL);
    if (!(fc & 1)) {
        hv_debug("vmx_init: writing IA32_FEATURE_CONTROL");
        fc |= (1u << 2) | 1u;
        wrmsr(IA32_FEATURE_CONTROL, fc);
    } else if (!(fc & (1u << 2))) {
        hv_debug("vmx_init: BIOS locked MSR w/o VMX-on");
        return false;
    }
    hv_debug("vmx_init: FEATURE_CONTROL OK");

    hv_debug("vmx_init: setting CR4.VMXE");
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 13);
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
    hv_debug("vmx_init: CR4.VMXE set");

    // VMXON region
    uint64_t basic = rdmsr(IA32_VMX_BASIC);
    *(uint32_t*)vmxon_region_storage = (uint32_t)(basic & 0x7FFFFFFF);
    g_vmxon_phys = (uint64_t)vmxon_region_storage;

    hv_debug("vmx_init: executing VMXON...");
    uint8_t err;
    __asm__ volatile(
        "vmxon %1\n\t"
        "setc %0"
        : "=r"(err) : "m"(g_vmxon_phys) : "cc", "memory"
    );
    if (err) {
        hv_debug("vmx_init: VMXON failed (CF=1)");
        return false;
    }
    hv_debug("vmx_init: VMXON OK");

    // VMCS region
    *(uint32_t*)vmcs_region_storage = (uint32_t)(basic & 0x7FFFFFFF);
    g_vmcs_phys = (uint64_t)vmcs_region_storage;

    hv_debug("vmx_init: VMCLEAR...");
    __asm__ volatile("vmclear %1\n\t setc %0" : "=r"(err) : "m"(g_vmcs_phys) : "cc", "memory");
    if (err) { hv_debug("vmx_init: VMCLEAR failed"); return false; }

    hv_debug("vmx_init: VMPTRLD...");
    __asm__ volatile("vmptrld %1\n\t setc %0" : "=r"(err) : "m"(g_vmcs_phys) : "cc", "memory");
    if (err) { hv_debug("vmx_init: VMPTRLD failed"); return false; }

    hv_debug("vmx_init: done OK");
    return true;
}

bool vmx_setup_vmcs() {
    // Control fields
    vmcs_write(VMCS_CTRL_PIN_BASED,   adjust_ctrl(IA32_VMX_PINBASED_CTLS,  0));
    vmcs_write(VMCS_CTRL_PROC_BASED,  adjust_ctrl(IA32_VMX_PROCBASED_CTLS, 0));
    vmcs_write(VMCS_CTRL_EXIT,        adjust_ctrl(IA32_VMX_EXIT_CTLS,      0));
    vmcs_write(VMCS_CTRL_ENTRY,       adjust_ctrl(IA32_VMX_ENTRY_CTLS,     0));

    // EPT pointer
    vmcs_write(VMCS_CTRL_EPT_POINTER, ept_get_eptp());

    // Host state
    uint64_t cr0, cr3, cr4, rsp;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));

    vmcs_write(VMCS_HOST_CR0, cr0);
    vmcs_write(VMCS_HOST_CR3, cr3);
    vmcs_write(VMCS_HOST_CR4, cr4);
    vmcs_write(VMCS_HOST_RSP, rsp);
    vmcs_write(VMCS_HOST_IA32_EFER, rdmsr(IA32_EFER));

    uint16_t cs, ss, ds, es, fs, gs;
    __asm__ volatile("mov %%cs, %0" : "=r"(cs));
    __asm__ volatile("mov %%ss, %0" : "=r"(ss));
    __asm__ volatile("mov %%ds, %0" : "=r"(ds));
    __asm__ volatile("mov %%es, %0" : "=r"(es));
    __asm__ volatile("mov %%fs, %0" : "=r"(fs));
    __asm__ volatile("mov %%gs, %0" : "=r"(gs));
    vmcs_write(VMCS_HOST_CS_SEL, cs);
    vmcs_write(VMCS_HOST_SS_SEL, ss);
    vmcs_write(VMCS_HOST_DS_SEL, ds);
    vmcs_write(VMCS_HOST_ES_SEL, es);
    vmcs_write(VMCS_HOST_FS_SEL, fs);
    vmcs_write(VMCS_HOST_GS_SEL, gs);

    // Guest state
    vmcs_write(VMCS_GUEST_CR0, cr0);
    vmcs_write(VMCS_GUEST_CR3, cr3);
    vmcs_write(VMCS_GUEST_CR4, cr4);
    vmcs_write(VMCS_GUEST_IA32_EFER, rdmsr(IA32_EFER));

    vmcs_write(VMCS_GUEST_CS_SEL, 0x08);
    vmcs_write(VMCS_GUEST_DS_SEL, 0x10);
    vmcs_write(VMCS_GUEST_ES_SEL, 0x10);
    vmcs_write(VMCS_GUEST_SS_SEL, 0x10);
    vmcs_write(VMCS_GUEST_FS_SEL, 0x10);
    vmcs_write(VMCS_GUEST_GS_SEL, 0x10);

    vmcs_write(VMCS_GUEST_CS_LIMIT, 0xFFFFFFFF);
    vmcs_write(VMCS_GUEST_DS_LIMIT, 0xFFFFFFFF);
    vmcs_write(VMCS_GUEST_SS_LIMIT, 0xFFFFFFFF);
    vmcs_write(VMCS_GUEST_ES_LIMIT, 0xFFFFFFFF);

    vmcs_write(VMCS_GUEST_CS_ACCESS, 0xA09B);
    vmcs_write(VMCS_GUEST_SS_ACCESS, 0xC093);
    vmcs_write(VMCS_GUEST_DS_ACCESS, 0xC093);
    vmcs_write(VMCS_GUEST_ES_ACCESS, 0xC093);

    // LDTR / TR - unusable
    vmcs_write(0x4818, 1u << 16);
    vmcs_write(0x481A, 1u << 16);

    // GDTR / IDTR
    vmcs_write(VMCS_GUEST_GDTR_BASE,  0);
    vmcs_write(VMCS_GUEST_GDTR_LIMIT, 0);
    vmcs_write(VMCS_GUEST_IDTR_BASE,  0);
    vmcs_write(VMCS_GUEST_IDTR_LIMIT, 0);

    vmcs_write(VMCS_GUEST_RSP, 0x8000);
    vmcs_write(VMCS_GUEST_RIP, 0x1000);
    vmcs_write(VMCS_GUEST_RFLAGS, 0x2);

    // Host RIP — VM-Exit handler
    vmcs_write(VMCS_HOST_RIP, (uint64_t)vm_exit_stub);

    return true;
}

bool vmx_launch_guest() {
    uint8_t err;
    __asm__ volatile(
        "vmlaunch\n\t"
        "setc %0"
        : "=r"(err) : : "cc", "memory"
    );
    return err == 0;
}

void vmx_off() {
    __asm__ volatile("vmxoff" : : : "cc");
}