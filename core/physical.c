/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/physical.h>
boot_status_t boot_phys_chunks(uint64_t address, void *buffer, size_t size,
                               boot_phys_chunk_fn chunk, void *opaque)
{
    if (!buffer || !size || !chunk || size > UINT64_MAX - address)
        return BOOT_E_INVALID;
    uint8_t *bytes = buffer;
    while (size)
    {
        size_t n = 0x200000 - (size_t)(address & 0x1fffff);
        if (n > size)
            n = size;
        boot_status_t s = chunk(opaque, address, bytes, n);
        if (s)
            return s;
        address += n;
        bytes += n;
        size -= n;
    }
    return BOOT_OK;
}
