/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_GRUB_ERR_H
#define BOOT_GRUB_ERR_H
#include <grub/i18n.h>
typedef enum
{
    GRUB_ERR_NONE,
    GRUB_ERR_OUT_OF_MEMORY,
    GRUB_ERR_BAD_FILE_TYPE,
    GRUB_ERR_FILE_NOT_FOUND,
    GRUB_ERR_BAD_FILENAME,
    GRUB_ERR_BAD_FS,
    GRUB_ERR_BAD_NUMBER,
    GRUB_ERR_OUT_OF_RANGE,
    GRUB_ERR_NOT_IMPLEMENTED_YET,
    GRUB_ERR_SYMLINK_LOOP,
    GRUB_ERR_EOF,
    GRUB_ERR_READ_ERROR
} grub_err_t;
/* Private source-compatibility lvalue; storage belongs to a scoped operation. */
grub_err_t *boot_grub_error_slot(void);
#define grub_errno (*boot_grub_error_slot())
grub_err_t grub_error(grub_err_t, const char *, ...);
#endif
