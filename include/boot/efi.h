/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_EFI_H
#define BOOT_EFI_H
#include <boot/context.h>
#if defined(__i386__)
#define BOOT_EFI __attribute__((cdecl))
#else
#define BOOT_EFI
#endif
typedef uintptr_t boot_efi_status;
struct boot_efi_header
{
    uint64_t signature;
    uint32_t revision, size, crc, reserved;
};
struct boot_efi_time
{
    uint16_t year;
    uint8_t month, day, hour, minute, second, pad;
    uint32_t nanosecond;
    int16_t timezone;
    uint8_t daylight, pad2;
};
struct boot_efi_descriptor
{
    uint32_t type, pad;
    uint64_t physical, virtual_address, pages, attribute;
};
struct boot_efi_console
{
    void *reset;
    boot_efi_status(BOOT_EFI *output)(void *, const uint16_t *);
};
struct boot_efi_serial
{
    uint32_t revision;
    void *reset, *set_attributes, *set_control, *get_control;
    boot_efi_status(BOOT_EFI *write)(void *, uintptr_t *, void *);
    void *read, *mode;
};
struct boot_efi_runtime
{
    struct boot_efi_header header;
    boot_efi_status(BOOT_EFI *get_time)(struct boot_efi_time *, void *);
    void *unused[9];
    void(BOOT_EFI *reset)(uint32_t, boot_efi_status, uintptr_t, void *);
};
struct boot_efi_services
{
    struct boot_efi_header header;
    void *raise_tpl, *restore_tpl;
    boot_efi_status(BOOT_EFI *allocate_pages)(uint32_t, uint32_t, uintptr_t, uint64_t *);
    boot_efi_status(BOOT_EFI *free_pages)(uint64_t, uintptr_t);
    boot_efi_status(BOOT_EFI *get_memory_map)(uintptr_t *, void *, uintptr_t *, uintptr_t *,
                                              uint32_t *);
    boot_efi_status(BOOT_EFI *allocate_pool)(uint32_t, uintptr_t, void **);
    boot_efi_status(BOOT_EFI *free_pool)(void *);
    void *unused1[9];
    boot_efi_status(BOOT_EFI *handle_protocol)(void *, const void *, void **);
    void *unused1_tail[5];
    boot_efi_status(BOOT_EFI *load_image)(uint8_t, void *, void *, void *, uintptr_t, void **);
    boot_efi_status(BOOT_EFI *start_image)(void *, uintptr_t *, uint16_t **);
    void *unused2[13];
    boot_efi_status(BOOT_EFI *locate_protocol)(const void *, void *, void **);
};
struct boot_efi_system
{
    struct boot_efi_header header;
    uint16_t *vendor;
    uint32_t revision;
    void *in_handle, *in, *out_handle;
    struct boot_efi_console *out;
    void *err_handle, *err;
    struct boot_efi_runtime *runtime;
    struct boot_efi_services *services;
    uintptr_t table_count;
    void *tables;
};
uint32_t boot_efi_memory_type(uint32_t);
boot_status_t boot_efi_import_map(struct boot_memory *, const void *, size_t, size_t);
void boot_arch_fp_reset(void);
boot_status_t boot_arch_fp_check(void);
#endif
