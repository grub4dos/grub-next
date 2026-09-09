/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/log.h>
#include <stdio.h>
#include <string.h>
static unsigned reads, serial_bytes, debug_bytes;
static uint8_t ready;
void out8(uint16_t port, uint8_t value)
{
    (void)value;
    if (port == 0x3f8)
        ++serial_bytes;
    if (port == 0xe9)
        ++debug_bytes;
}
uint8_t in8(uint16_t port)
{
    if (port != 0x3fd)
        return 0;
    ++reads;
    return ready;
}
#define CHECK(condition)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
        {                                                                                          \
            fprintf(stderr, "failed: %s\n", #condition);                                           \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
int main(void)
{
    CHECK(boot_log_write(0) == BOOT_E_INVALID);
    CHECK(reads == 0 && debug_bytes == 0);
    ready = 0x20;
    CHECK(boot_log_write("ok") == BOOT_OK);
    CHECK(serial_bytes == 2 && debug_bytes == 2);
    ready = 0;
    reads = serial_bytes = debug_bytes = 0;
    CHECK(boot_log_write("x") == BOOT_E_TIMEOUT);
    CHECK(reads == 100000 && serial_bytes == 0 && debug_bytes == 1);
    CHECK(strcmp(boot_status_string(12345), "unknown status") == 0);
    return 0;
}
