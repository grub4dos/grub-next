# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.24)
project(elf_fixture LANGUAGES C)
add_executable(elf-fixture module.c)
set_target_properties(elf-fixture PROPERTIES SUFFIX ".so")
target_compile_options(elf-fixture PRIVATE -fPIC -fvisibility=hidden -ffreestanding
    -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables)
target_link_options(elf-fixture PRIVATE -nostdlib -shared -Wl,-shared
    -Wl,-Bsymbolic,--hash-style=sysv,-e,boot_module_entry,--build-id=none)
if(CMAKE_C_COMPILER_TARGET MATCHES "^loongarch64")
    target_link_options(elf-fixture PRIVATE -Wl,-m,elf64loongarch,--no-relax)
endif()
