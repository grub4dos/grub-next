# SPDX-License-Identifier: GPL-3.0-or-later
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_ASM_COMPILER x86_64-w64-mingw32-gcc)
set(BOOT_TARGET x86_64-efi CACHE STRING "Runtime target")
set(CMAKE_C_FLAGS_INIT "-march=x86-64 -mno-red-zone")
set(CMAKE_ASM_FLAGS_INIT "-march=x86-64 -mno-red-zone")
