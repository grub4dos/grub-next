# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.24)
project(grub_next VERSION 0.0.0 LANGUAGES C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT BOOT_TARGET)
    set(BOOT_TARGET host)
endif()
if(NOT BOOT_TARGET MATCHES "^(host|i386-pc|i386-efi|x86_64-efi|arm64-efi|loongarch64-efi)$")
    message(FATAL_ERROR "Unsupported BOOT_TARGET: ${BOOT_TARGET}")
endif()
add_compile_options(-Wall -Wextra -Werror -Wdate-time
    "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=."
    "-ffile-prefix-map=${CMAKE_BINARY_DIR}=./build")
include_directories(include)
if(BOOT_TARGET STREQUAL "host")
    add_executable(boot-info host/main.c core/status.c)
    add_executable(boot-log-test tests/log_test.c platform/x86/log.c core/status.c)
    target_compile_definitions(boot-log-test PRIVATE BOOT_TEST_IO=1)
    enable_testing()
    add_test(NAME log-errors COMMAND boot-log-test)
    add_test(NAME host-version COMMAND boot-info --version)
    add_test(NAME host-invalid COMMAND boot-info --invalid)
    set_tests_properties(host-invalid PROPERTIES WILL_FAIL TRUE)
    include(cmake/phase1.cmake)
    include(cmake/elf-tests.cmake)
    include(cmake/modules.cmake)
    return()
endif()
add_compile_options(-ffreestanding -fno-stack-protector -fno-builtin
    -fno-unwind-tables -fno-asynchronous-unwind-tables)
if(BOOT_TARGET STREQUAL "i386-pc")
    add_executable(boot-stage2 platform/bios/start.S platform/bios/hello.c
        platform/x86/log.c core/status.c)
    set_target_properties(boot-stage2 PROPERTIES SUFFIX ".elf" LINK_DEPENDS "${CMAKE_SOURCE_DIR}/linker/i386-pc.ld")
    target_link_options(boot-stage2 PRIVATE -nostdlib -static
        "-Wl,-T,${CMAKE_SOURCE_DIR}/linker/i386-pc.ld" -Wl,--build-id=none)
elseif(BOOT_TARGET STREQUAL "x86_64-efi")
    add_executable(boot-hello platform/efi/hello.c platform/x86/log.c platform/x86/fp.S core/status.c)
    set_target_properties(boot-hello PROPERTIES SUFFIX ".efi")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/hello_image.h"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/embed.py"
            "$<TARGET_FILE:boot-hello>" "${CMAKE_BINARY_DIR}/hello_image.h"
        DEPENDS boot-hello tools/embed.py VERBATIM)
    add_executable(boot-efi-test tests/efi_parent.c platform/x86/log.c
        "${CMAKE_BINARY_DIR}/hello_image.h")
    target_include_directories(boot-efi-test PRIVATE "${CMAKE_BINARY_DIR}")
    set_target_properties(boot-efi-test PROPERTIES SUFFIX ".efi")

    # PE/COFF layout is defined by the native linker, never by ELF objcopy.
    foreach(image boot-hello boot-efi-test)
        if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
            target_link_options(${image} PRIVATE -nostdlib
                -Wl,--entry,efi_main,--subsystem,10,--no-insert-timestamp,--dynamicbase)
        else()
            target_link_options(${image} PRIVATE -nostdlib
                -Wl,/entry:efi_main,/subsystem:efi_application,/nodefaultlib,/timestamp:0,/dynamicbase)
        endif()
    endforeach()
else()
    # Phase 0 configuration/compile probe, not a bootable EFI image.
    add_library(boot-runtime STATIC core/status.c)
endif()

include(cmake/phase1.cmake)
include(cmake/modules.cmake)
