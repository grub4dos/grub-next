/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/status.h>
const char *boot_status_string(boot_status_t status)
{
    switch (status) {
    case BOOT_OK: return "ok";
    case BOOT_E_INVALID: return "invalid argument";
    case BOOT_E_UNSUPPORTED: return "unsupported";
    case BOOT_E_IO: return "I/O error";
    case BOOT_E_TIMEOUT: return "timeout";
    default: return "unknown status";
    }
}
