#ifndef EPT_H
#define EPT_H

#include <stdint.h>

bool     ept_init();
uint64_t ept_get_eptp();

#endif