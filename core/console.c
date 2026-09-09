/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/context.h>
#define RING_SIZE 4096
static char ring[RING_SIZE];
static size_t position, used;
void boot_ring_write(const char *s)
{
    if (!s)
        return;
    while (*s)
    {
        ring[position] = *s++;
        position = (position + 1) % RING_SIZE;
        if (used < RING_SIZE)
            ++used;
    }
}
size_t boot_ring_read(char *out, size_t capacity)
{
    size_t n = used < capacity ? used : capacity;
    if (!out)
        return 0;
    size_t start = (position + RING_SIZE - n) % RING_SIZE;
    for (size_t i = 0; i < n; ++i)
        out[i] = ring[(start + i) % RING_SIZE];
    return n;
}
boot_status_t boot_console(struct boot_context *c, const char *s)
{
    if (!s)
        return BOOT_E_INVALID;
    boot_ring_write(s);
    return c && c->platform.console ? c->platform.console(s) : BOOT_E_UNSUPPORTED;
}
_Noreturn void boot_panic(struct boot_context *c, const char *reason, int reset)
{
    boot_console(c, "BOOT:PANIC:");
    boot_console(c, reason ? reason : "unknown");
    boot_console(c, "\r\n");
    if (c && reset && c->platform.reset)
        c->platform.reset();
    if (c && c->platform.halt)
        c->platform.halt();
    for (;;)
    { /* Last resort never depends on a higher-level service. */
    }
}
