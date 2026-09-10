/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../core/grub/context.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    struct boot_grub_context outer, inner;
    for (unsigned i = 0; i < 100; ++i)
    {
        boot_grub_enter(&outer, NULL);
        unsigned char *p = grub_malloc(1024 * 1024);
        assert(p);
        grub_memset(p, 0x5a, 1024 * 1024);
        grub_errno = GRUB_ERR_FILE_NOT_FOUND;
        boot_grub_enter(&inner, NULL);
        assert(grub_errno == GRUB_ERR_NONE);
        assert(!grub_malloc(SIZE_MAX));
        assert(boot_grub_leave(&inner, GRUB_ERR_NONE) == BOOT_E_NOMEM);
        assert(grub_errno == GRUB_ERR_FILE_NOT_FOUND);
        assert(p[0] == 0x5a && p[1024 * 1024 - 1] == 0x5a);
        assert(boot_grub_leave(&outer, GRUB_ERR_NONE) == BOOT_E_NOT_FOUND);
        /* Repeated leaked allocations above must be reclaimed at scope exit. */
    }
    boot_grub_enter(&outer, NULL);
    void *a = grub_malloc(1000), *b = grub_malloc(2000), *c = grub_malloc(3000);
    assert(a && b && c);
    grub_free(b);
    a = grub_realloc(a, 4000);
    assert(a);
    grub_free(c);
    grub_free(a);
    assert(grub_malloc(4 * 1024 * 1024));
    assert(!grub_malloc(1));
    outer.provider_error = BOOT_E_STALE;
    assert(boot_grub_leave(&outer, GRUB_ERR_BAD_FS) == BOOT_E_STALE);
    for (uint64_t n = UINT64_MAX; n > 1024; n >>= 1)
        for (uint64_t d = UINT64_MAX; d; d >>= 1)
        {
            uint64_t remainder;
            assert(grub_divmod64(n, d, &remainder) == n / d);
            assert(remainder == n % d);
        }
    puts("PASS: scoped errors, nested lifetime, bounded arena and division");
    return 0;
}
