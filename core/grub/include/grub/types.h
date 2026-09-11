/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_GRUB_TYPES_H
#define BOOT_GRUB_TYPES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint8_t grub_uint8_t;
typedef uint16_t grub_uint16_t;
typedef uint32_t grub_uint32_t;
typedef uint64_t grub_uint64_t;
typedef int8_t grub_int8_t;
typedef int16_t grub_int16_t;
typedef int32_t grub_int32_t;
typedef int64_t grub_int64_t;
typedef size_t grub_size_t;
typedef intptr_t grub_ssize_t;
typedef uintptr_t grub_addr_t;
typedef uint64_t grub_off_t;
typedef uint64_t grub_disk_addr_t;
#define GRUB_PACKED __attribute__((packed))
#define GRUB_UNUSED __attribute__((unused))
#define GRUB_FILE __FILE__
#define GRUB_SIZE_MAX SIZE_MAX
#define GRUB_SSIZE_MAX INTPTR_MAX
#define GRUB_UINT_MAX UINT32_MAX
#define GRUB_INT_MAX INT32_MAX
#define GRUB_DISK_MAX_MAX_AGGLOMERATE 1
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define COMPILE_TIME_ASSERT(a) _Static_assert((a), #a)
#define grub_cpu_to_le16(a) ((uint16_t)(a))
#define grub_cpu_to_le32(a) ((uint32_t)(a))
#define grub_cpu_to_le64(a) ((uint64_t)(a))
#define grub_cpu_to_le16_compile_time(a) ((uint16_t)(a))
#define grub_cpu_to_le32_compile_time(a) ((uint32_t)(a))
#define grub_le_to_cpu16(a) ((uint16_t)(a))
#define grub_le_to_cpu32(a) ((uint32_t)(a))
#define grub_le_to_cpu64(a) ((uint64_t)(a))
#define grub_be_to_cpu16(a) __builtin_bswap16(a)
#define grub_be_to_cpu32(a) __builtin_bswap32(a)
#define grub_be_to_cpu64(a) __builtin_bswap64(a)
static inline uint16_t grub_get_unaligned16(const void *v)
{
    const uint8_t *p = v;
    return p[0] | (uint16_t)p[1] << 8;
}
static inline uint32_t grub_get_unaligned32(const void *v)
{
    const uint8_t *p = v;
    return grub_get_unaligned16(p) | (uint32_t)grub_get_unaligned16(p + 2) << 16;
}
static inline uint64_t grub_get_unaligned64(const void *v)
{
    const uint8_t *p = v;
    return grub_get_unaligned32(p) | (uint64_t)grub_get_unaligned32(p + 4) << 32;
}
static inline void grub_set_unaligned32(void *v, uint32_t n)
{
    uint8_t *p = v;
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(n >> (i * 8));
}
#endif
