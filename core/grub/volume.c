/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "context.h"
#include <boot/volume.h>
#include <grub/diskfilter.h>
#include <grub/extcmd.h>
#include <grub/partition.h>

/* GRUB's private registries are static, like the filesystem registry. All
 * retained objects belong to this single polling storage session. */
static struct boot_storage *storage;
static uint64_t generation;
static struct boot_grub_context lifetime;
static grub_extcmd_t loop_command;
grub_disk_dev_t grub_disk_dev_list;
static const struct boot_file *pending_file;
static unsigned nesting;
static unsigned listing_depth;
struct binding
{
    char name[128];
    struct boot_slice slice;
};
static struct binding inputs[BOOT_DISKS + BOOT_PARTITIONS];
static unsigned input_count, loop_count;
static char loops[BOOT_DISKS][128];
struct exported
{
    char name[128];
    struct boot_slice slice;
    uint64_t generation;
    struct boot_slice backing;
};
static struct exported exports[BOOT_DISKS];
static unsigned export_count;
struct backing
{
    struct boot_fs fs;
    struct boot_file file;
};

void boot_grub_init_loopback(grub_dl_t);
void boot_grub_init_diskfilter(grub_dl_t);
void boot_grub_fini_diskfilter(void);
void boot_grub_init_lvm(grub_dl_t);
void boot_grub_init_mdraid1x(grub_dl_t);
void boot_grub_init_raid5rec(grub_dl_t);
void boot_grub_init_raid6rec(grub_dl_t);

void grub_print_error(void)
{
    grub_errno = GRUB_ERR_NONE;
}
_Noreturn void grub_fatal(const char *message, ...)
{
    (void)message;
    __builtin_trap();
}
char *grub_strchr(const char *s, int c)
{
    do
    {
        if (*s == c)
            return (char *)s;
    } while (*s++);
    return NULL;
}
char *grub_strstr(const char *s, const char *needle)
{
    size_t n = grub_strlen(needle);
    do
    {
        if (!grub_strncmp(s, needle, n))
            return (char *)s;
    } while (*s++);
    return NULL;
}
unsigned long long grub_strtoull(const char *s, const char **end, int base)
{
    uint64_t value = 0;
    while (grub_isspace(*s))
        ++s;
    if (!base)
        base = 10;
    if (base == 16 && s[0] == '0' && grub_tolower(s[1]) == 'x')
        s += 2;
    while (*s)
    {
        if (!grub_isdigit(*s) && !(grub_tolower(*s) >= 'a' && grub_tolower(*s) <= 'z'))
            break;
        unsigned d =
            grub_isdigit(*s) ? (unsigned)(*s - '0') : (unsigned)(grub_tolower(*s) - 'a' + 10);
        if (d >= (unsigned)base)
            break;
        if (value > grub_divmod64(UINT64_MAX - d, (unsigned)base, NULL))
        {
            grub_error(GRUB_ERR_BAD_NUMBER, "number overflow");
            break;
        }
        value = value * (unsigned)base + d;
        ++s;
    }
    if (end)
        *end = s;
    return value;
}
unsigned long grub_strtoul(const char *s, const char **end, int base)
{
    uint64_t v = grub_strtoull(s, end, base);
    if (v > (unsigned long)-1)
        grub_error(GRUB_ERR_BAD_NUMBER, "number overflow");
    return (unsigned long)v;
}
grub_extcmd_t grub_register_extcmd(const char *n, grub_extcmd_t f, unsigned flags, const char *s,
                                   const char *d, const struct grub_arg_option *opts)
{
    (void)n;
    (void)flags;
    (void)s;
    (void)d;
    (void)opts;
    loop_command = f;
    return f;
}
void grub_unregister_extcmd(grub_extcmd_t c)
{
    (void)c;
}
void grub_disk_dev_register(grub_disk_dev_t d)
{
    for (grub_disk_dev_t p = grub_disk_dev_list; p; p = p->next)
        if (p == d)
            return;
    d->next = grub_disk_dev_list;
    grub_disk_dev_list = d;
}
void grub_disk_dev_unregister(grub_disk_dev_t d)
{
    for (grub_disk_dev_t *p = &grub_disk_dev_list; *p; p = &(*p)->next)
        if (*p == d)
        {
            *p = d->next;
            return;
        }
}
static int native_iterate(grub_disk_dev_iterate_hook_t hook, void *arg, grub_disk_pull_t pull)
{
    if (pull != GRUB_DISK_PULL_NONE)
        return 0;
    for (unsigned i = 0; i < input_count; ++i)
        if (hook(inputs[i].name, arg))
            return 1;
    return 0;
}
static grub_err_t native_open(const char *name, grub_disk_t d)
{
    for (unsigned i = 0; i < input_count; ++i)
        if (!grub_strcmp(inputs[i].name, name))
        {
            d->slice = &inputs[i].slice;
            d->total_sectors = d->slice->size >> 9;
            d->id = i;
            return GRUB_ERR_NONE;
        }
    return grub_error(GRUB_ERR_UNKNOWN_DEVICE, "unknown input");
}
static struct grub_disk_dev native = {.name = "boot",
                                      .id = GRUB_DISK_DEVICE_HOST_ID,
                                      .disk_iterate = native_iterate,
                                      .disk_open = native_open};
