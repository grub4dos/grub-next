# SPDX-License-Identifier: GPL-3.0-or-later
find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(GLOB_RECURSE BOOT_GRUB_INPUTS CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/vendor/grub/*" "${CMAKE_SOURCE_DIR}/patches/grub/*.patch")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${BOOT_GRUB_INPUTS}
    "${CMAKE_SOURCE_DIR}/tools/prepare_grub.py" "${CMAKE_SOURCE_DIR}/tools/import_grub.py")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/prepare_grub.py"
    "${CMAKE_BINARY_DIR}/grub-fs" COMMAND_ERROR_IS_FATAL ANY)
set(BOOT_GRUB_FS)
foreach(name fat ext2 iso9660 ntfs ntfscomp fshelp)
    list(APPEND BOOT_GRUB_FS "${CMAKE_BINARY_DIR}/grub-fs/${name}.c")
endforeach()
foreach(name loopback diskfilter lvm mdraid1x_linux raid5_recover raid6_recover list)
    list(APPEND BOOT_GRUB_FS "${CMAKE_BINARY_DIR}/grub-fs/${name}.c")
endforeach()
set(BOOT_STORAGE core/storage/block.c core/storage/partition.c core/storage/fs.c
    core/grub/compat.c core/grub/volume.c ${BOOT_GRUB_FS})
set_source_files_properties(${BOOT_GRUB_FS} PROPERTIES COMPILE_OPTIONS
    "-Wno-unused-parameter;-Wno-sign-compare;-Wno-unused-but-set-variable;-Wno-unused-variable")
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    # fshelp's historical opaque-node callbacks deliberately use per-driver tags.
    set_property(SOURCE ${BOOT_GRUB_FS} APPEND PROPERTY COMPILE_OPTIONS -fno-sanitize=function)
elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    # GCC cannot prove the scoped error slot remains nonzero on decomp_get16's
    # error return; that branch returns before the output is consumed.
    set_property(SOURCE "${CMAKE_BINARY_DIR}/grub-fs/ntfscomp.c" APPEND PROPERTY
        COMPILE_OPTIONS -Wno-maybe-uninitialized)
endif()
if(BOOT_TARGET STREQUAL "host")
    set(BOOT_STORAGE_RUNTIME ${BOOT_STORAGE})
    list(APPEND BOOT_STORAGE host/grub_alloc.c)
    add_executable(boot-storage-test tests/storage_test.c ${BOOT_STORAGE})
    add_executable(boot-storage-read host/storage_read.c host/block.c ${BOOT_STORAGE})
    add_executable(boot-storage-fuzz tests/storage_fuzz.c ${BOOT_STORAGE})
    add_executable(boot-volume-test tests/volume_test.c host/block.c ${BOOT_STORAGE})
    target_compile_options(boot-volume-test PRIVATE -UNDEBUG)
    add_executable(boot-grub-compat-test tests/grub_compat_test.c ${BOOT_STORAGE_RUNTIME}
        core/grub/arena.c)
    target_compile_options(boot-grub-compat-test PRIVATE -UNDEBUG)
    add_executable(boot-efi-storage-test tests/efi_storage_test.c platform/efi/storage.c
        core/storage/block.c core/sha256.c)
    target_compile_options(boot-efi-storage-test PRIVATE -UNDEBUG)
    target_compile_options(boot-storage-fuzz PRIVATE -UNDEBUG)
    target_compile_options(boot-storage-test PRIVATE -UNDEBUG)
    foreach(t boot-storage-test boot-storage-read boot-storage-fuzz boot-grub-compat-test boot-volume-test)
        target_include_directories(${t} PRIVATE core/grub/include vendor/grub/include)
    endforeach()
    if(BOOT_SANITIZERS)
        foreach(t boot-storage-test boot-storage-read boot-storage-fuzz boot-efi-storage-test boot-grub-compat-test boot-volume-test)
            target_compile_options(${t} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${t} PRIVATE -fsanitize=address,undefined)
        endforeach()
    endif()
    add_test(NAME storage-core COMMAND boot-storage-test)
    add_test(NAME grub-compat-context COMMAND boot-grub-compat-test)
    add_test(NAME efi-storage-media COMMAND boot-efi-storage-test)
else()
    target_sources(boot-core PRIVATE ${BOOT_STORAGE} core/grub/arena.c tests/storage_probe.c core/status.c)
    target_include_directories(boot-core PRIVATE core/grub/include vendor/grub/include)
    if(BOOT_TARGET STREQUAL "i386-pc")
        target_sources(boot-core PRIVATE platform/bios/storage.c platform/bios/disk_thunk.S)
    else()
        target_sources(boot-core PRIVATE platform/efi/storage.c)
        if(NOT BOOT_TARGET STREQUAL "loongarch64-efi")
            # Firmware provides an already committed stack, with no host guard-page ABI.
            target_compile_options(boot-core PRIVATE -mno-stack-arg-probe)
        endif()
    endif()
endif()
