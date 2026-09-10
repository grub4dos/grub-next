# SPDX-License-Identifier: GPL-3.0-or-later
# Install then build a copied sample using only the public SDK, outside the source tree.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(BOOT_MODULE_CLANG clang REQUIRED)
set(module_id 3)
set(module_arch x86_64)
set(module_flags "-march=x86-64 -mno-red-zone")
if(BOOT_TARGET MATCHES "^i386")
    set(module_id 1)
    if(BOOT_TARGET STREQUAL "i386-efi")
        set(module_id 2)
    endif()
    set(module_arch i386)
    set(module_flags "-march=pentium4 -msse2 -mfpmath=sse")
elseif(BOOT_TARGET STREQUAL "arm64-efi")
    set(module_id 4)
    set(module_arch aarch64)
    set(module_flags "-march=armv8-a")
elseif(BOOT_TARGET STREQUAL "loongarch64-efi")
    set(module_id 5)
    set(module_arch loongarch64)
    set(module_flags "-march=loongarch64 -mabi=lp64d")
endif()
set(sdk "${CMAKE_BINARY_DIR}/external-sdk")
set(samples "${CMAKE_BINARY_DIR}/external-sample")
file(GLOB sdk_headers CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/include/boot/*.h")
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/module_images.h"
    COMMAND ${CMAKE_COMMAND} -S "${CMAKE_SOURCE_DIR}/sdk" -B "${CMAKE_BINARY_DIR}/sdk-install" -G Ninja
        "-DCMAKE_INSTALL_PREFIX=${sdk}"
    COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}/sdk-install"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${sdk}/sample" "${samples}"
    COMMAND ${CMAKE_COMMAND} -S "${samples}" -B "${samples}/build" -G Ninja
        "-DBOOT_SDK=${sdk}" "-DBOOT_MODULE_TARGET_ID=${module_id}"
        -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
        "-DCMAKE_C_COMPILER=${BOOT_MODULE_CLANG}" "-DCMAKE_C_COMPILER_TARGET=${module_arch}-none-elf"
        "-DCMAKE_C_FLAGS=${module_flags}" -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld
    COMMAND ${CMAKE_COMMAND} --build "${samples}/build"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/embed_module.py"
        "${samples}/build/sample.so" "${samples}/build/failing.so" "${CMAKE_BINARY_DIR}/module_images.h"
    DEPENDS sdk/CMakeLists.txt sdk/BootModule.cmake sdk/sample/CMakeLists.txt sdk/sample/module.c
        tools/embed_module.py ${sdk_headers} VERBATIM)
if(BOOT_TARGET STREQUAL "host")
    add_executable(boot-module-test tests/module_test.c core/module.c core/elf.c
        "${CMAKE_BINARY_DIR}/module_images.h")
    target_include_directories(boot-module-test PRIVATE "${CMAKE_BINARY_DIR}")
    target_compile_options(boot-module-test PRIVATE -UNDEBUG)
    if(BOOT_SANITIZERS)
        target_compile_options(boot-module-test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(boot-module-test PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME module-abi COMMAND boot-module-test)
else()
    target_sources(boot-core PRIVATE core/module.c platform/module.c tests/module_probe.c)
    target_include_directories(boot-core PRIVATE "${CMAKE_BINARY_DIR}")
    target_compile_definitions(boot-core PRIVATE BOOT_MODULE_TARGET_ID=${module_id})
endif()
