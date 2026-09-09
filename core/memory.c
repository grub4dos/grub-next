/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/memory.h>
static int valid(const struct boot_memory *m, uint64_t base, uint64_t size)
{
    return m && m->physical_bits >= 32 && m->physical_bits <= 63 && size &&
           base < (UINT64_C(1) << m->physical_bits) &&
           size <= (UINT64_C(1) << m->physical_bits) - base;
}
static int overlaps(uint64_t a, uint64_t n, uint64_t b, uint64_t s)
{
    return a < b + s && b < a + n;
}
boot_status_t boot_memory_add(struct boot_memory *m, uint64_t base, uint64_t size, uint32_t type)
{
    if (!valid(m, base, size) || m->frozen || type > BOOT_MEM_LOADER)
        return BOOT_E_INVALID;
    if (m->count == BOOT_REGIONS)
        return BOOT_E_NOMEM;
    size_t i = 0;
    while (i < m->count && m->regions[i].base < base)
        ++i;
    if ((i && overlaps(base, size, m->regions[i - 1].base, m->regions[i - 1].size)) ||
        (i < m->count && overlaps(base, size, m->regions[i].base, m->regions[i].size)))
        return BOOT_E_INVALID;
    for (size_t j = m->count; j > i; --j)
        m->regions[j] = m->regions[j - 1];
    m->regions[i] = (struct boot_region){base, size, type};
    ++m->count;
    return BOOT_OK;
}
boot_status_t boot_memory_reserve(struct boot_memory *m, uint64_t base, uint64_t size,
                                  enum boot_owner owner)
{
    if (!valid(m, base, size) || m->frozen || owner < BOOT_BL || owner > BOOT_RESIDENT ||
        (owner == BOOT_LOADER && !m->transaction))
        return BOOT_E_INVALID;
    if (m->used == BOOT_ALLOCATIONS)
        return BOOT_E_NOMEM;
    for (size_t i = 0; i < m->used; ++i)
        if (overlaps(base, size, m->allocations[i].base, m->allocations[i].size))
            return BOOT_E_INVALID;
    m->allocations[m->used++] =
        (struct boot_allocation){base, size, owner, owner == BOOT_LOADER ? m->transaction : 0};
    return BOOT_OK;
}
boot_status_t boot_memory_alloc(struct boot_memory *m, enum boot_owner owner, uint64_t size,
                                uint64_t alignment, uint64_t minimum, uint64_t maximum,
                                uint64_t *out)
{
    if (!m || m->frozen || m->physical_bits < 32 || m->physical_bits > 63 || !out || !size ||
        owner < BOOT_BL || owner > BOOT_RESIDENT || (owner == BOOT_LOADER && !m->transaction) ||
        !alignment || (alignment & (alignment - 1)) || minimum >= maximum ||
        maximum > (UINT64_C(1) << m->physical_bits))
        return BOOT_E_INVALID;
    if (m->refresh)
    {
        boot_status_t s = m->refresh(m);
        if (s)
            return s;
    }
    uint64_t granularity =
        owner == BOOT_RESIDENT && m->resident_page_size ? m->resident_page_size : m->page_size;
    if (granularity)
    {
        if (size > UINT64_MAX - (granularity - 1))
            return BOOT_E_INVALID;
        size = (size + granularity - 1) & ~(granularity - 1);
        if (alignment < granularity)
            alignment = granularity;
    }
    unsigned firmware_attempts = 0;
    /* Highest fitting address: bulk callers choose minimum=4GiB then retry low. */
    for (size_t i = m->count; i; --i)
    {
        const struct boot_region *r = &m->regions[i - 1];
        if (r->type != BOOT_MEM_FREE)
            continue;
        uint64_t low = r->base > minimum ? r->base : minimum;
        uint64_t end = r->base + r->size;
        if (end > maximum)
            end = maximum;
        while (end > low && size <= end - low)
        {
            uint64_t base = (end - size) & ~(alignment - 1);
            if (base < low)
                break;
            size_t j;
            for (j = 0; j < m->used; ++j)
                if (overlaps(base, size, m->allocations[j].base, m->allocations[j].size))
                {
                    end = m->allocations[j].base;
                    break;
                }
            if (j != m->used)
                continue;
            if (m->used == BOOT_ALLOCATIONS || owner < BOOT_BL || owner > BOOT_RESIDENT ||
                (owner == BOOT_LOADER && !m->transaction))
                return BOOT_E_INVALID;
            if (m->reserve_pages)
            {
                boot_status_t status = m->reserve_pages(base, size, owner);
                if (status)
                {
                    /* Firmware may reject otherwise conventional pages in a
                       hibernation/runtime allocation range. Try a lower range. */
                    if (status != BOOT_E_NOMEM || ++firmware_attempts == 256)
                        return status;
                    end = base;
                    continue;
                }
            }
            boot_status_t s = boot_memory_reserve(m, base, size, owner);
            if (s == BOOT_OK)
                *out = base;
            return s;
        }
    }
    return BOOT_E_NOMEM;
}
boot_status_t boot_memory_release(struct boot_memory *m, uint64_t base)
{
    if (!m || m->frozen)
        return BOOT_E_INVALID;
    for (size_t i = 0; i < m->used; ++i)
    {
        if (m->allocations[i].base != base)
            continue;
        if (m->allocations[i].owner != BOOT_BL)
            return BOOT_E_INVALID;
        if (m->free_pages && m->free_pages(base, m->allocations[i].size))
            return BOOT_E_IO;
        m->allocations[i] = m->allocations[--m->used];
        return BOOT_OK;
    }
    return BOOT_E_INVALID;
}
boot_status_t boot_loader_begin(struct boot_memory *m)
{
    if (!m || m->frozen || m->transaction || m->next_transaction == UINT32_MAX)
        return BOOT_E_INVALID;
    m->transaction = ++m->next_transaction;
    return BOOT_OK;
}
boot_status_t boot_loader_abort(struct boot_memory *m)
{
    if (!m || m->frozen || !m->transaction)
        return BOOT_E_INVALID;
    for (size_t i = 0; i < m->used;)
    {
        if (m->allocations[i].transaction == m->transaction)
        {
            if (m->free_pages && m->free_pages(m->allocations[i].base, m->allocations[i].size))
                return BOOT_E_IO;
            m->allocations[i] = m->allocations[--m->used];
        }
        else
            ++i;
    }
    m->transaction = 0;
    return BOOT_OK;
}
boot_status_t boot_loader_commit(struct boot_memory *m)
{
    if (!m || m->frozen || !m->transaction)
        return BOOT_E_INVALID;
    for (size_t i = 0; i < m->used; ++i)
        if (m->allocations[i].transaction == m->transaction)
            m->allocations[i].transaction = 0;
    m->transaction = 0;
    return BOOT_OK;
}
boot_status_t boot_memory_export(const struct boot_memory *m, struct boot_region *out,
                                 size_t *count)
{
    if (!m || !count || (!out && *count))
        return BOOT_E_INVALID;
    size_t needed = 0, capacity = *count;
    for (size_t i = 0; i < m->count; ++i)
    {
        const struct boot_region *r = &m->regions[i];
        uint64_t p = r->base, end = p + r->size;
        while (p < end)
        {
            uint64_t next = end;
            uint32_t type = r->type;
            for (size_t j = 0; j < m->used; ++j)
            {
                const struct boot_allocation *a = &m->allocations[j];
                if (a->base <= p && p < a->base + a->size)
                {
                    if (a->base + a->size < next)
                        next = a->base + a->size;
                    type = BOOT_MEM_RESERVED;
                }
                else if (a->base > p && a->base < next)
                    next = a->base;
            }
            if (needed < capacity)
                out[needed] = (struct boot_region){p, next - p, type};
            ++needed;
            p = next;
        }
    }
    *count = needed;
    return needed <= capacity ? BOOT_OK : BOOT_E_NOMEM;
}
boot_status_t boot_memory_access(const struct boot_memory *m, uint64_t base, uint64_t size)
{
    if (!valid(m, base, size))
        return BOOT_E_INVALID;
    uint64_t end = base + size, cursor = base;
    for (size_t i = 0; i < m->count && cursor < end; ++i)
    {
        const struct boot_region *r = &m->regions[i];
        if (r->base <= cursor && cursor < r->base + r->size)
        {
            if (r->type != BOOT_MEM_FREE)
                return BOOT_E_INVALID;
            cursor = r->base + r->size;
        }
    }
    if (cursor < end)
        return BOOT_E_INVALID;
    /* Only explicitly owned RAM may be accessed through the physical API. */
    for (size_t i = 0; i < m->used; ++i)
    {
        const struct boot_allocation *a = &m->allocations[i];
        if (base >= a->base && base - a->base < a->size && size <= a->size - (base - a->base))
            return BOOT_OK;
    }
    return BOOT_E_INVALID;
}
