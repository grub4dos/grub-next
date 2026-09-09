/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/module.h>
#ifndef BOOT_SAMPLE_FAIL
#define BOOT_SAMPLE_FAIL 0
#endif
__attribute__((section(".note.boot.module"), used)) static const struct boot_module_note note = {
    8,
    152,
    1,
    "BOOTMOD",
    {1,
     BOOT_SAMPLE_TARGET,
     1,
     1,
     BOOT_CAP_SERVICE,
     BOOT_CAP_ALL,
     {0x92, 0x17, 0x20, 0x34, 0x17, 0xe1, 0x40, 0xa2, 0xb3, 1, 2, 3, 4, 5, 6, 7},
     {0xa3, 0x24, 0x30, 0x45, 0x25, 0xf2, 0x41, 0xb3, 0xa4, 2, 3, 4, 5, 6, 7, BOOT_SAMPLE_FAIL},
     "sdk-sample",
     "1.0",
     0,
     0,
     {0}}};
static uint32_t value = 41, bss;
static uint32_t *volatile relocated = &value;
static boot_status_t BOOT_MODULE_CALL answer(uint32_t *out)
{
    if (!out || bss != 1)
        return BOOT_E_INVALID;
    volatile double half = 20.5, two = 2.0;
    if (half * two != *relocated)
        return BOOT_E_INVALID;
    *out = *relocated + bss;
    return BOOT_OK;
}
static boot_status_t BOOT_MODULE_CALL init(const struct boot_api *api)
{
    if (!api || api->abi_version != BOOT_ABI_V1 || api->struct_size < sizeof(*api) ||
        (api->capabilities & BOOT_CAP_ALL) != BOOT_CAP_ALL || bss || *relocated != 41)
        return BOOT_E_UNSUPPORTED;
    bss = 1;
    boot_status_t s =
        api->register_service(api->context, BOOT_SAMPLE_FAIL ? "rolled-back" : "answer", answer);
    if (s)
        return s;
    s = api->log(api->context, "BOOT:PASS:module-api-call\r\n");
    return s ? s : (BOOT_SAMPLE_FAIL ? BOOT_E_IO : BOOT_OK);
}
static const struct boot_module_v1 module = {1, sizeof(module), init};
__attribute__((visibility("default"))) const struct boot_module_v1 *BOOT_MODULE_CALL
boot_module_entry(void)
{
    return &module;
}
