# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.24)
project(boot_sample C)
include("${BOOT_SDK}/cmake/BootModule.cmake")
boot_add_module(sample module.c)
boot_add_module(failing module.c)
target_compile_definitions(failing PRIVATE BOOT_SAMPLE_FAIL=1)
