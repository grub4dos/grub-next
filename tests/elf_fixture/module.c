/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Loader execution fixture, intentionally not the future module ABI. */
static int data = 37;
static int bss[32];
static int *volatile pointer = &data;
static int *volatile zero_pointer = &bss[31];
__attribute__((visibility("default"))) int boot_module_entry(void)
{
    if (*zero_pointer != 0)
        return -1;
    *zero_pointer = 5;
    return *pointer + *zero_pointer;
}
