# SPDX-License-Identifier: GPL-3.0-or-later
get_filename_component(BOOT_SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
function(boot_add_module name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE "${BOOT_SDK_ROOT}/include")
    target_compile_definitions(${name} PRIVATE BOOT_SAMPLE_TARGET=${BOOT_MODULE_TARGET_ID})
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror -std=c11
        -fPIC -fvisibility=hidden -ffreestanding -fno-builtin -fno-stack-protector
        -fno-unwind-tables -fno-asynchronous-unwind-tables -Wdate-time
        "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.")
    target_link_options(${name} PRIVATE -nostdlib -shared -Wl,-shared
        -Wl,-Bsymbolic,--no-undefined,--hash-style=sysv,-e,boot_module_entry,--build-id=none)
    if(CMAKE_C_COMPILER_TARGET MATCHES "^loongarch64")
        target_link_options(${name} PRIVATE -Wl,-m,elf64loongarch,--no-relax)
    endif()
    set_target_properties(${name} PROPERTIES SUFFIX ".so")
endfunction()
