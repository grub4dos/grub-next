/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/efi.h>
static struct boot_efi_system *system_table;
static struct boot_efi_serial *serial;
static boot_status_t console(const char *s)
{
    boot_status_t result = BOOT_OK;
    if (serial)
    {
        /* Firmware Serial I/O has a finite timeout established below. */
        for (const char *p = s; *p; ++p)
        {
            uintptr_t n = 1;
            if (serial->write(serial, &n, (void *)p) || n != 1)
                result = BOOT_E_IO;
        }
    }
    if (system_table->out)
    {
        uint16_t text[2] = {0, 0};
        for (; *s; ++s)
        {
            text[0] = (uint8_t)*s;
            if (system_table->out->output(system_table->out, text))
                result = BOOT_E_IO;
        }
    }
    return result;
}
static boot_status_t time_read(struct boot_time *t)
{
    struct boot_efi_time e;
    if (!t)
        return BOOT_E_INVALID;
    if (system_table->runtime->get_time(&e, 0))
        return BOOT_E_IO;
    *t = (struct boot_time){e.year, e.month, e.day, e.hour, e.minute, e.second};
    return BOOT_OK;
}
static _Noreturn void halt(void)
{
    for (;;)
    {
#if defined(__i386__) || defined(__x86_64__)
        __asm__ volatile("cli; hlt");
#elif defined(__aarch64__)
        __asm__ volatile("msr daifset, #15; wfi");
#elif defined(__loongarch__)
        __asm__ volatile("csrwr $zero, 0; idle 0");
#endif
    }
}
static _Noreturn void reset(void)
{
    boot_arch_fp_reset();
    system_table->runtime->reset(0, 0, 0, 0);
    halt();
}
static boot_status_t refresh_map(struct boot_memory *m)
{
    static uint8_t map[65536] __attribute__((aligned(8)));
    uintptr_t size = sizeof(map), key, stride;
    uint32_t version;
    if (system_table->services->get_memory_map(&size, map, &key, &stride, &version))
        return BOOT_E_NOMEM;
    m->count = 0;
    return boot_efi_import_map(m, map, size, stride);
}
static boot_status_t reserve_pages(uint64_t base, uint64_t size, enum boot_owner owner)
{
    uint64_t address = base;
    if (size / 4096 > UINTPTR_MAX)
        return BOOT_E_NOMEM;
    return system_table->services->allocate_pages(2, owner == BOOT_RESIDENT ? 0 : 2,
                                                  (uintptr_t)(size / 4096), &address)
               ? BOOT_E_NOMEM
               : BOOT_OK;
}
static boot_status_t free_pages(uint64_t base, uint64_t size)
{
    return system_table->services->free_pages(base, (uintptr_t)(size / 4096)) ? BOOT_E_IO : BOOT_OK;
}
struct loaded_image_prefix
{
    uint32_t revision;
    void *parent, *system, *device, *path, *reserved;
    uint32_t options_size;
    void *options;
};
struct gop_info
{
    uint32_t version, width, height, format, masks[4], scanline;
};
struct gop_mode
{
    uint32_t max_mode, current_mode;
    struct gop_info *info;
    uintptr_t info_size;
    uint64_t framebuffer;
    uintptr_t framebuffer_size;
};
struct gop_protocol
{
    void *query, *set, *blt;
    struct gop_mode *mode;
};
static void input_metadata(struct boot_context *c, void *image)
{
    static const uint8_t loaded_guid[16] = {0xa1, 0x31, 0x1b, 0x5b, 0x62, 0x95, 0xd2, 0x11,
                                            0x8e, 0x3f, 0,    0xa0, 0xc9, 0x69, 0x72, 0x3b};
    static const uint8_t gop_guid[16] = {0xde, 0xa9, 0x42, 0x90, 0xdc, 0x23, 0x38, 0x4a,
                                         0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a};
    struct loaded_image_prefix *loaded = 0;
    if (!system_table->services->handle_protocol(image, loaded_guid, (void **)&loaded) && loaded)
    {
        c->command_line = loaded->options;
        c->command_line_size = loaded->options_size;
        c->command_line_utf16 = 1;
    }
    struct gop_protocol *gop = 0;
    if (system_table->services->locate_protocol(gop_guid, 0, (void **)&gop) || !gop || !gop->mode ||
        !gop->mode->info)
        return;
    struct gop_info *i = gop->mode->info;
    if (i->format >= 3 || i->scanline > UINT32_MAX / 4 ||
        gop->mode->framebuffer_size > UINT64_MAX - gop->mode->framebuffer)
        return;
    struct boot_framebuffer *f = &c->framebuffer;
    f->address = gop->mode->framebuffer;
    f->size = gop->mode->framebuffer_size;
    f->width = i->width;
    f->height = i->height;
    f->pitch = i->scanline * 4;
    f->bpp = 32;
    f->kind = 1;
    if (i->format < 2)
    {
        f->red_position = i->format == 0 ? 0 : 16;
        f->green_position = 8;
        f->blue_position = i->format == 0 ? 16 : 0;
        f->red_size = f->green_size = f->blue_size = 8;
    }
    else
    {
        uint8_t *positions[3] = {&f->red_position, &f->green_position, &f->blue_position};
        uint8_t *sizes[3] = {&f->red_size, &f->green_size, &f->blue_size};
        for (unsigned n = 0; n < 3; ++n)
        {
            uint32_t mask = i->masks[n];
            if (!mask)
                continue;
            while (!(mask & 1))
            {
                ++*positions[n];
                mask >>= 1;
            }
            while (mask & 1)
            {
                ++*sizes[n];
                mask >>= 1;
            }
            if (mask)
                f->kind = 0; /* Non-contiguous masks need a future frontend. */
        }
    }
}
boot_status_t boot_efi_platform(struct boot_context *c, void *image, struct boot_efi_system *s)
{
    static const uint8_t serial_guid[16] = {0x53, 0x47, 0xc1, 0xbb, 0x64, 0x97, 0xd2, 0x11,
                                            0x8e, 0x3f, 0,    0xa0, 0xc9, 0x69, 0x72, 0x3b};
    if (!c || !s || s->header.signature != UINT64_C(0x5453595320494249) || !s->services ||
        !s->runtime)
        return BOOT_E_INVALID;
    system_table = s;
    c->version = 1;
    c->entry = BOOT_ENTRY_EFI;
    c->firmware = "UEFI";
    c->firmware_revision = s->revision;
    c->firmware_table = s;
    c->image_handle = image;
    c->platform = (struct boot_platform){console, time_read, reset, halt};
    s->services->locate_protocol(serial_guid, 0, (void **)&serial);
    if (serial)
    {
        typedef boot_efi_status(BOOT_EFI * attributes_fn)(void *, uint64_t, uint32_t, uint32_t,
                                                          uint32_t, uint8_t, uint32_t);
        attributes_fn set = (attributes_fn)serial->set_attributes;
        if (set(serial, 115200, 16, 10000, 1, 8, 1))
            serial = 0;
    }
    input_metadata(c, image);
    uintptr_t size = 0, key, stride = 0;
    uint32_t version;
    void *map = 0;
    s->services->get_memory_map(&size, 0, &key, &stride, &version);
    if (stride < sizeof(struct boot_efi_descriptor) || size > 1024 * 1024)
        return BOOT_E_INVALID;
    /* Allocating the buffer changes the map. Retry boundedly with slack. */
    for (unsigned attempt = 0; attempt < 4; ++attempt)
    {
        size += stride * 16;
        if (s->services->allocate_pool(2, size, &map))
            return BOOT_E_NOMEM;
        uintptr_t capacity = size;
        boot_efi_status status =
            s->services->get_memory_map(&capacity, map, &key, &stride, &version);
        if (!status)
        {
            c->memory.physical_bits = 63;
            boot_status_t result = boot_efi_import_map(&c->memory, map, capacity, stride);
            s->services->free_pool(map);
            c->memory.page_size = 4096;
#if defined(__aarch64__) || defined(__loongarch__)
            c->memory.resident_page_size = 65536;
#endif
            c->memory.refresh = refresh_map;
            c->memory.reserve_pages = reserve_pages;
            c->memory.free_pages = free_pages;
            return result;
        }
        s->services->free_pool(map);
        if (capacity <= size || capacity > 1024 * 1024)
            return BOOT_E_IO;
        size = capacity;
    }
    return BOOT_E_NOMEM;
}
