/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/elf.h>
#include <stdint.h>

/* Deliberately small ELF profile: LE, SysV hash, REL(i386)/RELA(64-bit),
 * symbol-free RELATIVE relocations only. Read wire fields bytewise so malformed
 * alignment never becomes an unaligned C access. All tables are read from file,
 * never from a relocation destination. */
#define LIMIT (16u * 1024u * 1024u)
#define COUNT_LIMIT 65536u
struct segment
{
    uint64_t offset, address, filesz, memsz, align;
    uint32_t type, flags;
};
struct parser
{
    const unsigned char *file;
    size_t length;
    unsigned wide, word, count;
    uint32_t relative;
    struct segment segments[32];
    uint64_t low, high, alignment, entry;
    uint64_t tags[38];
    uint64_t seen;
};
static uint64_t read_le(const unsigned char *p, unsigned n)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < n; ++i)
        value |= (uint64_t)p[i] << (8 * i);
    return value;
}
static int range(uint64_t start, uint64_t size, uint64_t bound)
{
    return start <= bound && size <= bound - start;
}
static int power2(uint64_t n)
{
    return n && !(n & (n - 1));
}
static const unsigned char *backed(const struct parser *p, uint64_t address, uint64_t size)
{
    for (unsigned i = 0; i < p->count; ++i)
    {
        const struct segment *s = &p->segments[i];
        if (s->type == 1 && address >= s->address && range(address - s->address, size, s->filesz))
            return p->file + (size_t)(s->offset + address - s->address);
    }
    return NULL;
}
static int mapped(const struct parser *p, uint64_t address, uint64_t size, unsigned flags)
{
    for (unsigned i = 0; i < p->count; ++i)
    {
        const struct segment *s = &p->segments[i];
        if (s->type == 1 && (s->flags & flags) == flags && address >= s->address &&
            range(address - s->address, size, s->memsz))
            return 1;
    }
    return 0;
}
static boot_status_t headers(struct parser *p, enum boot_elf_machine machine)
{
    const unsigned char *f = p->file;
    if (p->length < 16 || f[0] != 0x7f || f[1] != 'E' || f[2] != 'L' || f[3] != 'F')
        return BOOT_E_INVALID;
    switch (machine)
    {
    case BOOT_ELF_I386:
        p->wide = 0;
        p->relative = 8;
        break;
    case BOOT_ELF_X86_64:
        p->wide = 1;
        p->relative = 8;
        break;
    case BOOT_ELF_ARM64:
        p->wide = 1;
        p->relative = 1027;
        break;
    case BOOT_ELF_LOONGARCH64:
        p->wide = 1;
        p->relative = 3;
        break;
    default:
        return BOOT_E_UNSUPPORTED;
    }
    p->word = p->wide ? 8 : 4;
    unsigned ehsize = p->wide ? 64 : 52, phsize = p->wide ? 56 : 32;
    if (f[4] != 1 + p->wide || f[5] != 1 || f[6] != 1 || f[7] != 0 || f[8] != 0)
        return BOOT_E_UNSUPPORTED;
    if (p->length < ehsize)
        return BOOT_E_INVALID;
    if (read_le(f + 16, 2) != 3 || read_le(f + 18, 2) != (unsigned)machine)
        return BOOT_E_UNSUPPORTED;
    uint64_t flags = read_le(f + (p->wide ? 48 : 36), 4);
    /* LoongArch object ABI v1, double-float LP64D. */
    if (flags != (machine == BOOT_ELF_LOONGARCH64 ? 0x43u : 0u))
        return BOOT_E_UNSUPPORTED;
    if (read_le(f + 20, 4) != 1 || read_le(f + (p->wide ? 52 : 40), 2) != ehsize ||
        read_le(f + (p->wide ? 54 : 42), 2) != phsize)
        return BOOT_E_INVALID;
    p->entry = read_le(f + 24, p->word);
    uint64_t phoff = read_le(f + (p->wide ? 32 : 28), p->word);
    p->count = (unsigned)read_le(f + (p->wide ? 56 : 44), 2);
    if (!p->count || p->count > 32 || !range(phoff, p->count * phsize, p->length))
        return BOOT_E_INVALID;
    p->low = UINT64_MAX;
    p->alignment = 4096;
    unsigned dynamic = 0;
    for (unsigned i = 0; i < p->count; ++i)
    {
        const unsigned char *h = f + (size_t)phoff + i * phsize;
        struct segment *s = &p->segments[i];
        s->type = (uint32_t)read_le(h, 4);
        s->flags = (uint32_t)read_le(h + (p->wide ? 4 : 24), 4);
        s->offset = read_le(h + (p->wide ? 8 : 4), p->word);
        s->address = read_le(h + (p->wide ? 16 : 8), p->word);
        s->filesz = read_le(h + (p->wide ? 32 : 16), p->word);
        s->memsz = read_le(h + (p->wide ? 40 : 20), p->word);
        s->align = read_le(h + (p->wide ? 48 : 28), p->word);
        if (!range(s->offset, s->filesz, p->length) || s->filesz > s->memsz ||
            !range(s->address, s->memsz, p->wide ? UINT64_MAX : UINT32_MAX))
            return BOOT_E_INVALID;
        switch (s->type)
        {
        case 0:
        case 4:
        case 6:
        case 0x6474e552:
            break; /* NULL, NOTE, PHDR, RELRO */
        case 0x6474e551:
            if (s->flags & 1)
                return BOOT_E_UNSUPPORTED;
            break;
        case 2:
            ++dynamic;
            break;
        case 1:
            if ((s->flags & ~7u) || !(s->flags & 4) || (s->flags & 3) == 3 ||
                (s->align > 1 && (!power2(s->align) || s->align > LIMIT ||
                                  ((s->address ^ s->offset) & (s->align - 1)))))
                return BOOT_E_INVALID;
            if (!s->memsz)
                break;
            if (s->align > p->alignment)
                p->alignment = s->align;
            if (s->address < p->low)
                p->low = s->address;
            if (s->address + s->memsz > p->high)
                p->high = s->address + s->memsz;
            for (unsigned j = 0; j < i; ++j)
            {
                const struct segment *t = &p->segments[j];
                if (t->type == 1 && s->address < t->address + t->memsz &&
                    t->address < s->address + s->memsz)
                    return BOOT_E_INVALID;
            }
            break;
        default:
            return BOOT_E_UNSUPPORTED; /* includes INTERP and TLS */
        }
    }
    if (dynamic != 1 || p->low == UINT64_MAX)
        return BOOT_E_INVALID;
    p->low &= ~(p->alignment - 1);
    if (p->high - p->low > LIMIT || !mapped(p, p->entry, 1, 5) || !backed(p, p->entry, 1) ||
        ((machine == BOOT_ELF_ARM64 || machine == BOOT_ELF_LOONGARCH64) && (p->entry & 3)))
        return BOOT_E_INVALID;
    return BOOT_OK;
}
static boot_status_t dynamic(struct parser *p)
{
    const struct segment *d = NULL;
    for (unsigned i = 0; i < p->count; ++i)
        if (p->segments[i].type == 2)
            d = &p->segments[i];
    unsigned stride = 2 * p->word;
    const unsigned char *table = backed(p, d->address, d->filesz);
    if (!table || table != p->file + (size_t)d->offset || !d->filesz ||
        d->filesz > COUNT_LIMIT * stride || (unsigned)d->filesz % stride)
        return BOOT_E_INVALID;
    int end = 0;
    for (uint64_t i = 0; i < d->filesz; i += stride)
    {
        uint64_t tag = read_le(table + (size_t)i, p->word);
        uint64_t value = read_le(table + (size_t)i + p->word, p->word);
        if (!tag)
        {
            end = 1;
            break;
        }
        switch (tag)
        {
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:
        case 17:
        case 18:
        case 19:
        case 30:
            if (p->seen & (UINT64_C(1) << tag))
                return BOOT_E_INVALID;
            p->seen |= UINT64_C(1) << tag;
            p->tags[tag] = value;
            break;
        case 16:
            break; /* SYMBOLIC: all definitions bound locally */
        case 0x6ffffff9:
        case 0x6ffffffa:
            break; /* optional REL[A]COUNT hint */
        case 0x6ffffffb:
            if (value & ~UINT64_C(0x8000001))
                return BOOT_E_UNSUPPORTED;
            break; /* NOW/PIE */
        default:
            return BOOT_E_UNSUPPORTED;
        }
    }
    if (!end)
        return BOOT_E_INVALID;
    if (p->tags[30] & ~UINT64_C(2))
        return BOOT_E_UNSUPPORTED; /* only SYMBOLIC */
    uint64_t required = (UINT64_C(1) << 4) | (UINT64_C(1) << 5) | (UINT64_C(1) << 6) |
                        (UINT64_C(1) << 10) | (UINT64_C(1) << 11);
    if ((p->seen & required) != required)
        return BOOT_E_INVALID;
    const unsigned char *hash = backed(p, p->tags[4], 8);
    if (!hash)
        return BOOT_E_INVALID;
    uint64_t buckets = read_le(hash, 4), symbols = read_le(hash + 4, 4);
    if (!buckets || !symbols || buckets > COUNT_LIMIT || symbols > COUNT_LIMIT ||
        !backed(p, p->tags[4], 8 + 4 * (buckets + symbols)))
        return BOOT_E_INVALID;
    for (uint64_t i = 0; i < buckets + symbols; ++i)
        if (read_le(hash + 8 + (size_t)i * 4, 4) >= symbols)
            return BOOT_E_INVALID;
    unsigned symsize = p->wide ? 24 : 16;
    const unsigned char *syms = backed(p, p->tags[6], symbols * symsize);
    const unsigned char *strings = backed(p, p->tags[5], p->tags[10]);
    if (p->tags[11] != symsize || !syms || !strings || !p->tags[10] || strings[0])
        return BOOT_E_INVALID;
    for (unsigned i = 0; i < symsize; ++i)
        if (syms[i])
            return BOOT_E_INVALID;
    unsigned entries = 0;
    static const char entry_name[] = "boot_module_entry";
    for (uint64_t i = 1; i < symbols; ++i)
    {
        const unsigned char *s = syms + (size_t)i * symsize;
        uint64_t name = read_le(s, 4), value = read_le(s + (p->wide ? 8 : 4), p->word);
        unsigned info = s[p->wide ? 4 : 12], other = s[p->wide ? 5 : 13];
        unsigned section = (unsigned)read_le(s + (p->wide ? 6 : 14), 2);
        if (name >= p->tags[10])
            return BOOT_E_INVALID;
        uint64_t n = name;
        while (n < p->tags[10] && n - name < 256 && strings[n])
            ++n;
        if (n == p->tags[10] || n - name == 256)
            return BOOT_E_INVALID;
        if (!section || section >= 0xff00 || (info & 15) == 6 || (info & 15) == 10)
            return BOOT_E_UNSUPPORTED;
        if ((info >> 4) && (other & 3) != 2)
        {
            if (n - name != sizeof(entry_name) - 1 || info != 0x12 || other ||
                !mapped(p, value, 1, 5) || value != p->entry)
                return BOOT_E_UNSUPPORTED;
            for (unsigned j = 0; j < sizeof(entry_name) - 1; ++j)
                if (strings[name + j] != (unsigned char)entry_name[j])
                    return BOOT_E_UNSUPPORTED;
            ++entries;
        }
    }
    return entries == 1 ? BOOT_OK : BOOT_E_INVALID;
}
static boot_status_t relocate(const struct parser *p, unsigned char *memory)
{
    unsigned tag = p->wide ? 7 : 17, stride = p->wide ? 24 : 8;
    uint64_t mask = (UINT64_C(7) << tag), forbidden = UINT64_C(7) << (p->wide ? 17 : 7);
    if (p->seen & forbidden)
        return BOOT_E_UNSUPPORTED;
    if (!(p->seen & mask))
        return BOOT_OK;
    if ((p->seen & mask) != mask || p->tags[tag + 2] != stride ||
        p->tags[tag + 1] > COUNT_LIMIT * stride || (unsigned)p->tags[tag + 1] % stride)
        return BOOT_E_INVALID;
    const unsigned char *r = backed(p, p->tags[tag], p->tags[tag + 1]);
    if (!r)
        return BOOT_E_INVALID;
    uint64_t previous = 0;
    for (uint64_t i = 0; i < p->tags[tag + 1]; i += stride)
    {
        const unsigned char *record = r + (size_t)i;
        uint64_t address = read_le(record, p->word), info = read_le(record + p->word, p->word);
        if (info != p->relative)
            return BOOT_E_UNSUPPORTED;
        /* Sorted, disjoint relocation slots bound work and disallow double fixups. */
        if ((address & (p->word - 1)) || !mapped(p, address, p->word, 6) ||
            (i && address < previous + p->word))
            return BOOT_E_INVALID;
        previous = address;
        uint64_t addend;
        if (p->wide)
            addend = read_le(record + 16, 8);
        else
        {
            const unsigned char *slot = backed(p, address, 4);
            if (!slot)
                return BOOT_E_INVALID;
            addend = read_le(slot, 4);
        }
        if (!mapped(p, addend, 1, 4))
            return BOOT_E_INVALID;
        if (memory)
        {
            uint64_t value = (uintptr_t)memory + (addend - p->low);
            for (unsigned j = 0; j < p->word; ++j)
                memory[(size_t)(address - p->low) + j] = (unsigned char)(value >> (8 * j));
        }
    }
    return BOOT_OK;
}
static boot_status_t parse(struct parser *p, enum boot_elf_machine machine)
{
    boot_status_t status = headers(p, machine);
    if (!status)
        status = dynamic(p);
    if (!status)
        status = relocate(p, NULL);
    return status;
}
static void describe(const struct parser *p, struct boot_elf_image *image)
{
    image->size = (size_t)(p->high - p->low);
    image->alignment = (size_t)p->alignment;
    image->entry_offset = (size_t)(p->entry - p->low);
}
boot_status_t boot_elf_inspect(const void *file, size_t length, enum boot_elf_machine machine,
                               struct boot_elf_image *image)
{
    if (!file || !image)
        return BOOT_E_INVALID;
    struct parser p = {0};
    p.file = file;
    p.length = length;
    boot_status_t status = parse(&p, machine);
    if (!status)
        describe(&p, image);
    return status;
}
boot_status_t boot_elf_load(const void *file, size_t length, enum boot_elf_machine machine,
                            void *memory, size_t capacity, struct boot_elf_image *image)
{
    if (!file || !memory || !image)
        return BOOT_E_INVALID;
    struct parser p = {0};
    p.file = file;
    p.length = length;
    boot_status_t status = parse(&p, machine);
    if (status)
        return status;
    uintptr_t base = (uintptr_t)memory, source = (uintptr_t)file;
    uint64_t size = p.high - p.low;
    if (capacity < size)
        return BOOT_E_NOMEM;
    if ((base & (p.alignment - 1)) || size > UINTPTR_MAX - base || length > UINTPTR_MAX - source ||
        (base < source + length && source < base + size) ||
        (!p.wide && (base > UINT32_MAX || size > UINT32_MAX - base)))
        return BOOT_E_INVALID;
    unsigned char *out = memory;
    for (size_t i = 0; i < size; ++i)
        out[i] = 0;
    for (unsigned i = 0; i < p.count; ++i)
    {
        const struct segment *s = &p.segments[i];
        if (s->type == 1)
            for (size_t j = 0; j < s->filesz; ++j)
                out[(size_t)(s->address - p.low) + j] = p.file[(size_t)s->offset + j];
    }
    (void)relocate(&p, out); /* already validated, source is immutable and disjoint */
    describe(&p, image);
    return BOOT_OK;
}
