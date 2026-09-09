/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/module.h>

static uint64_t le(const uint8_t *p, unsigned n)
{
    uint64_t v = 0;
    for (unsigned i = 0; i < n; ++i)
        v |= (uint64_t)p[i] << (8 * i);
    return v;
}
static int equal(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    for (size_t i = 0; i < n; ++i)
        if (x[i] != y[i])
            return 0;
    return 1;
}
static int name_valid(const char *s, size_t n)
{
    if (!s[0])
        return 0;
    for (size_t i = 0; i < n; ++i)
    {
        if (!s[i])
            return 1;
        if ((unsigned char)s[i] < 33 || (unsigned char)s[i] > 126)
            return 0;
    }
    return 0;
}
static int name_equal(const char *a, const char *b)
{
    for (size_t i = 0; i < 32; ++i)
    {
        if (a[i] != b[i])
            return 0;
        if (!a[i])
            return 1;
    }
    return 0;
}
static enum boot_elf_machine machine(enum boot_module_target t)
{
    switch (t)
    {
    case BOOT_MODULE_PC:
    case BOOT_MODULE_IA32:
        return BOOT_ELF_I386;
    case BOOT_MODULE_X64:
        return BOOT_ELF_X86_64;
    case BOOT_MODULE_ARM64:
        return BOOT_ELF_ARM64;
    case BOOT_MODULE_LOONGARCH64:
        return BOOT_ELF_LOONGARCH64;
    default:
        return 0;
    }
}
boot_status_t boot_module_inspect(const void *file, size_t length, enum boot_module_target target,
                                  uint64_t caps, struct boot_module_metadata *out,
                                  struct boot_elf_image *image)
{
    struct boot_elf_image info;
    if (!out || !image)
        return BOOT_E_INVALID;
    boot_status_t s = boot_elf_inspect(file, length, machine(target), &info);
    if (s)
        return s;
    const uint8_t *f = file, *descriptor = NULL;
    unsigned wide = f[4] == 2, word = wide ? 8 : 4;
    size_t ph = (size_t)le(f + (wide ? 32 : 28), word);
    unsigned count = (unsigned)le(f + (wide ? 56 : 44), 2);
    for (unsigned i = 0; i < count; ++i)
    {
        const uint8_t *h = f + ph + i * (wide ? 56 : 32);
        if (le(h, 4) != 4)
            continue;
        uint64_t offset = le(h + (wide ? 8 : 4), word);
        uint64_t size = le(h + (wide ? 32 : 16), word);
        if (offset > length || size > length - offset)
            return BOOT_E_INVALID;
        while (size)
        {
            if (size < 12)
                return BOOT_E_INVALID;
            const uint8_t *n = f + (size_t)offset;
            uint64_t names = le(n, 4), desc = le(n + 4, 4);
            uint64_t padded_name = (names + 3) & ~UINT64_C(3);
            uint64_t padded_desc = (desc + 3) & ~UINT64_C(3);
            uint64_t total = 12 + padded_name + padded_desc;
            if (total > size)
                return BOOT_E_INVALID;
            if (names == 8 && equal(n + 12, "BOOTMOD\0", 8))
            {
                if (descriptor || le(n + 8, 4) != 1 || desc != 152)
                    return BOOT_E_INVALID;
                descriptor = n + 12 + (size_t)padded_name;
            }
            offset += total;
            size -= total;
        }
    }
    if (!descriptor)
        return BOOT_E_INVALID;
    struct boot_module_metadata m;
    _Static_assert(sizeof(m) == 152, "metadata wire layout");
    for (size_t i = 0; i < sizeof(m); ++i)
        ((uint8_t *)&m)[i] = descriptor[i];
    const uint8_t zero[32] = {0};
    if (m.format != 1 || m.target != (unsigned)target || m.abi_min > BOOT_ABI_V1 ||
        m.abi_max < BOOT_ABI_V1 || m.abi_min > m.abi_max || !m.abi_min || (m.requires & ~caps) ||
        (m.provides & ~BOOT_CAP_ALL) || m.hash_kind || m.signature_kind ||
        !equal(m.digest, zero, 32))
        return BOOT_E_UNSUPPORTED;
    if (equal(m.vendor_uuid, zero, 16) || equal(m.module_uuid, zero, 16) ||
        !name_valid(m.name, sizeof(m.name)) || !name_valid(m.version, sizeof(m.version)))
        return BOOT_E_INVALID;
    *out = m;
    *image = info;
    return BOOT_OK;
}
static boot_status_t BOOT_MODULE_CALL api_log(void *opaque, const char *text)
{
    struct boot_module_manager *m = opaque;
    if (!m || !text)
        return BOOT_E_INVALID;
    size_t n = 0;
    while (n < 1024 && text[n])
        ++n;
    if (n == 1024)
        return BOOT_E_INVALID;
    if (m->write_log)
        m->write_log(m->log_context, text);
    return BOOT_OK;
}
static boot_status_t BOOT_MODULE_CALL api_register(void *opaque, const char *name,
                                                   boot_service_fn fn)
{
    struct boot_module_manager *m = opaque;
    if (!m || !m->busy || m->frozen || !name || !fn || !name_valid(name, 32))
        return BOOT_E_INVALID;
    if (!(m->modules[m->count - 1].metadata.provides & BOOT_CAP_SERVICE))
        return BOOT_E_UNSUPPORTED;
    for (size_t i = 0; i < m->service_count; ++i)
        if (name_equal(name, m->services[i].name))
            return BOOT_E_INVALID;
    if (m->service_count == BOOT_SERVICE_LIMIT)
        return BOOT_E_NOMEM;
    struct boot_service *s = &m->services[m->service_count++];
    size_t i = 0;
    do
    {
        s->name[i] = name[i];
    } while (name[i++]);
    s->call = fn;
    return BOOT_OK;
}
void boot_modules_init(struct boot_module_manager *m, enum boot_module_target target,
                       void (*log)(void *, const char *), void *context)
{
    for (size_t i = 0; i < sizeof(*m); ++i)
        ((uint8_t *)m)[i] = 0;
    m->target = target;
    m->write_log = log;
    m->log_context = context;
    m->api = (struct boot_api){BOOT_ABI_V1, sizeof(struct boot_api), BOOT_CAP_ALL, m, api_log,
                               api_register};
}
boot_status_t boot_module_load(struct boot_module_manager *m, const void *file, size_t length,
                               void *memory, size_t capacity, void (*sync)(void *, size_t))
{
    if (!m || m->busy || m->frozen || !sync)
        return BOOT_E_INVALID;
    struct boot_module_metadata metadata;
    struct boot_elf_image image;
    boot_status_t s =
        boot_module_inspect(file, length, m->target, m->api.capabilities, &metadata, &image);
    if (s)
        return s;
    for (size_t i = 0; i < m->count; ++i)
    {
        if (equal(metadata.module_uuid, m->modules[i].metadata.module_uuid, 16))
            return BOOT_E_INVALID;
        uintptr_t a = (uintptr_t)memory, b = (uintptr_t)m->modules[i].memory;
        if (a >= b ? a - b < m->modules[i].size : b - a < image.size)
            return BOOT_E_INVALID;
    }
    if (m->count == BOOT_MODULE_LIMIT)
        return BOOT_E_NOMEM;
    s = boot_elf_load(file, length, machine(m->target), memory, capacity, &image);
    if (s)
        return s;
    struct boot_module_record *r = &m->modules[m->count++];
    *r = (struct boot_module_record){metadata, BOOT_MODULE_LOADING, memory, image.size};
    m->busy = 1;
    sync(memory, image.size);
    boot_module_entry_fn entry = (boot_module_entry_fn)((uint8_t *)memory + image.entry_offset);
    const struct boot_module_v1 *v = entry();
    /* Native modules are trusted code, not a sandbox. Still reject descriptor
     * pointers outside their retained image before dereferencing them. */
    uintptr_t base = (uintptr_t)memory, address = (uintptr_t)v;
    s = BOOT_E_INVALID;
    if (address >= base && address - base <= image.size &&
        sizeof(*v) <= image.size - (address - base) &&
        !(address % _Alignof(struct boot_module_v1)) && v->abi_version == BOOT_ABI_V1 &&
        v->struct_size >= sizeof(*v) && v->struct_size <= image.size - (address - base) &&
        (uintptr_t)v->init >= base && (uintptr_t)v->init - base < image.size)
        s = v->init(&m->api);
    m->busy = 0;
    r->state = s ? BOOT_MODULE_FAILED : BOOT_MODULE_ACTIVE;
    if (s)
    {
        while (m->service_count > m->committed)
            m->services[--m->service_count] = (struct boot_service){0};
    }
    else
        m->committed = m->service_count;
    return s;
}
boot_status_t boot_modules_freeze(struct boot_module_manager *m)
{
    if (!m || m->busy)
        return BOOT_E_INVALID;
    m->frozen = 1;
    return BOOT_OK;
}
boot_status_t boot_service_find(const struct boot_module_manager *m, const char *name,
                                boot_service_fn *out)
{
    if (!m || !name || !out || !name_valid(name, 32))
        return BOOT_E_INVALID;
    for (size_t i = 0; i < m->committed; ++i)
        if (name_equal(name, m->services[i].name))
        {
            *out = m->services[i].call;
            return BOOT_OK;
        }
    return BOOT_E_INVALID;
}
