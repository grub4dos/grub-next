/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/status.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv)
{
    if (argc != 2 || strcmp(argv[1], "--version"))
    {
        fputs("usage: boot-info --version\n", stderr);
        return 2;
    }
    printf("grub-next Phase 0; status=%s\n", boot_status_string(BOOT_OK));
    return 0;
}
