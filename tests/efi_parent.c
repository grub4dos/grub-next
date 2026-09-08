/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stddef.h>
#include <boot/log.h>
#include "hello_image.h"
static unsigned char *volatile child_image = hello_image;
/* Test-only x64 UEFI table prefixes; unused service slots retain ABI offsets.
 * Layout cross-checked against ref/grub/include/grub/efi/api.h.
 */
struct boot_services {
    uint8_t header[24];
    void (*unused[22])(void);
    uintptr_t (*load_image)(uint8_t, void *, void *, void *, uintptr_t, void **);
    uintptr_t (*start_image)(void *, uintptr_t *, uint16_t **);
};
struct system_table {
    uint8_t prefix[96];
    struct boot_services *services;
};
_Static_assert(offsetof(struct boot_services, load_image) == 200, "LoadImage ABI");
_Static_assert(offsetof(struct boot_services, start_image) == 208, "StartImage ABI");
_Static_assert(offsetof(struct system_table, services) == 96, "SystemTable ABI");
uintptr_t efi_main(void *self, struct system_table *system)
{
    void *child = 0;
    uintptr_t status;
    static uint8_t bad[64];
    boot_log_init();
    status = system->services->load_image(0, self, 0, bad, sizeof(bad), &child);
    if (!(status >> 63)) {
        boot_log_write("BOOT:FAIL:invalid-pe-accepted\r\n");
        return 1;
    }
    boot_log_write("BOOT:PASS:invalid-pe-rejected\r\n");
    status = system->services->load_image(0, self, 0, child_image,
                                         sizeof(hello_image), &child);
    if (status) {
        boot_log_write("BOOT:FAIL:LoadImage\r\n");
        return status;
    }
    boot_log_write("BOOT:PASS:LoadImage\r\n");
    status = system->services->start_image(child, 0, 0);
    boot_log_write(status ? "BOOT:FAIL:StartImage\r\n" : "BOOT:PASS:StartImage\r\n");
    return status;
}
