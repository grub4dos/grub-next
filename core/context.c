/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/context.h>
static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t u64(const uint8_t *p)
{
    return u32(p) | (uint64_t)u32(p + 4) << 32;
}
static uint32_t e820_type(uint32_t t)
{
    switch (t)
    {
    case 1:
        return BOOT_MEM_FREE;
    case 3:
        return BOOT_MEM_ACPI;
    case 4:
        return BOOT_MEM_NVS;
    case 5:
        return BOOT_MEM_BAD;
    default:
        return BOOT_MEM_RESERVED;
    }
}
static boot_status_t add_e820(struct boot_context *c, const uint8_t *p)
{
    if (!u64(p + 8))
        return BOOT_OK;
    return boot_memory_add(&c->memory, u64(p), u64(p + 8), e820_type(u32(p + 16)));
}
boot_status_t boot_context_multiboot2(struct boot_context *c, const void *data, size_t length)
{
    const uint8_t *p = data;
    if (!c || !p || length < 16 || u32(p) > length || (u32(p) & 7) || u32(p) < 16)
        return BOOT_E_INVALID;
    size_t total = u32(p);
    int have_map = 0;
    c->version = 1;
    c->entry = BOOT_ENTRY_MULTIBOOT2;
    c->firmware = "BIOS";
    for (size_t off = 8; off <= total - 8;)
    {
        uint32_t type = u32(p + off), size = u32(p + off + 4);
        if (size < 8 || size > total - off)
            return BOOT_E_INVALID;
        const uint8_t *tag = p + off;
        if (!type)
            return size == 8 && off + 8 == total && have_map ? BOOT_OK : BOOT_E_INVALID;
        if (type == 6)
        {
            if (have_map || size < 16 || u32(tag + 8) < 24 || u32(tag + 12) ||
                (size - 16) % u32(tag + 8))
                return BOOT_E_INVALID;
            for (size_t j = 16; j < size; j += u32(tag + 8))
                if (add_e820(c, tag + j))
                    return BOOT_E_INVALID;
            have_map = 1;
        }
        else if (type == 1)
        {
            size_t n = 8;
            while (n < size && tag[n])
                ++n;
            if (n == size)
                return BOOT_E_INVALID;
            c->command_line = tag + 8;
            c->command_line_size = n - 8;
        }
        else if (type == 8)
        {
            if (size < 32)
                return BOOT_E_INVALID;
            struct boot_framebuffer *f = &c->framebuffer;
            f->address = u64(tag + 8);
            f->pitch = u32(tag + 16);
            f->width = u32(tag + 20);
            f->height = u32(tag + 24);
            f->bpp = tag[28];
            f->kind = tag[29];
            f->size = (uint64_t)f->pitch * f->height;
            if (f->size > UINT64_MAX - f->address)
                return BOOT_E_INVALID;
            if (f->kind == 1)
            {
                if (size < 38)
                    return BOOT_E_INVALID;
                f->red_position = tag[32];
                f->red_size = tag[33];
                f->green_position = tag[34];
                f->green_size = tag[35];
                f->blue_position = tag[36];
                f->blue_size = tag[37];
            }
        }
        else if (type == 3)
        {
            if (size < 17 || u32(tag + 12) < u32(tag + 8))
                return BOOT_E_INVALID;
            uint64_t base = u32(tag + 8), n = u32(tag + 12) - base;
            size_t name_end = 16;
            while (name_end < size && tag[name_end])
                ++name_end;
            if (name_end == size)
                return BOOT_E_INVALID;
            if (c->module_count == 16)
                return BOOT_E_NOMEM;
            c->modules[c->module_count++] =
                (struct boot_module_input){base, n, (const char *)tag + 16};
            if (n && boot_memory_reserve(&c->memory, base, n, BOOT_BL))
                return BOOT_E_INVALID;
            if (!c->resource_size)
            {
                c->resource_base = base;
                c->resource_size = n;
            }
        }
        size_t step = ((size_t)size + 7) & ~(size_t)7;
        if (step > total - off)
            return BOOT_E_INVALID;
        off += step;
    }
    return BOOT_E_INVALID;
}
boot_status_t boot_context_linux(struct boot_context *c, const void *data, size_t length)
{
    const uint8_t *p = data;
    if (!c || !p || length < 4096 || u32(p + 0x202) != 0x53726448 ||
        ((unsigned)p[0x206] | (unsigned)p[0x207] << 8) < 0x0203 || !p[0x1e8] || p[0x1e8] > 128)
        return BOOT_E_INVALID;
    /* This is the 32-bit entry: there are no implied initrd high fields. */
    if (u64(p + 0x250))
        return BOOT_E_UNSUPPORTED; /* setup_data not consumed yet */
    c->version = 1;
    c->entry = BOOT_ENTRY_LINUX;
    c->firmware = "BIOS";
    for (unsigned i = 0; i < p[0x1e8]; ++i)
        if (add_e820(c, p + 0x2d0 + i * 20))
            return BOOT_E_INVALID;
    c->command_line = (void *)(uintptr_t)u32(p + 0x228);
    /* Keep raw screen_info for the later VBE frontend, including 64-bit base. */
    c->framebuffer.width = (uint32_t)p[0x12] | (uint32_t)p[0x13] << 8;
    c->framebuffer.height = (uint32_t)p[0x14] | (uint32_t)p[0x15] << 8;
    c->framebuffer.bpp = p[0x16];
    c->framebuffer.address = u32(p + 0x18);
    if (u32(p + 0x36) & 2)
        c->framebuffer.address |= (uint64_t)u32(p + 0x3a) << 32;
    c->framebuffer.pitch = (uint32_t)p[0x24] | (uint32_t)p[0x25] << 8;
    c->framebuffer.size = (uint64_t)c->framebuffer.pitch * c->framebuffer.height;
    c->framebuffer.kind = 1;
    c->framebuffer.red_size = p[0x26];
    c->framebuffer.red_position = p[0x27];
    c->framebuffer.green_size = p[0x28];
    c->framebuffer.green_position = p[0x29];
    c->framebuffer.blue_size = p[0x2a];
    c->framebuffer.blue_position = p[0x2b];
    c->resource_base = u32(p + 0x218);
    c->resource_size = u32(p + 0x21c);
    if (c->resource_size &&
        (c->resource_size > UINT64_C(0x100000000) - c->resource_base ||
         boot_memory_reserve(&c->memory, c->resource_base, c->resource_size, BOOT_BL)))
        return BOOT_E_INVALID;
    return BOOT_OK;
}
