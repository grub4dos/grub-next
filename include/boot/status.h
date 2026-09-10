/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_STATUS_H
#define BOOT_STATUS_H
#include <stdint.h>
typedef uint32_t boot_status_t;
enum
{
    BOOT_OK = 0,
    BOOT_E_INVALID = 1,
    BOOT_E_UNSUPPORTED = 2,
    BOOT_E_IO = 3,
    BOOT_E_TIMEOUT = 4,
    BOOT_E_NOMEM = 5,
    BOOT_E_STALE = 6,
    BOOT_E_NOT_FOUND = 7,
    BOOT_E_CORRUPT = 8
};
const char *boot_status_string(boot_status_t status);
#endif
