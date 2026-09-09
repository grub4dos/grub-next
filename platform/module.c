/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/efi.h>
#include <boot/module.h>
boot_status_t boot_module_allocate(struct boot_context *c, struct boot_efi_services *bs,
                                   const struct boot_elf_image *info,
                                   struct boot_module_buffer *out)
{
    if (!c || !info || !out || !info->size || !info->alignment ||
        (info->alignment & (info->alignment - 1)) || info->size > 16 * 1024 * 1024 ||
        info->alignment > 16 * 1024 * 1024 || c->memory.frozen)
        return BOOT_E_INVALID;
    uint64_t base = 0;
    size_t capacity = (info->size + info->alignment + 4095) & ~(size_t)4095;
    boot_status_t s;
    if (bs)
    {
        /* EfiLoaderCode is executable BL storage; firmware owns its lifetime
         * after EBS. It contains ELF modules, never manually loaded PE images. */
        if (bs->allocate_pages(0, 1, capacity / 4096, &base))
            return BOOT_E_NOMEM;
        if (base > UINTPTR_MAX || capacity > UINTPTR_MAX - base)
            s = BOOT_E_UNSUPPORTED;
        else
            s = boot_memory_reserve(&c->memory, base, capacity, BOOT_BL);
        if (s)
        {
            bs->free_pages(base, capacity / 4096);
            return s;
        }
    }
    else
    {
        s = boot_memory_alloc(&c->memory, BOOT_BL, capacity, info->alignment, 0x100000, UINT32_MAX,
                              &base);
        if (s)
            return s;
    }
    out->data =
        (void *)(uintptr_t)((base + info->alignment - 1) & ~(uint64_t)(info->alignment - 1));
    out->size = info->size;
    return BOOT_OK;
}
/* Single-CPU publication of copied/relocated ELF instructions. */
void boot_module_sync(void *memory, size_t length)
{
#if defined(__aarch64__)
    uint64_t ctr;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    size_t dc = (size_t)4 << ((ctr >> 16) & 15), ic = (size_t)4 << (ctr & 15);
    uintptr_t end = (uintptr_t)memory + length;
    for (uintptr_t p = (uintptr_t)memory & ~(dc - 1); p < end; p += dc)
        __asm__ volatile("dc cvau, %0" : : "r"(p) : "memory");
    __asm__ volatile("dsb ish" : : : "memory");
    for (uintptr_t p = (uintptr_t)memory & ~(ic - 1); p < end; p += ic)
        __asm__ volatile("ic ivau, %0" : : "r"(p) : "memory");
    __asm__ volatile("dsb ish; isb" : : : "memory");
#elif defined(__loongarch__)
    (void)memory;
    (void)length;
    __asm__ volatile("dbar 0; ibar 0" : : : "memory");
#else
    (void)memory;
    (void)length;
    __asm__ volatile("" : : : "memory");
#endif
}
