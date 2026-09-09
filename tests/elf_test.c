/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include <assert.h>
#include <boot/elf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static uint64_t get(const unsigned char *p, unsigned n)
{
    uint64_t v = 0;
    for (unsigned i = 0; i < n; ++i)
        v |= (uint64_t)p[i] << (i * 8);
    return v;
}
static void put(unsigned char *p, uint64_t v, unsigned n)
{
    for (unsigned i = 0; i < n; ++i)
        p[i] = (unsigned char)(v >> (i * 8));
}
static size_t offset(const unsigned char *f, uint64_t address)
{
    size_t ph = (size_t)get(f + 32, 8);
    for (unsigned i = 0; i < get(f + 56, 2); ++i)
    {
        const unsigned char *h = f + ph + 56 * i;
        uint64_t start = get(h + 16, 8), size = get(h + 32, 8);
        if (get(h, 4) == 1 && address >= start && address - start < size)
            return (size_t)(get(h + 8, 8) + address - start);
    }
    abort();
}
static void reject(unsigned char *f, size_t n, void *memory, size_t capacity)
{
    struct boot_elf_image info = {123, 456, 789};
    memset(memory, 0xa5, capacity);
    assert(boot_elf_inspect(f, n, BOOT_ELF_X86_64, &info) != BOOT_OK);
    assert(info.size == 123 && info.alignment == 456 && info.entry_offset == 789);
    assert(boot_elf_load(f, n, BOOT_ELF_X86_64, memory, capacity, &info) != BOOT_OK);
    for (size_t i = 0; i < capacity; ++i)
        assert(((unsigned char *)memory)[i] == 0xa5);
}
int main(int argc, char **argv)
{
    assert(argc == 2 || argc == 3);
    FILE *file = fopen(argv[1], "rb");
    assert(file && !fseek(file, 0, SEEK_END));
    size_t n = (size_t)ftell(file);
    rewind(file);
    unsigned char *f = malloc(n), *copy = malloc(n);
    assert(f && copy && fread(f, 1, n, file) == n);
    fclose(file);
    struct boot_elf_image info;
    if (argc == 3)
    {
        enum boot_elf_machine machine = (enum boot_elf_machine)strtoul(argv[2], NULL, 0);
        assert(boot_elf_inspect(f, n, machine, &info) == BOOT_OK);
        size_t bytes = info.size + info.alignment;
        void *allocation = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
        assert(allocation != MAP_FAILED);
        void *destination = (void *)(((uintptr_t)allocation + info.alignment - 1) &
                                     ~(uintptr_t)(info.alignment - 1));
        assert(boot_elf_load(f, n, machine, destination, info.size, &info) == BOOT_OK);
        /* Independent reference check for every relative relocation and BSS. */
        unsigned word = f[4] == 2 ? 8 : 4, phsize = word == 8 ? 56 : 32;
        size_t phoff = (size_t)get(f + (word == 8 ? 32 : 28), word);
        unsigned count = (unsigned)get(f + (word == 8 ? 56 : 44), 2);
        uint64_t low = UINT64_MAX, dynoff = 0, dynbytes = 0, reloc = 0, relbytes = 0;
        for (unsigned i = 0; i < count; ++i)
        {
            unsigned char *h = f + phoff + i * phsize;
            uint64_t address = get(h + (word == 8 ? 16 : 8), word);
            if (get(h, 4) == 1 && address < low)
                low = address;
            if (get(h, 4) == 2)
            {
                dynoff = get(h + (word == 8 ? 8 : 4), word);
                dynbytes = get(h + (word == 8 ? 32 : 16), word);
            }
        }
        low &= ~(uint64_t)(info.alignment - 1);
        for (uint64_t i = dynoff; i < dynoff + dynbytes; i += 2 * word)
        {
            uint64_t tag = get(f + i, word);
            if (tag == (word == 8 ? 7u : 17u))
                reloc = get(f + i + word, word);
            if (tag == (word == 8 ? 8u : 18u))
                relbytes = get(f + i + word, word);
        }
        assert(relbytes);
        for (unsigned i = 0; i < count; ++i)
        {
            unsigned char *h = f + phoff + i * phsize;
            if (get(h, 4) != 1)
                continue;
            uint64_t address = get(h + (word == 8 ? 16 : 8), word);
            uint64_t fileoff = get(h + (word == 8 ? 8 : 4), word);
            uint64_t filesz = get(h + (word == 8 ? 32 : 16), word);
            uint64_t memsz = get(h + (word == 8 ? 40 : 20), word);
            for (uint64_t j = filesz; j < memsz; ++j)
                assert(((unsigned char *)destination)[address - low + j] == 0);
            if (reloc < address || reloc - address >= filesz)
                continue;
            const unsigned char *r = f + fileoff + reloc - address;
            for (uint64_t j = 0; j < relbytes; j += (word == 8 ? 24 : 8))
            {
                uint64_t slot = get(r + j, word), addend = 0;
                if (word == 8)
                    addend = get(r + j + 16, word);
                else
                    for (unsigned k = 0; k < count; ++k)
                    {
                        unsigned char *s = f + phoff + k * phsize;
                        uint64_t start = get(s + 8, 4), size = get(s + 16, 4);
                        if (get(s, 4) == 1 && slot >= start && slot - start < size)
                            addend = get(f + get(s + 4, 4) + slot - start, 4);
                    }
                assert(get((unsigned char *)destination + slot - low, word) ==
                       (uintptr_t)destination + addend - low);
            }
        }
        munmap(allocation, bytes);
        free(copy);
        free(f);
        puts("BOOT:PASS:elf-cross-image-relocation-bss");
        return 0;
    }
    assert(boot_elf_inspect(f, n, BOOT_ELF_X86_64, &info) == BOOT_OK);
    size_t capacity = (info.size + 4095) & ~(size_t)4095;
    void *memory = mmap(NULL, capacity, PROT_READ | PROT_WRITE | PROT_EXEC,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(memory != MAP_FAILED && info.alignment == 4096);
    assert(boot_elf_load(f, n, BOOT_ELF_X86_64, memory, capacity, &info) == BOOT_OK);
    int (*entry)(void) = (int (*)(void))((unsigned char *)memory + info.entry_offset);
    assert(entry() == 42); /* code, initialized data, RELATIVE pointer and zero BSS */
    assert(boot_elf_load(f, n, BOOT_ELF_X86_64, memory, capacity, &info) == BOOT_OK);
    assert(entry() == 42);
    assert(boot_elf_load(f, n, BOOT_ELF_X86_64, memory, 1, &info) == BOOT_E_NOMEM);
    assert(boot_elf_load(f, n, BOOT_ELF_X86_64, (char *)memory + 1, capacity, &info) ==
           BOOT_E_INVALID);
    assert(boot_elf_inspect(f, n, BOOT_ELF_ARM64, &info) == BOOT_E_UNSUPPORTED);
    size_t ph = (size_t)get(f + 32, 8), dynamic = 0, dynsize = 0, rela = 0, sym = 0;
    size_t last = 0;
    for (unsigned i = 0; i < get(f + 56, 2); ++i)
    {
        unsigned char *h = f + ph + 56 * i;
        size_t end = (size_t)(get(h + 8, 8) + get(h + 32, 8));
        if (end > last)
            last = end;
        if (get(h, 4) == 2)
        {
            dynamic = (size_t)get(h + 8, 8);
            dynsize = (size_t)get(h + 32, 8);
        }
    }
    for (size_t i = dynamic; i < dynamic + dynsize && get(f + i, 8); i += 16)
    {
        if (get(f + i, 8) == 7)
            rela = offset(f, get(f + i + 8, 8));
        if (get(f + i, 8) == 6)
            sym = offset(f, get(f + i + 8, 8));
    }
    assert(rela && sym);
    /* Section headers are not part of the runtime contract. */
    memcpy(copy, f, n);
    memset(copy + 40, 0, 8);
    memset(copy + 58, 0, 6);
    assert(boot_elf_load(copy, last, BOOT_ELF_X86_64, memory, capacity, &info) == BOOT_OK);
    assert(entry() == 42);
    for (size_t i = 0; i < last; ++i)
        reject(f, i, memory, capacity);
#define MUTATE(at, value, width)                                                                   \
    do                                                                                             \
    {                                                                                              \
        memcpy(copy, f, n);                                                                        \
        put(copy + (at), value, width);                                                            \
        reject(copy, n, memory, capacity);                                                         \
    } while (0)
    MUTATE(0, 0, 1);
    MUTATE(4, 1, 1);
    MUTATE(5, 2, 1);
    MUTATE(16, 2, 2);
    MUTATE(18, 183, 2);
    MUTATE(32, UINT64_MAX, 8);
    MUTATE(56, 0xffff, 2);
    MUTATE(ph, 7, 4);     /* TLS */
    MUTATE(ph, 3, 4);     /* interpreter */
    MUTATE(ph + 4, 7, 4); /* W+X */
    MUTATE(ph + 40, UINT64_MAX, 8);
    MUTATE(ph + 48, 3, 8);
    MUTATE(dynamic, 1, 8);                      /* NEEDED */
    MUTATE(dynamic, 22, 8);                     /* TEXTREL */
    MUTATE(dynamic, 35, 8);                     /* RELR */
    MUTATE(rela + 8, 1, 8);                     /* absolute relocation */
    MUTATE(rela + 8, UINT64_C(0x100000008), 8); /* symbol import */
    MUTATE(rela, UINT64_MAX, 8);
    MUTATE(rela, get(f + 24, 8), 8); /* text destination */
    MUTATE(rela + 16, UINT64_MAX, 8);
    MUTATE(rela + 24, get(f + rela, 8), 8);     /* duplicate slot */
    MUTATE(rela + 24, get(f + rela, 8) - 8, 8); /* unsorted slots */
    MUTATE(sym + 24 + 6, 0, 2);                 /* undefined */
    MUTATE(sym + 24 + 4, 0x1a, 1);              /* IFUNC */
    MUTATE(sym + 24, UINT32_MAX, 4);
    /* Deterministic bounded mutation smoke under sanitizers; accepted changes
     * are never executed. */
    uint32_t seed = 12345;
    for (unsigned i = 0; i < 10000; ++i)
    {
        memcpy(copy, f, n);
        seed = seed * 1664525u + 1013904223u;
        copy[seed % last] ^= (unsigned char)(1u << (i % 8));
        if (boot_elf_inspect(copy, n, BOOT_ELF_X86_64, &info) == BOOT_OK && info.size <= capacity &&
            info.alignment == 4096)
            assert(boot_elf_load(copy, n, BOOT_ELF_X86_64, memory, capacity, &info) == BOOT_OK);
    }
    munmap(memory, capacity);
    free(copy);
    free(f);
    puts("BOOT:PASS:elf-execution-bss-relative-malformed");
    return 0;
}
