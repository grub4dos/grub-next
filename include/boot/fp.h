/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_FP_H
#define BOOT_FP_H
#include <boot/status.h>
/* x86 Phase 0 diagnostic, called only after eager initialization. */
static inline boot_status_t boot_fp_check(void)
{
    uint32_t mxcsr;
    uint16_t control;
    __asm__ volatile ("stmxcsr %0" : "=m"(mxcsr));
    __asm__ volatile ("fnstcw %0" : "=m"(control));
    if (mxcsr != 0x1f80 || control != 0x37f) return BOOT_E_INVALID;
    volatile double a = 1.5, b = 2.0;
    return a * b == 3.0 ? BOOT_OK : BOOT_E_INVALID;
}
#endif
