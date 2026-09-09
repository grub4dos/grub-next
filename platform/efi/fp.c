/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/efi.h>
#if defined(_MSC_VER)
int _fltused;
#endif
void boot_arch_fp_reset(void)
{
#if defined(__i386__) || defined(__x86_64__)
    const uint32_t value = 0x1f80;
    __asm__ volatile("fninit; ldmxcsr %0" : : "m"(value));
#elif defined(__aarch64__)
    __asm__ volatile("msr fpcr, xzr; msr fpsr, xzr; isb");
#elif defined(__loongarch__)
    __asm__ volatile("movgr2fcsr $fcsr0, $zero");
#endif
}
boot_status_t boot_arch_fp_check(void)
{
#if defined(__i386__) || defined(__x86_64__)
    uint32_t mxcsr;
    uint16_t cw;
    __asm__ volatile("stmxcsr %0; fnstcw %1" : "=m"(mxcsr), "=m"(cw));
    if (mxcsr != 0x1f80 || cw != 0x37f)
        return BOOT_E_INVALID;
#elif defined(__aarch64__)
    uint64_t control, status;
    __asm__ volatile("mrs %0, fpcr; mrs %1, fpsr" : "=r"(control), "=r"(status));
    if (control || status)
        return BOOT_E_INVALID;
#elif defined(__loongarch__)
    uint32_t control;
    __asm__ volatile("movfcsr2gr %0, $fcsr0" : "=r"(control));
    if (control)
        return BOOT_E_INVALID;
#endif
    volatile double a = 1.5, b = 2;
    return a * b == 3 ? BOOT_OK : BOOT_E_INVALID;
}
