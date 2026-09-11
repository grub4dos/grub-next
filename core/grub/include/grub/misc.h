/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_GRUB_MISC_H
#define BOOT_GRUB_MISC_H
#include <grub/err.h>
#include <grub/types.h>
#define grub_min(a, b) ((a) < (b) ? (a) : (b))
#define grub_max(a, b) ((a) > (b) ? (a) : (b))
#define grub_dprintf(...) ((void)0)
#define ALIGN_UP(n, a) (((n) + (a) - 1) & ~((a) - 1))
#define grub_printf(...) ((void)0)
_Noreturn void grub_fatal(const char *, ...);
char *grub_strchr(const char *, int);
char *grub_strstr(const char *, const char *);
unsigned long long grub_strtoull(const char *, const char **, int);
unsigned long grub_strtoul(const char *, const char **, int);
static inline int grub_isdigit(int c)
{
    return c >= '0' && c <= '9';
}
static inline int grub_isspace(int c)
{
    return c == ' ' || (c >= 9 && c <= 13);
}
void *grub_memcpy(void *, const void *, grub_size_t);
void *grub_memmove(void *, const void *, grub_size_t);
void *grub_memset(void *, int, grub_size_t);
int grub_memcmp(const void *, const void *, grub_size_t);
grub_size_t grub_strlen(const char *);
int grub_strcmp(const char *, const char *);
int grub_strncmp(const char *, const char *, grub_size_t);
int grub_strcasecmp(const char *, const char *);
char *grub_strcpy(char *, const char *);
char *grub_strdup(const char *);
char *grub_strndup(const char *, grub_size_t);
char *grub_strrchr(const char *, int);
char *grub_xasprintf(const char *, ...);
grub_uint64_t grub_divmod64(grub_uint64_t, grub_uint64_t, grub_uint64_t *);
static inline int grub_tolower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}
static inline int grub_toupper(int c)
{
    return c >= 'a' && c <= 'z' ? c - 32 : c;
}
static inline int grub_isalpha(int c)
{
    return grub_tolower(c) >= 'a' && grub_tolower(c) <= 'z';
}
#endif
