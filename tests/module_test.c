/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "module_images.h"
#include <assert.h>
#include <boot/module.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
static void sync_code(void *p, size_t n)
{
    (void)p;
    (void)n;
}
static struct boot_module_manager manager;
static struct boot_module_manager incompatible;
static size_t test_entry_offset;
static void corrupt_descriptor(void *memory, size_t length)
{
    (void)length;
    boot_module_entry_fn entry = (boot_module_entry_fn)((uint8_t *)memory + test_entry_offset);
    struct boot_module_v1 *v = (struct boot_module_v1 *)entry();
    v->abi_version = 2;
}
static unsigned log_calls;
static void log_probe(void *context, const char *text)
{
    (void)context;
    assert(text && manager.busy && manager.count);
    assert(manager.modules[manager.count - 1].state == BOOT_MODULE_LOADING);
    boot_service_fn fn;
    assert(boot_service_find(&manager, log_calls ? "rolled-back" : "answer", &fn) ==
           BOOT_E_INVALID);
    assert(boot_modules_freeze(&manager) == BOOT_E_INVALID);
    assert(boot_module_load(&manager, sample_image, sizeof(sample_image), NULL, 0, sync_code) ==
           BOOT_E_INVALID);
    ++log_calls;
}
static size_t note_offset(void)
{
    for (size_t i = 0; i + 160 < sizeof(sample_image); ++i)
        if (!memcmp(sample_image + i, "BOOTMOD\0", 8))
            return i + 8;
    abort();
}
static void reject(uint8_t *copy, size_t length, void *memory)
{
    memset(memory, 0xa5, 65536);
    assert(boot_module_load(&manager, copy, length, memory, 65536, sync_code) != BOOT_OK);
    assert(!manager.count && !manager.service_count);
    for (size_t i = 0; i < 65536; ++i)
        assert(((uint8_t *)memory)[i] == 0xa5);
}
int main(void)
{
    void *memory =
        mmap(NULL, 262144, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(memory != MAP_FAILED);
    void *second = (uint8_t *)memory + 65536;
    uint8_t *copy = malloc(sizeof(sample_image));
    assert(copy);
    boot_modules_init(&manager, BOOT_MODULE_X64, log_probe, NULL);
    size_t d = note_offset();
    for (size_t field = d - 20; field < d; field += 4)
    {
        memcpy(copy, sample_image, sizeof(sample_image));
        memset(copy + field, 0xff, 4);
        reject(copy, sizeof(sample_image), memory);
    }
    const size_t fields[] = {0, 4, 8, 12, 24, 32, 48, 64, 96, 112, 116, 120};
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
    {
        memcpy(copy, sample_image, sizeof(sample_image));
        size_t at = fields[i];
        if (at == 12)
            memset(copy + d + at, 0, 4);
        else if (at == 32 || at == 48)
            memset(copy + d + at, 0, 16);
        else if (at == 64 || at == 96)
            memset(copy + d + at, 'x', at == 64 ? 32 : 16);
        else
            memset(copy + d + at, 0xff, 4);
        reject(copy, sizeof(sample_image), memory);
    }
    /* All truncation points in note and ELF; section headers are optional. */
    for (size_t n = 0; n < sizeof(sample_image); ++n)
    {
        struct boot_module_metadata metadata;
        struct boot_elf_image image;
        boot_status_t s =
            boot_module_inspect(sample_image, n, BOOT_MODULE_X64, BOOT_CAP_ALL, &metadata, &image);
        if (s)
        {
            memcpy(copy, sample_image, sizeof(sample_image));
            reject(copy, n, memory);
        }
    }
    /* Deterministic metadata fuzzing never invokes mutated native code. */
    uint32_t seed = 0x12092026;
    for (unsigned i = 0; i < 10000; ++i)
    {
        memcpy(copy, sample_image, sizeof(sample_image));
        seed = seed * 1664525u + 1013904223u;
        copy[d - 20 + seed % 172] ^= (uint8_t)((seed >> 24) | 1);
        struct boot_module_metadata metadata;
        struct boot_elf_image image;
        (void)boot_module_inspect(copy, sizeof(sample_image), BOOT_MODULE_X64, BOOT_CAP_ALL,
                                  &metadata, &image);
    }
    struct boot_module_metadata metadata;
    struct boot_elf_image image;
    assert(boot_module_inspect(sample_image, sizeof(sample_image), BOOT_MODULE_X64, 0, &metadata,
                               &image) == BOOT_E_UNSUPPORTED);
    assert(boot_module_inspect(sample_image, sizeof(sample_image), BOOT_MODULE_ARM64, BOOT_CAP_ALL,
                               &metadata, &image) != BOOT_OK);
    assert(boot_module_inspect(sample_image, sizeof(sample_image), BOOT_MODULE_X64, BOOT_CAP_ALL,
                               &metadata, &image) == BOOT_OK);
    test_entry_offset = image.entry_offset;
    boot_modules_init(&incompatible, BOOT_MODULE_X64, NULL, NULL);
    assert(boot_module_load(&incompatible, sample_image, sizeof(sample_image),
                            (uint8_t *)memory + 131072, 65536,
                            corrupt_descriptor) == BOOT_E_INVALID);
    assert(incompatible.modules[0].state == BOOT_MODULE_FAILED && !incompatible.service_count);
    boot_modules_init(&incompatible, BOOT_MODULE_X64, NULL, NULL);
    incompatible.api.struct_size = 0;
    assert(boot_module_load(&incompatible, sample_image, sizeof(sample_image),
                            (uint8_t *)memory + 196608, 65536, sync_code) == BOOT_E_UNSUPPORTED);
    assert(incompatible.modules[0].state == BOOT_MODULE_FAILED && !incompatible.service_count);
    assert(boot_module_load(&manager, sample_image, sizeof(sample_image), memory, 65536,
                            sync_code) == BOOT_OK);
    assert(manager.modules[0].state == BOOT_MODULE_ACTIVE);
    boot_service_fn fn;
    uint32_t value;
    assert(boot_service_find(&manager, "answer", &fn) == BOOT_OK);
    assert(fn(&value) == BOOT_OK && value == 42);
    assert(boot_module_load(&manager, sample_image, sizeof(sample_image), second, 65536,
                            sync_code) == BOOT_E_INVALID);
    assert(boot_module_load(&manager, failing_image, sizeof(failing_image), second, 65536,
                            sync_code) == BOOT_E_IO);
    assert(manager.modules[1].state == BOOT_MODULE_FAILED && manager.service_count == 1);
    assert(log_calls == 2);
    assert(boot_service_find(&manager, "rolled-back", &fn) == BOOT_E_INVALID);
    assert(boot_module_load(&manager, failing_image, sizeof(failing_image), second, 65536,
                            sync_code) == BOOT_E_INVALID);
    assert(boot_modules_freeze(&manager) == BOOT_OK);
    assert(manager.api.register_service(manager.api.context, "late", fn) == BOOT_E_INVALID);
    assert(boot_module_load(&manager, sample_image, sizeof(sample_image), second, 65536,
                            sync_code) == BOOT_E_INVALID);
    free(copy);
    munmap(memory, 262144);
    puts("PASS: module ABI, metadata, truncation, 10000 mutations, execution, rollback, duplicate, "
         "freeze");
    return 0;
}
