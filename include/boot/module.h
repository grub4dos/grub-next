/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_MODULE_H
#define BOOT_MODULE_H
#include <boot/elf.h>
#if defined(__x86_64__)
#define BOOT_MODULE_CALL __attribute__((sysv_abi))
#elif defined(__i386__)
#define BOOT_MODULE_CALL __attribute__((cdecl))
#else
#define BOOT_MODULE_CALL
#endif
#define BOOT_ABI_V1 1u
#define BOOT_CAP_LOG UINT64_C(1)
#define BOOT_CAP_SERVICE UINT64_C(2)
#define BOOT_CAP_ALL (BOOT_CAP_LOG | BOOT_CAP_SERVICE)
#define BOOT_MODULE_LIMIT 16
#define BOOT_SERVICE_LIMIT 32
enum boot_module_target
{
    BOOT_MODULE_PC = 1,
    BOOT_MODULE_IA32,
    BOOT_MODULE_X64,
    BOOT_MODULE_ARM64,
    BOOT_MODULE_LOONGARCH64
};
typedef boot_status_t(BOOT_MODULE_CALL *boot_service_fn)(uint32_t *);
struct boot_api
{
    uint32_t abi_version, struct_size;
    uint64_t capabilities;
    void *context;
    boot_status_t(BOOT_MODULE_CALL *log)(void *, const char *);
    boot_status_t(BOOT_MODULE_CALL *register_service)(void *, const char *, boot_service_fn);
};
struct boot_module_v1
{
    uint32_t abi_version, struct_size;
    boot_status_t(BOOT_MODULE_CALL *init)(const struct boot_api *);
};
typedef const struct boot_module_v1 *(BOOT_MODULE_CALL *boot_module_entry_fn)(void);
/* Fixed LE PT_NOTE descriptor, no native pointers. Hash/signature kind 0 means
 * unsigned; reserved digest bytes must be zero. Authentication is a later phase. */
struct boot_module_metadata
{
    uint32_t format, target, abi_min, abi_max;
    uint64_t provides,
        requires;
    uint8_t vendor_uuid[16], module_uuid[16];
    char name[32], version[16];
    uint32_t hash_kind, signature_kind;
    uint8_t digest[32];
};
struct boot_module_note
{
    uint32_t namesz, descsz, type;
    char owner[8];
    struct boot_module_metadata metadata;
} __attribute__((packed, aligned(4)));
enum boot_module_state
{
    BOOT_MODULE_LOADING = 1,
    BOOT_MODULE_ACTIVE,
    BOOT_MODULE_FAILED
};
struct boot_module_record
{
    struct boot_module_metadata metadata;
    enum boot_module_state state;
    void *memory;
    size_t size;
};
struct boot_service
{
    char name[32];
    boot_service_fn call;
};
struct boot_module_manager
{
    struct boot_api api;
    struct boot_module_record modules[BOOT_MODULE_LIMIT];
    struct boot_service services[BOOT_SERVICE_LIMIT];
    size_t count, service_count, committed;
    unsigned busy, frozen;
    enum boot_module_target target;
    void *log_context;
    void (*write_log)(void *, const char *);
};
void boot_modules_init(struct boot_module_manager *, enum boot_module_target,
                       void (*)(void *, const char *), void *);
boot_status_t boot_module_inspect(const void *, size_t, enum boot_module_target, uint64_t,
                                  struct boot_module_metadata *, struct boot_elf_image *);
/* Caller-owned BL executable storage remains alive, even on init failure.
 * Input stays immutable/disjoint. synchronize publishes code before execution. */
boot_status_t boot_module_load(struct boot_module_manager *, const void *, size_t, void *, size_t,
                               void (*synchronize)(void *, size_t));
boot_status_t boot_modules_freeze(struct boot_module_manager *);
boot_status_t boot_service_find(const struct boot_module_manager *, const char *,
                                boot_service_fn *);
struct boot_context;
struct boot_efi_services;
struct boot_module_buffer
{
    void *data;
    size_t size;
};
/* BIOS passes NULL services. Allocation must precede Resident memory freeze. */
boot_status_t boot_module_allocate(struct boot_context *, struct boot_efi_services *,
                                   const struct boot_elf_image *, struct boot_module_buffer *);
void boot_module_sync(void *, size_t);
#endif
