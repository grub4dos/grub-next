# SPDX-License-Identifier: GPL-3.0-or-later
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/resource.cpio"
        "${CMAKE_BINARY_DIR}/resource_image.h" "${CMAKE_BINARY_DIR}/resource.manifest.sha256"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/pack_resources.py"
        --output "${CMAKE_BINARY_DIR}/resource.cpio" --header "${CMAKE_BINARY_DIR}/resource_image.h"
        --file "boot.lua=${CMAKE_SOURCE_DIR}/resources/boot.lua"
        --file "fonts/minimal.hex=${CMAKE_SOURCE_DIR}/resources/fonts/minimal.hex"
        --file "modules/${BOOT_TARGET}/sample.so=${samples}/build/sample.so"
        --file "modules/${BOOT_TARGET}/failing.so=${samples}/build/failing.so"
    DEPENDS "${CMAKE_BINARY_DIR}/module_images.h" tools/pack_resources.py
        resources/boot.lua resources/fonts/minimal.hex VERBATIM)
add_custom_target(boot-resources ALL DEPENDS "${CMAKE_BINARY_DIR}/resource.cpio")
if(BOOT_TARGET STREQUAL "host")
    add_executable(boot-archive-test tests/archive_test.c core/archive.c core/sha256.c
        "${CMAKE_BINARY_DIR}/resource_image.h")
    target_include_directories(boot-archive-test PRIVATE "${CMAKE_BINARY_DIR}")
    target_compile_options(boot-archive-test PRIVATE -UNDEBUG)
    if(BOOT_SANITIZERS)
        target_compile_options(boot-archive-test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(boot-archive-test PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME resource-archive COMMAND boot-archive-test)
    add_executable(boot-resource-input-test tests/resource_input_test.c platform/resource.c
        core/archive.c core/sha256.c "${CMAKE_BINARY_DIR}/resource_image.h")
    target_include_directories(boot-resource-input-test PRIVATE "${CMAKE_BINARY_DIR}")
    target_compile_definitions(boot-resource-input-test PRIVATE BOOT_MODULE_TARGET_ID=1)
    target_compile_options(boot-resource-input-test PRIVATE -UNDEBUG)
    if(BOOT_SANITIZERS)
        target_compile_options(boot-resource-input-test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(boot-resource-input-test PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME resource-input COMMAND boot-resource-input-test)
    add_test(NAME resource-packer COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/archive_pack.py"
        $<TARGET_FILE:boot-archive-test>)
else()
    target_sources(boot-core PRIVATE core/archive.c core/sha256.c platform/resource.c
        "${CMAKE_BINARY_DIR}/resource_image.h")
    target_compile_definitions(boot-core PRIVATE BOOT_RESOURCE_TARGET="${BOOT_TARGET}")
endif()
