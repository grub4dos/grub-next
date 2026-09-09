# SPDX-License-Identifier: GPL-3.0-or-later
add_subdirectory(tests/elf_fixture)
add_executable(boot-elf-test tests/elf_test.c core/elf.c)
target_compile_options(boot-elf-test PRIVATE -UNDEBUG)
if(BOOT_SANITIZERS)
    target_compile_options(boot-elf-test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(boot-elf-test PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME elf-loader COMMAND boot-elf-test $<TARGET_FILE:elf-fixture>)
