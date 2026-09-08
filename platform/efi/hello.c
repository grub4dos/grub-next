/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/log.h>
#include <boot/fp.h>
#if defined(_MSC_VER)
/* MSVC ABI marker emitted for FP expressions; no CRT initializer is needed. */
int _fltused = 0;
#endif
/* Native x64 Windows target supplies the UEFI Microsoft calling convention. */
extern void boot_fp_reset(void);
static const char *volatile greeting = "BOOT:HELLO:x86_64-efi\r\n";
uintptr_t efi_main(void *image, void *system_table)
{
    (void)image;
    (void)system_table;
    boot_fp_reset();
    boot_log_init();
    if (boot_fp_check() != BOOT_OK) {
        boot_log_write("BOOT:FAIL:fp-state\r\n");
        return 1;
    }
    boot_log_write("BOOT:PASS:fp-state\r\n");
    boot_log_write(greeting);
    /* Phase 0 probe intentionally returns to its firmware test parent. */
    boot_fp_reset();
    return 0;
}