grub_disk_t grub_disk_open(const char *name)
{
    if (++nesting > 16)
    {
        --nesting;
        grub_error(GRUB_ERR_BAD_DEVICE, "volume nesting limit");
        return NULL;
    }
    grub_disk_t d = grub_zalloc(sizeof(*d));
    if (!d)
    {
        --nesting;
        return NULL;
    }
    d->name = grub_strdup(name);
    if (!d->name)
    {
        grub_free(d);
        --nesting;
        return NULL;
    }
    d->log_sector_size = 9;
    for (grub_disk_dev_t p = grub_disk_dev_list; p; p = p->next)
    {
        d->dev = p;
        grub_errno = GRUB_ERR_NONE;
        grub_err_t e = p->disk_open(name, d);
        if (!e)
        {
            --nesting;
            return d;
        }
        if (e != GRUB_ERR_UNKNOWN_DEVICE)
            break;
    }
    grub_free((void *)d->name);
    grub_free(d);
    --nesting;
    return NULL;
}
void grub_disk_close(grub_disk_t d)
{
    if (!d)
        return;
    if (d->dev->disk_close)
        d->dev->disk_close(d);
    grub_free((void *)d->name);
    grub_free(d);
}
grub_disk_addr_t grub_disk_native_sectors(grub_disk_t d)
{
    return d->partition ? d->partition->len : d->total_sectors;
}
static boot_status_t partition_read(void *opaque, uint64_t lba, uint32_t count, void *buf)
{
    grub_disk_t d = opaque;
    return d->dev->disk_read(d, lba, count, buf) ? BOOT_E_IO : BOOT_OK;
}
int grub_partition_iterate(grub_disk_t d, int (*hook)(grub_disk_t, grub_partition_t, void *),
                           void *arg)
{
    /* Physical inputs already include their native-block partitions. For an
     * assembled volume, reuse that same boot parser through a temporary adapter. */
    if (d->slice)
        return 0;
    void *memory = grub_zalloc(sizeof(struct boot_storage) + 4095);
    struct boot_storage *temporary =
        memory ? (struct boot_storage *)(((uintptr_t)memory + 4095) & ~(uintptr_t)4095) : NULL;
    struct boot_slice *parts = grub_calloc(BOOT_PARTITIONS, sizeof(*parts));
    if (!temporary || !parts)
    {
        grub_free(memory);
        grub_free(parts);
        return 0;
    }
    static const struct boot_block_ops ops = {partition_read, NULL};
    struct boot_block block = {.ops = &ops,
                               .opaque = d,
                               .blocks = d->total_sectors,
                               .logical_size = 512,
                               .physical_size = 512,
                               .io_alignment = 1,
                               .max_blocks = 1};
    struct boot_slice root;
    temporary->generation = 1;
    size_t count = BOOT_PARTITIONS;
    int stop = 0;
    if (!boot_block_add(temporary, &block, &root) && !boot_partitions(&root, parts, &count))
        for (size_t i = 0; i < count; ++i)
        {
            struct grub_partition p = {parts[i].offset >> 9, parts[i].size >> 9};
            stop = hook(d, &p, arg);
            d->partition = NULL;
            if (stop)
                break;
        }
    grub_free(parts);
    grub_free(memory);
    grub_errno = GRUB_ERR_NONE;
    return stop;
}
grub_err_t boot_grub_virtual_read(grub_disk_t d, uint64_t at, void *buffer, size_t n)
{
    if (!d->dev || !d->dev->disk_read || d->total_sectors > (UINT64_MAX >> 9) ||
        at > (grub_disk_native_sectors(d) << 9) || n > (grub_disk_native_sectors(d) << 9) - at ||
        nesting >= 16)
        return grub_error(GRUB_ERR_OUT_OF_RANGE, "volume range");
    if (d->partition)
        at += d->partition->start << 9;
    ++nesting;
    uint8_t *p = buffer;
    grub_err_t e = GRUB_ERR_NONE;
    while (n)
    {
        uint8_t sector[512];
        size_t offset = (size_t)(at & 511), take = grub_min(n, 512 - offset);
        e = d->dev->disk_read(d, at >> 9, 1, (char *)sector);
        if (e)
            break;
        grub_memcpy(p, sector + offset, take);
        p += take;
        at += take;
        n -= take;
    }
    --nesting;
    return e;
}
grub_file_t grub_file_open(const char *path, enum grub_file_type type)
{
    (void)path;
    if (!pending_file || !(type & GRUB_FILE_TYPE_NO_DECOMPRESS))
    {
        grub_error(GRUB_ERR_BAD_ARGUMENT, "typed backing file required");
        return NULL;
    }
    grub_file_t f = grub_zalloc(sizeof(*f));
    struct backing *b = grub_zalloc(sizeof(*b));
    if (!f || !b)
    {
        grub_free(f);
        grub_free(b);
        return NULL;
    }
    b->fs = *pending_file->fs;
    b->fs.root.fs = &b->fs;
    b->file = *pending_file;
    b->file.fs = &b->fs;
    f->size = b->file.size;
    f->boot_backing = b;
    return f;
}
grub_err_t grub_file_close(grub_file_t f)
{
    grub_free(f->boot_backing);
    grub_free(f);
    return GRUB_ERR_NONE;
}
grub_off_t grub_file_seek(grub_file_t f, grub_off_t pos)
{
    grub_off_t old = f->offset;
    if (pos > f->size)
        grub_error(GRUB_ERR_OUT_OF_RANGE, "file seek");
    else
        f->offset = pos;
    return old;
}
grub_ssize_t grub_file_read(grub_file_t f, void *buf, size_t n)
{
    struct backing *b = f->boot_backing;
    size_t got = 0;
    boot_status_t s = boot_file_read(&b->file, f->offset, buf, n, &got);
    if (s)
    {
        grub_error(GRUB_ERR_READ_ERROR, "backing read");
        return -1;
    }
    f->offset += got;
    return (grub_ssize_t)got;
}
static void drop(void)
{
    struct boot_grub_context ctx;
    boot_grub_enter(&ctx, NULL);
    boot_grub_fini_diskfilter();
    while (loop_count)
    {
        char *args[] = {loops[--loop_count]};
        struct grub_arg_list state[2] = {{1}, {0}};
        struct grub_extcmd_context command = {state};
        loop_command(&command, 1, args);
    }
    boot_grub_release(&lifetime);
    (void)boot_grub_leave(&ctx, GRUB_ERR_NONE);
    input_count = export_count = 0;
    storage = NULL;
}
static boot_status_t session(struct boot_storage *s)
{
    if (!s || !s->generation || s->busy)
        return BOOT_E_INVALID;
    if (storage && storage != s)
        return BOOT_E_INVALID;
    if (storage && generation != s->generation)
    {
        if (listing_depth)
            return BOOT_E_STALE;
        drop();
    }
    if (!storage)
    {
        static unsigned initialized;
        storage = s;
        generation = s->generation;
        grub_disk_dev_register(&native);
        boot_grub_init_loopback(NULL);
        boot_grub_init_diskfilter(NULL);
        if (!initialized)
        {
            boot_grub_init_lvm(NULL);
            boot_grub_init_mdraid1x(NULL);
            boot_grub_init_raid5rec(NULL);
            boot_grub_init_raid6rec(NULL);
            initialized = 1;
        }
    }
    return BOOT_OK;
}
static boot_status_t exported_validate(void *opaque)
{
    struct exported *e = opaque;
    if (!storage || generation != storage->generation || e->generation != generation)
        return BOOT_E_STALE;
    if (e->backing.storage)
        return boot_slice_validate(&e->backing);
    /* Validate every input, even on a cache hit. A degraded array is detected
     * at scan time; media replacement never silently joins an existing array. */
    for (unsigned i = 0; i < input_count; ++i)
    {
        boot_status_t status = boot_slice_validate(&inputs[i].slice);
        if (status)
            return status;
    }
    return BOOT_OK;
}
static boot_status_t exported_read(void *opaque, uint64_t lba, uint32_t count, void *buf)
{
    struct exported *e = opaque;
    struct boot_grub_context ctx;
    boot_grub_enter(&ctx, NULL);
    grub_disk_t d = grub_disk_open(e->name);
    grub_err_t error = d ? grub_disk_read(d, lba, 0, (size_t)count << 9, buf) : grub_errno;
    grub_disk_close(d);
    if (!error)
        ctx.provider_error = BOOT_OK; /* Upstream mirror/parity recovery succeeded. */
    return boot_grub_leave(&ctx, error);
}
static const struct boot_block_ops volume_ops = {exported_read, exported_validate};
boot_status_t boot_volume_open(struct boot_storage *s, const char *name, struct boot_slice *out)
{
    if (!name || !out || grub_strlen(name) < 4 || grub_strlen(name) >= sizeof(exports[0].name))
        return BOOT_E_INVALID;
    boot_status_t status = session(s);
    if (status)
        return status;
    for (unsigned i = 0; i < export_count; ++i)
        if (!grub_strcmp(name, exports[i].name))
        {
            *out = exports[i].slice;
            return BOOT_OK;
        }
    if (export_count == BOOT_DISKS)
        return BOOT_E_NOMEM;
    struct boot_grub_context ctx;
    boot_grub_enter(&ctx, NULL);
    grub_disk_t d = grub_disk_open(name);
    uint64_t sectors = d ? d->total_sectors : 0;
    grub_disk_close(d);
    /* Opening an upstream LV may discover further persistent volumes. */
    boot_grub_retain(&ctx, &lifetime);
    status = boot_grub_leave(&ctx, GRUB_ERR_NONE);
    if (status)
        return status;
    struct exported *e = &exports[export_count];
    grub_memset(e, 0, sizeof(*e));
    grub_strcpy(e->name, name);
    e->generation = generation;
    struct boot_block b = {.ops = &volume_ops,
                           .opaque = e,
                           .blocks = sectors,
                           .logical_size = 512,
                           .physical_size = 512,
                           .io_alignment = 1,
                           .max_blocks = 1,
                           .provider = "grub-volume"};
    status = boot_block_add(s, &b, out);
    if (!status)
    {
        e->slice = *out;
        ++export_count;
    }
    return status;
}
static boot_status_t add_input(const struct boot_slice *slice)
{
    for (unsigned i = 0; i < input_count; ++i)
        if (inputs[i].slice.slot == slice->slot && inputs[i].slice.offset == slice->offset &&
            inputs[i].slice.size == slice->size)
            return BOOT_OK;
    if (input_count == ARRAY_SIZE(inputs))
        return BOOT_E_NOMEM;
    struct binding *b = &inputs[input_count];
    b->slice = *slice;
    /* Fixed-width aliases avoid host formatting and are deliberately unstable. */
    grub_strcpy(b->name, "boot000");
    b->name[4] += (char)(input_count / 100);
    b->name[5] += (char)((input_count / 10) % 10);
    b->name[6] += (char)(input_count % 10);
    ++input_count;
    return BOOT_OK;
}
boot_status_t boot_loopback_add(const char *name, const struct boot_file *file,
                                struct boot_slice *out)
{
    if (listing_depth || !name || !file || !file->fs || !out || !file->size || file->directory ||
        file->size > UINT64_MAX - 511 || file->fs->slice.depth >= 8 ||
        file->generation != file->fs->slice.generation || grub_strlen(name) >= sizeof(loops[0]) ||
        grub_strncmp(name, "loop", 4))
        return BOOT_E_INVALID;
    boot_status_t status = boot_slice_validate(&file->fs->slice);
    if (status)
        return status;
    status = session(file->fs->slice.storage);
    if (status)
        return status;
    if (loop_count == BOOT_DISKS || storage->count == BOOT_DISKS)
        return BOOT_E_NOMEM;
    /* The backing handle exists before this new device: graph edges can only
     * point backwards; replacement/detach cannot create a cycle. */
    struct boot_grub_context ctx;
    boot_grub_enter(&ctx, NULL);
    struct grub_arg_list state[2] = {{0}, {0}};
    struct grub_extcmd_context command = {state};
    char *args[] = {(char *)name, "typed-file"};
    pending_file = file;
    grub_err_t error = loop_command(&command, 2, args);
    pending_file = NULL;
    if (!error)
        boot_grub_retain(&ctx, &lifetime);
    status = boot_grub_leave(&ctx, error);
    if (status)
        return status;
    grub_strcpy(loops[loop_count++], name);
    status = boot_volume_open(storage, name, out);
    if (!status)
    {
        ((struct exported *)storage->disks[out->slot].opaque)->backing = file->fs->slice;
        out->depth = file->fs->slice.depth + 1;
        ((struct exported *)storage->disks[out->slot].opaque)->slice.depth = out->depth;
    }
    else
    {
        boot_grub_enter(&ctx, NULL);
        state[0].set = 1;
        (void)loop_command(&command, 1, args);
        --loop_count;
        (void)boot_grub_leave(&ctx, GRUB_ERR_NONE);
    }
    return status;
}
static int ignore_name(const char *name, void *opaque)
{
    (void)name;
    (void)opaque;
    return 0;
}
boot_status_t boot_diskfilter_scan(struct boot_storage *s)
{
    if (listing_depth)
        return BOOT_E_INVALID;
    boot_status_t status = session(s);
    if (status)
        return status;
    for (size_t i = 0; i < s->count; ++i)
    {
        if (s->disks[i].ops == &volume_ops)
            continue;
        struct boot_slice root = {.storage = s,
                                  .generation = s->generation,
                                  .slot = (uint32_t)i,
                                  .size = s->disks[i].blocks * s->disks[i].logical_size};
        uint8_t header[512];
        status = boot_block_read(&root, 0, header, sizeof(header));
        if (status == BOOT_E_IO || status == BOOT_E_TIMEOUT)
            continue;
        if (status)
            return status;
        status = add_input(&root);
        if (status)
            return status;
        struct boot_slice parts[BOOT_PARTITIONS];
        size_t count = BOOT_PARTITIONS;
        status = boot_partitions(&root, parts, &count);
        if (status == BOOT_E_STALE || status == BOOT_E_NOMEM)
            return status;
        if (!status)
            for (size_t j = 0; j < count; ++j)
            {
                status = add_input(&parts[j]);
                if (status)
                    return status;
            }
    }
    struct boot_grub_context ctx;
    boot_grub_enter(&ctx, NULL);
    for (grub_disk_dev_t d = grub_disk_dev_list; d; d = d->next)
        if (d->id == GRUB_DISK_DEVICE_DISKFILTER_ID)
            d->disk_iterate(ignore_name, NULL, GRUB_DISK_PULL_RESCAN);
    boot_grub_retain(&ctx, &lifetime);
    /* A detector may read past tiny/nonmatching media, as in upstream scan. */
    ctx.provider_error = BOOT_OK;
    return boot_grub_leave(&ctx, GRUB_ERR_NONE);
}
struct visit_state
{
    boot_volume_hook hook;
    void *arg;
    boot_status_t status;
};
static int visit_name(const char *name, void *opaque)
{
    struct visit_state *v = opaque;
    v->status = v->hook(v->arg, name);
    return v->status != BOOT_OK;
}
boot_status_t boot_volume_list(struct boot_storage *s, boot_volume_hook hook, void *arg)
{
    if (!hook)
        return BOOT_E_INVALID;
    boot_status_t status = session(s);
    if (status)
        return status;
    struct boot_grub_context ctx;
    boot_grub_enter(&ctx, NULL);
    struct visit_state v = {hook, arg, BOOT_OK};
    ++listing_depth;
    for (grub_disk_dev_t d = grub_disk_dev_list; d && !v.status; d = d->next)
        if (d != &native)
            d->disk_iterate(visit_name, &v, GRUB_DISK_PULL_NONE);
    --listing_depth;
    status = boot_grub_leave(&ctx, GRUB_ERR_NONE);
    if (s->generation != generation)
        return BOOT_E_STALE;
    return status ? status : v.status;
}
boot_status_t boot_volume_reset(struct boot_storage *s)
{
    if (listing_depth || (storage && storage != s))
        return BOOT_E_INVALID;
    boot_status_t status = boot_storage_rescan(s);
    if (!status && storage)
        drop();
    return status;
}
boot_status_t boot_slice_physical(const struct boot_slice *s, int *physical)
{
    if (!physical)
        return BOOT_E_INVALID;
    boot_status_t status = boot_slice_validate(s);
    if (status)
        return status;
    *physical = s->storage->disks[s->slot].ops != &volume_ops;
    return BOOT_OK;
}
