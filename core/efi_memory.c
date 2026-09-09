/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/efi.h>
uint32_t boot_efi_memory_type(uint32_t type)
{
    switch (type)
    {
    case 1:
    case 2:
        return BOOT_MEM_LOADER;
    case 3:
    case 4:
        return BOOT_MEM_BS;
    case 5:
    case 6:
        return BOOT_MEM_RT;
    case 7:
        return BOOT_MEM_FREE;
    case 8:
        return BOOT_MEM_BAD;
    case 9:
        return BOOT_MEM_ACPI;
    case 10:
        return BOOT_MEM_NVS;
    default:
        return BOOT_MEM_RESERVED;
    }
}
boot_status_t boot_efi_import_map(struct boot_memory *m, const void *data, size_t size,
                                  size_t stride)
{
    if (!m || !data || stride < sizeof(struct boot_efi_descriptor) || size % stride)
        return BOOT_E_INVALID;
    const uint8_t *p = data;
    for (size_t off = 0; off < size; off += stride)
    {
        /* Firmware descriptors may have a larger stride; copy without alignment assumptions. */
        struct boot_efi_descriptor d;
        for (size_t i = 0; i < sizeof(d); ++i)
            ((uint8_t *)&d)[i] = p[off + i];
        if (!d.pages || (d.physical & 4095) || d.pages > UINT64_MAX / 4096 ||
            boot_memory_add(m, d.physical, d.pages * 4096, boot_efi_memory_type(d.type)))
            return BOOT_E_INVALID;
    }
    return BOOT_OK;
}
