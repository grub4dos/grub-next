# SPDX-License-Identifier: GPL-3.0-or-later
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_COMPILER gcc)
set(CMAKE_ASM_COMPILER gcc)
set(BOOT_TARGET i386-pc CACHE STRING "Runtime target")
set(CMAKE_C_FLAGS_INIT "-m32 -march=pentium4 -msse2 -mfpmath=sse -fno-pic -fno-pie")
set(CMAKE_ASM_FLAGS_INIT "-m32 -march=pentium4 -msse2 -mfpmath=sse -fno-pic -fno-pie")
