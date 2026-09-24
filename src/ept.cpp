#include "ept.h"

extern "C" uint8_t ept_pages[4096 * 4];

#define EPT_R    (1ULL << 0)
#define EPT_W    (1ULL << 1)
#define EPT_X    (1ULL << 2)
#define EPT_WB   (6ULL << 3)

static uint64_t g_eptp = 0;

bool ept_init() {
    uint64_t* pml4 = (uint64_t*)(ept_pages + 0 * 4096);
    uint64_t* pdpt = (uint64_t*)(ept_pages + 1 * 4096);
    uint64_t* pd   = (uint64_t*)(ept_pages + 2 * 4096);
    uint64_t* pt   = (uint64_t*)(ept_pages + 3 * 4096);

    for (int i = 0; i < 512; i++) {
        pml4[i] = 0;
        pdpt[i] = 0;
        pd[i]   = 0;
        pt[i]   = 0;
    }

    pml4[0] = ((uint64_t)pdpt) | EPT_R | EPT_W | EPT_X;
    pdpt[0] = ((uint64_t)pd)   | EPT_R | EPT_W | EPT_X;
    pd[0]   = ((uint64_t)pt)   | EPT_R | EPT_W | EPT_X;

    // Identity map первых 2 МБ (512 × 4 КиБ)
    for (int i = 0; i < 512; i++) {
        pt[i] = ((uint64_t)i * 0x1000ULL) | EPT_R | EPT_W | EPT_X | EPT_WB;
    }

    // EPTP: PML4 phys | walk length = 3 (для 4 уровней) | WB memory type
    g_eptp = ((uint64_t)pml4) | (3ULL << 3) | (6ULL << 0);
    return true;
}

uint64_t ept_get_eptp() {
    return g_eptp;
}