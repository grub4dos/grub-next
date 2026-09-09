# SPDX-License-Identifier: GPL-3.0-or-later
set(BOOT_COMMON core/memory.c core/physical.c core/context.c core/console.c core/string.c core/efi_memory.c core/elf.c)
if(BOOT_TARGET STREQUAL "host")
    add_executable(boot-core-test tests/core_test.c ${BOOT_COMMON})
    target_compile_options(boot-core-test PRIVATE -UNDEBUG)
    option(BOOT_SANITIZERS "Host core tests with ASan and UBSan" OFF)
    if(BOOT_SANITIZERS)
        target_compile_options(boot-core-test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(boot-core-test PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME core-memory-context COMMAND boot-core-test)
    return()
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
if(BOOT_TARGET STREQUAL "i386-pc")
    add_executable(boot-core platform/bios/entry.S platform/bios/main.c
        platform/bios/platform.c platform/bios/physical.c platform/bios/resident.c
        platform/bios/resident.S platform/x86/log.c tests/core_probe.c ${BOOT_COMMON})
    target_link_options(boot-core PRIVATE -nostdlib -static
        "-Wl,-T,${CMAKE_SOURCE_DIR}/linker/i386-core.ld" -Wl,--build-id=none)
    set_target_properties(boot-core PROPERTIES SUFFIX ".elf"
        LINK_DEPENDS "${CMAKE_SOURCE_DIR}/linker/i386-core.ld")
    find_program(BOOT_OBJCOPY NAMES llvm-objcopy-18 llvm-objcopy objcopy REQUIRED)
    add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/boot-core.bin"
        COMMAND ${BOOT_OBJCOPY} -O binary --set-section-flags .bss=alloc,load,contents
            $<TARGET_FILE:boot-core> "${CMAKE_BINARY_DIR}/boot-core.bin"
        DEPENDS boot-core VERBATIM)
    add_executable(boot-linux-setup platform/bios/linux_setup.S)
    target_link_options(boot-linux-setup PRIVATE -nostdlib -static
        -Wl,-Ttext=0x268,--oformat=binary,-e,setup_start,--build-id=none)
    set_target_properties(boot-linux-setup PROPERTIES SUFFIX ".bin")
    add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/boot-linux.bz"
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/linux_image.py"
            "${CMAKE_BINARY_DIR}/boot-core.bin" "${CMAKE_BINARY_DIR}/boot-linux.bz"
            $<TARGET_FILE:boot-linux-setup>
        DEPENDS "${CMAKE_BINARY_DIR}/boot-core.bin" boot-linux-setup tools/linux_image.py VERBATIM)
    add_custom_target(boot-linux ALL DEPENDS "${CMAKE_BINARY_DIR}/boot-linux.bz")
else()
    add_executable(boot-core platform/efi/main.c platform/efi/platform.c platform/efi/fp.c
        tests/core_probe.c tests/efi_memory_probe.c ${BOOT_COMMON})
    target_compile_definitions(boot-core PRIVATE BOOT_TARGET_NAME="${BOOT_TARGET}")
    if(BOOT_TARGET STREQUAL "loongarch64-efi")
        target_sources(boot-core PRIVATE platform/efi/loongarch_entry.S)
        set_source_files_properties(platform/efi/main.c PROPERTIES COMPILE_DEFINITIONS efi_main=boot_efi_main)
        target_compile_options(boot-core PRIVATE -fpie -fvisibility=hidden)
        target_link_options(boot-core PRIVATE -nostdlib -pie -Wl,-m,elf64loongarch,-Bsymbolic,--no-relax,-z,norelro
            "-Wl,-T,${CMAKE_SOURCE_DIR}/linker/loongarch-efi.ld" -Wl,--build-id=none)
        set_target_properties(boot-core PROPERTIES SUFFIX ".elf"
            LINK_DEPENDS "${CMAKE_SOURCE_DIR}/linker/loongarch-efi.ld")
        add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/boot-core.efi"
            COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/loongarch_pe.py"
                $<TARGET_FILE:boot-core> "${CMAKE_BINARY_DIR}/boot-core.efi"
            DEPENDS boot-core tools/loongarch_pe.py VERBATIM)
        add_custom_target(boot-core-pe ALL DEPENDS "${CMAKE_BINARY_DIR}/boot-core.efi")
    else()
        if(BOOT_TARGET STREQUAL "i386-efi")
            target_compile_options(boot-core PRIVATE --target=i686-pc-windows-msvc)
            target_link_options(boot-core PRIVATE --target=i686-pc-windows-msvc)
        elseif(BOOT_TARGET STREQUAL "arm64-efi")
            target_compile_options(boot-core PRIVATE --target=aarch64-pc-windows-msvc)
            target_link_options(boot-core PRIVATE --target=aarch64-pc-windows-msvc)
        endif()
        set_target_properties(boot-core PROPERTIES SUFFIX ".efi")
        if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
            target_link_options(boot-core PRIVATE -nostdlib
                -Wl,--entry,efi_main,--subsystem,10,--no-insert-timestamp,--dynamicbase)
        else()
            target_link_options(boot-core PRIVATE -nostdlib
                -Wl,/entry:efi_main,/subsystem:efi_application,/nodefaultlib,/timestamp:0,/dynamicbase)
        endif()
    endif()
endif()
