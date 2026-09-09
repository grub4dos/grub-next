# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.24)
project(boot_sdk NONE)
install(DIRECTORY ../include/boot DESTINATION include FILES_MATCHING PATTERN "module.h"
    PATTERN "elf.h" PATTERN "status.h")
install(FILES BootModule.cmake DESTINATION cmake)
install(DIRECTORY sample DESTINATION .)
