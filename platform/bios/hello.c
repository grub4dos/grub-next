/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/log.h>
#include <boot/fp.h>
void boot_bios_main(uint32_t magic, uint32_t info)
{
    boot_log_init();
    if (boot_fp_check() != BOOT_OK) {
        boot_log_write("BOOT:FAIL:fp-state\r\n");
        return;
    }
    boot_log_write("BOOT:PASS:fp-state\r\n");
    if (magic != 0x36d76289 || !info || (info & 7)) {
        boot_log_write("BOOT:FAIL:multiboot2-context\r\n");
        return;
    }
    boot_log_write("BOOT:HELLO:i386-pc:multiboot2\r\n");
}
