/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/context.h>
#include <boot/log.h>
static void out8(uint16_t p, uint8_t b)
{
    __asm__ volatile("outb %0,%1" ::"a"(b), "Nd"(p));
}
static uint8_t in8(uint16_t p)
{
    uint8_t b;
    __asm__ volatile("inb %1,%0" : "=a"(b) : "Nd"(p));
    return b;
}
static unsigned cursor;
static boot_status_t console(const char *s)
{
    boot_status_t result = boot_log_write(s);
    volatile uint16_t *screen = (volatile uint16_t *)0xb8000;
    for (; *s; ++s)
    {
        if (*s == '\r')
            cursor -= cursor % 80;
        else if (*s == '\n')
            cursor += 80 - cursor % 80;
        else
            screen[cursor++] = 0x0700 | (uint8_t)*s;
        if (cursor >= 80 * 25)
        {
            for (unsigned i = 0; i < 80 * 24; ++i)
                screen[i] = screen[i + 80];
            for (unsigned i = 80 * 24; i < 80 * 25; ++i)
                screen[i] = 0x0720;
            cursor = 80 * 24;
        }
    }
    return result;
}
static uint8_t rtc(unsigned r)
{
    uint8_t previous = in8(0x70);
    out8(0x70, (previous & 0x80) | r);
    uint8_t value = in8(0x71);
    out8(0x70, previous);
    return value;
}
static uint8_t decimal(uint8_t v)
{
    return (v >> 4) * 10 + (v & 15);
}
static boot_status_t time_read(struct boot_time *t)
{
    if (!t)
        return BOOT_E_INVALID;
    for (unsigned tries = 0; tries < 10000; ++tries)
    {
        if (rtc(10) & 0x80)
            continue;
        uint8_t sec = rtc(0), min = rtc(2), hour = rtc(4);
        uint8_t day = rtc(7), month = rtc(8), year = rtc(9), mode = rtc(11);
        if ((rtc(10) & 0x80) || rtc(0) != sec)
            continue;
        unsigned pm = hour & 0x80;
        hour &= 127;
        if (!(mode & 4))
        {
            sec = decimal(sec);
            min = decimal(min);
            hour = decimal(hour);
            day = decimal(day);
            month = decimal(month);
            year = decimal(year);
        }
        if (!(mode & 2))
        {
            hour %= 12;
            if (pm)
                hour += 12;
        }
        *t = (struct boot_time){2000 + year, month, day, hour, min, sec};
        return BOOT_OK;
    }
    return BOOT_E_TIMEOUT;
}
static _Noreturn void halt(void)
{
    for (;;)
        __asm__ volatile("cli; hlt");
}
static _Noreturn void reset(void)
{
    out8(0xcf9, 2);
    out8(0xcf9, 6);
    for (unsigned i = 0; i < 100000; ++i)
        if (!(in8(0x64) & 2))
        {
            out8(0x64, 0xfe);
            break;
        }
    halt();
}
void boot_bios_platform(struct boot_context *c)
{
    boot_log_init();
    c->platform = (struct boot_platform){console, time_read, reset, halt};
}
