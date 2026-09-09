/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/log.h>
#ifdef BOOT_TEST_IO
extern void out8(uint16_t port, uint8_t value);
extern uint8_t in8(uint16_t port);
#else
static void out8(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}
static uint8_t in8(uint16_t port)
{
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
#endif
void boot_log_init(void)
{
    out8(0x3f9, 0);
    out8(0x3fb, 0x80);
    out8(0x3f8, 1);
    out8(0x3f9, 0);
    out8(0x3fb, 3);
    out8(0x3fa, 0xc7);
    out8(0x3fc, 0x0b);
}
boot_status_t boot_log_write(const char *message)
{
    boot_status_t status = BOOT_OK;
    if (!message)
        return BOOT_E_INVALID;
    while (*message)
    {
        uint8_t byte = (uint8_t)*message++;
        out8(0xe9, byte);
        unsigned tries = 100000;
        while (tries && !(in8(0x3fd) & 0x20))
            --tries;
        if (tries)
            out8(0x3f8, byte);
        else
            status = BOOT_E_TIMEOUT;
    }
    return status;
}
