/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../grub/context.h"

void boot_grub_init_fat(grub_dl_t);
void boot_grub_init_ext2(grub_dl_t);
void boot_grub_init_iso9660(grub_dl_t);
void boot_grub_init_ntfs(grub_dl_t);
void boot_grub_init_ntfscomp(grub_dl_t);

static int stop(const char *name, const struct grub_dirhook_info *info, void *opaque)
{
    (void)name;
    (void)info;
    (void)opaque;
    return 1;
}
static boot_status_t validate(const struct boot_file *file)
{
    if (!file || !file->fs || file->fs->kind < BOOT_FS_FAT || file->fs->kind > BOOT_FS_NTFS)
        return BOOT_E_INVALID;
    if (file->generation != file->fs->slice.generation)
        return BOOT_E_STALE;
    return boot_slice_validate(&file->fs->slice);
}
boot_status_t boot_fs_mount(struct boot_fs *fs, const struct boot_slice *slice)
{
    if (!fs || !slice)
        return BOOT_E_INVALID;
    boot_status_t status = boot_slice_validate(slice);
    if (status)
        return status;
    grub_memset(fs, 0, sizeof(*fs));
    fs->slice = *slice;
    /* Static driver registration; no runtime module loading or unloading. */
    if (!boot_grub_drivers[BOOT_FS_FAT])
    {
        boot_grub_init_fat(NULL);
        boot_grub_init_ext2(NULL);
        boot_grub_init_iso9660(NULL);
        boot_grub_init_ntfs(NULL);
        boot_grub_init_ntfscomp(NULL);
    }
    for (unsigned i = BOOT_FS_FAT; i <= BOOT_FS_NTFS; ++i)
    {
        struct boot_grub_context ctx;
        boot_grub_enter(&ctx, slice);
        grub_err_t error = boot_grub_drivers[i]->fs_dir(&ctx.device, "/", stop, NULL);
        status = boot_grub_leave(&ctx, error);
        if (!status)
        {
            fs->kind = (enum boot_fs_kind)i;
            fs->root.fs = fs;
            fs->root.generation = slice->generation;
            fs->root.directory = 1;
            fs->root.path[0] = '/';
            if (boot_grub_drivers[i]->fs_uuid)
            {
                boot_grub_enter(&ctx, slice);
                char *uuid = NULL;
                error = boot_grub_drivers[i]->fs_uuid(&ctx.device, &uuid);
                if (!error && uuid && grub_strlen(uuid) < sizeof(fs->uuid))
                    grub_strcpy(fs->uuid, uuid);
                grub_free(uuid);
                /* A missing UUID does not make readable content inaccessible. */
                status = boot_grub_leave(&ctx, error);
                if (status == BOOT_E_STALE || status == BOOT_E_IO || status == BOOT_E_TIMEOUT)
                {
                    fs->kind = 0;
                    return status;
                }
            }
            return BOOT_OK;
        }
        if (status == BOOT_E_STALE || status == BOOT_E_IO || status == BOOT_E_TIMEOUT)
            return status;
    }
    return BOOT_E_CORRUPT;
}
boot_status_t boot_file_open(struct boot_fs *fs, const char *path, struct boot_file *out)
{
    if (!fs || !path || !out || *path != '/')
        return BOOT_E_INVALID;
    boot_status_t status = validate(&fs->root);
    if (status)
        return status;
    size_t length = 0;
    while (path[length])
        if (++length >= sizeof(out->path))
            return BOOT_E_INVALID;
    struct boot_grub_context ctx;
    boot_grub_enter(&ctx, &fs->slice);
    grub_fs_t driver = boot_grub_drivers[fs->kind];
    struct grub_file native = {.device = &ctx.device, .fs = driver};
    grub_err_t error = driver->fs_open(&native, path);
    unsigned directory = 0;
    if (!error)
        driver->fs_close(&native);
    else if (error == GRUB_ERR_BAD_FILE_TYPE)
    {
        ctx.error = GRUB_ERR_NONE;
        error = driver->fs_dir(&ctx.device, path, stop, NULL);
        directory = !error;
    }
    status = boot_grub_leave(&ctx, error);
    if (status)
        return status;
    grub_memset(out, 0, sizeof(*out));
    out->fs = fs;
    out->generation = fs->slice.generation;
    out->directory = directory;
    out->size = directory ? 0 : native.size;
    grub_memcpy(out->path, path, length + 1);
    return BOOT_OK;
}
boot_status_t boot_file_read(const struct boot_file *file, uint64_t offset, void *buffer,
                             size_t length, size_t *got)
{
    if (!got || (!buffer && length))
        return BOOT_E_INVALID;
    *got = 0;
    boot_status_t status = validate(file);
    if (status)
        return status;
    if (file->directory || offset > file->size)
        return BOOT_E_INVALID;
    if (length > file->size - offset)
        length = (size_t)(file->size - offset);
    if (!length)
        return BOOT_OK;
    if (length > INTPTR_MAX)
        return BOOT_E_INVALID;
    struct boot_grub_context ctx;
    boot_grub_enter(&ctx, &file->fs->slice);
    grub_fs_t driver = boot_grub_drivers[file->fs->kind];
    struct grub_file native = {.device = &ctx.device, .fs = driver};
    grub_err_t error = driver->fs_open(&native, (const char *)file->path);
    if (!error)
    {
        native.offset = offset;
        grub_ssize_t count = driver->fs_read(&native, buffer, length);
        if (count >= 0 && (size_t)count <= length)
            *got = (size_t)count;
        else if (!ctx.error)
            ctx.error = GRUB_ERR_READ_ERROR;
        error = ctx.error;
        driver->fs_close(&native);
    }
    status = boot_grub_leave(&ctx, error);
    if (status)
        *got = 0;
    return status;
}
struct listing
{
    const struct boot_file *directory;
    boot_dir_hook hook;
    void *opaque;
    boot_status_t status;
};
static int visit(const char *name, const struct grub_dirhook_info *info, void *opaque)
{
    struct listing *list = opaque;
    if (!grub_strcmp(name, ".") || !grub_strcmp(name, ".."))
        return 0;
    const char *parent = (const char *)list->directory->path;
    size_t a = grub_strlen(parent), b = grub_strlen(name);
    struct boot_file child;
    char path[sizeof(child.path)];
    if (a + b + 2 > sizeof(path))
    {
        list->status = BOOT_E_UNSUPPORTED;
        return 1;
    }
    grub_memcpy(path, parent, a);
    if (a && path[a - 1] != '/')
        path[a++] = '/';
    grub_memcpy(path + a, name, b + 1);
    if (info->dir)
    {
        grub_memset(&child, 0, sizeof(child));
        child.fs = list->directory->fs;
        child.generation = list->directory->generation;
        child.directory = 1;
        grub_memcpy(child.path, path, a + b + 1);
    }
    else
    {
        list->status = boot_file_open(list->directory->fs, path, &child);
        if (list->status)
            return 1;
    }
    child.id = info->inodeset ? info->inode : 0;
    list->status = list->hook(list->opaque, name, &child);
    return list->status != BOOT_OK;
}
boot_status_t boot_file_list(const struct boot_file *file, boot_dir_hook hook, void *opaque)
{
    if (!hook)
        return BOOT_E_INVALID;
    boot_status_t status = validate(file);
    if (status)
        return status;
    if (!file->directory)
        return BOOT_E_INVALID;
    struct boot_grub_context ctx;
    boot_grub_enter(&ctx, &file->fs->slice);
    struct listing list = {file, hook, opaque, BOOT_OK};
    grub_err_t error = boot_grub_drivers[file->fs->kind]->fs_dir(
        &ctx.device, (const char *)file->path, visit, &list);
    status = boot_grub_leave(&ctx, error);
    return status ? status : list.status;
}
