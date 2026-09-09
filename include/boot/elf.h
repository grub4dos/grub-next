/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_ELF_H
#define BOOT_ELF_H
#include <boot/status.h>
#include <stddef.h>

enum boot_elf_machine
{
    BOOT_ELF_I386 = 3,
    BOOT_ELF_X86_64 = 62,
    BOOT_ELF_ARM64 = 183,
    BOOT_ELF_LOONGARCH64 = 258
};

struct boot_elf_image
{
    size_t size, alignment, entry_offset;
};

/* Input must remain immutable throughout each call. No section headers are needed.
 * inspect validates the entire image, including relocations, without allocating.
 * load repeats validation and only then writes into a disjoint caller-owned BL
 * buffer. The caller provides executable memory, synchronizes instruction caches
 * and enforces platform permissions before invoking entry with the ELF ABI.
 * This layer neither authenticates nor invokes modules and offers no unload.
 * Output descriptors are published only on success. */
boot_status_t boot_elf_inspect(const void *file, size_t length, enum boot_elf_machine machine,
                               struct boot_elf_image *image);
boot_status_t boot_elf_load(const void *file, size_t length, enum boot_elf_machine machine,
                            void *memory, size_t capacity, struct boot_elf_image *image);
#endif
