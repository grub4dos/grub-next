/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/efi.h>
#include <boot/module.h>
#include <boot/resource.h>
static struct boot_module_manager manager;
static struct boot_archive archive;
static void log_message(void *c, const char *text)
{
    boot_console(c, text);
}
static struct boot_module_buffer
allocate_image(struct boot_context *c, struct boot_efi_services *bs, const void *file, size_t size)
{
    struct boot_module_metadata metadata;
    struct boot_elf_image info;
    if (boot_module_inspect(file, size, BOOT_MODULE_TARGET_ID, BOOT_CAP_ALL, &metadata, &info))
        boot_panic(c, "module-inspect", 0);
    struct boot_module_buffer buffer;
    if (boot_module_allocate(c, bs, &info, &buffer))
        boot_panic(c, "module-memory", 0);
    return buffer;
}
void boot_module_probe(struct boot_context *c, struct boot_efi_services *bs)
{
    struct boot_archive_file sample, failing, config, font;
    if (boot_resource_open(c, &archive) ||
        boot_archive_find(&archive, "modules/" BOOT_RESOURCE_TARGET "/sample.so", &sample) ||
        boot_archive_find(&archive, "modules/" BOOT_RESOURCE_TARGET "/failing.so", &failing) ||
        boot_archive_find(&archive, "boot.lua", &config) || !config.size ||
        boot_archive_find(&archive, "fonts/minimal.hex", &font) || !font.size)
        boot_panic(c, "resource-open", 1);
    boot_console(c, "BOOT:PASS:resource-config-font\r\n");
    const void *sample_image = sample.data, *failing_image = failing.data;
    boot_modules_init(&manager, BOOT_MODULE_TARGET_ID, log_message, c);
    struct boot_module_buffer first = allocate_image(c, bs, sample_image, sample.size);
    struct boot_module_buffer second = allocate_image(c, bs, failing_image, failing.size);
    if (boot_module_load(&manager, sample_image, sample.size, first.data, first.size,
                         boot_module_sync))
        boot_panic(c, "module-load", 0);
    boot_service_fn fn = NULL;
    uint32_t value = 0;
    if (boot_service_find(&manager, "answer", &fn) || fn(&value) || value != 42)
        boot_panic(c, "module-service", 0);
    boot_console(c, "BOOT:PASS:module-sdk-execution\r\n");
    if (boot_module_load(&manager, sample_image, sample.size, second.data, second.size,
                         boot_module_sync) != BOOT_E_INVALID ||
        manager.count != 1)
        boot_panic(c, "module-duplicate", 0);
    if (boot_module_load(&manager, failing_image, failing.size, second.data, second.size,
                         boot_module_sync) != BOOT_E_IO ||
        manager.count != 2 || manager.modules[1].state != BOOT_MODULE_FAILED ||
        manager.service_count != 1 || boot_service_find(&manager, "rolled-back", &fn) == BOOT_OK)
        boot_panic(c, "module-rollback", 0);
    if (boot_service_find(&manager, "answer", &fn) || fn(&value) || value != 42 ||
        boot_module_load(&manager, failing_image, failing.size, second.data, second.size,
                         boot_module_sync) != BOOT_E_INVALID ||
        boot_modules_freeze(&manager) ||
        manager.api.register_service(manager.api.context, "late", fn) != BOOT_E_INVALID ||
        boot_module_load(&manager, sample_image, sample.size, first.data, first.size,
                         boot_module_sync) != BOOT_E_INVALID)
        boot_panic(c, "module-freeze", 0);
    boot_console(c, "BOOT:PASS:module-duplicate-rollback-freeze\r\n");
}
