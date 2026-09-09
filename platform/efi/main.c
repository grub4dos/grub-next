/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/efi.h>
extern boot_status_t boot_efi_platform(struct boot_context *, void *, struct boot_efi_system *);
extern void boot_efi_memory_test(struct boot_context *);
extern void boot_core_test(struct boot_context *);
extern void boot_module_probe(struct boot_context *, struct boot_efi_services *);
static struct boot_context context;
uintptr_t BOOT_EFI efi_main(void *, struct boot_efi_system *);
static uintptr_t(BOOT_EFI *volatile relocation_probe)(void *, struct boot_efi_system *) = efi_main;
/* Every real EFI entry ends in the common core. Test success is followed by
   deliberate panic/reset; only Phase 0's separately named probe returns. */
uintptr_t BOOT_EFI efi_main(void *image, struct boot_efi_system *system)
{
    boot_arch_fp_reset();
    boot_status_t s = boot_efi_platform(&context, image, system);
    if (s)
        boot_panic(&context, "efi-context", 0);
    if (relocation_probe != efi_main)
        boot_panic(&context, "pe-relocation", 0);
    if (boot_arch_fp_check())
        boot_panic(&context, "fp-state", 0);
    boot_console(&context, "BOOT:PASS:efi-context:" BOOT_TARGET_NAME "\r\n");
    boot_console(&context, "BOOT:PASS:fp-state\r\n");
    boot_core_test(&context);
    boot_efi_memory_test(&context);
    boot_module_probe(&context, system->services);
    boot_panic(&context, "phase1-reset", 1);
}
