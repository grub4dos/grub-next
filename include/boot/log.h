/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_LOG_H
#define BOOT_LOG_H
#include <boot/status.h>
/* COM1, 115200 8N1; polling is bounded. Debugcon mirrors every byte. */
void boot_log_init(void);
boot_status_t boot_log_write(const char *message);
#endif
