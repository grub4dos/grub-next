/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_CONTEXT_H
#define BOOT_CONTEXT_H
#include <boot/memory.h>
enum boot_entry
{
    BOOT_ENTRY_MULTIBOOT2,
    BOOT_ENTRY_LINUX,
    BOOT_ENTRY_EFI
};
struct boot_context;
struct boot_time
{
    uint16_t year;
    uint8_t month, day, hour, minute, second;
};
struct boot_platform
{
    boot_status_t (*console)(const char *);
    boot_status_t (*time)(struct boot_time *);
    void (*reset)(void);
    void (*halt)(void);
};
struct boot_framebuffer
{
    uint64_t address, size;
    uint32_t pitch, width, height;
    uint8_t bpp, kind, red_position, red_size, green_position, green_size, blue_position, blue_size;
};
struct boot_module_input
{
    uint64_t base, size;
    const char *name;
};
struct boot_context
{
    uint32_t version;
    enum boot_entry entry;
    struct boot_memory memory;
    struct boot_platform platform;
    uint64_t resource_base, resource_size;
    const void *command_line;
    size_t command_line_size;
    int command_line_utf16;
    struct boot_framebuffer framebuffer;
    struct boot_module_input modules[16];
    size_t module_count;
    const char *firmware;
    uint32_t firmware_revision;
    void *firmware_table;
    void *image_handle;
    int pae;
};
boot_status_t boot_context_multiboot2(struct boot_context *, const void *, size_t);
boot_status_t boot_context_linux(struct boot_context *, const void *, size_t);
void boot_ring_write(const char *);
size_t boot_ring_read(char *, size_t);
boot_status_t boot_console(struct boot_context *, const char *);
_Noreturn void boot_panic(struct boot_context *, const char *, int reset);
#endif
