#ifndef VMX_H
#define VMX_H

#include <stdint.h>

bool     vmx_init();
bool     vmx_setup_vmcs();
bool     vmx_launch_guest();
void     vmx_off();

uint64_t vmcs_read(uint64_t field);
void     vmcs_write(uint64_t field, uint64_t value);

#endif