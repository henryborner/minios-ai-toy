#ifndef MINIOS_MEMMAP_NORMALIZE_H
#define MINIOS_MEMMAP_NORMALIZE_H

#include <stdint.h>

typedef struct {
    uint32_t base_lo, base_hi;
    uint32_t len_lo, len_hi;
    uint32_t type;
} memmap_entry;

static inline void mm_set_range(uint32_t *bm, uint32_t total,
                                uint32_t first, uint32_t end, int used) {
    if (first >= total) return;
    if (end > total) end = total;
    for (uint32_t p = first; p < end; p++) {
        if (used) bm[p / 32] |= (1u << (p % 32));
        else bm[p / 32] &= ~(1u << (p % 32));
    }
}

/* Build a page bitmap where 1=unavailable.  Available ranges are copied in,
   then all non-type-1 ranges are marked reserved.  Overlaps count once. */
static inline uint32_t memmap_build(uint32_t *bm, uint32_t total_pages,
                                    uint32_t pool_start_page,
                                    const memmap_entry *e, int n) {
    for (uint32_t i = 0; i < (total_pages + 31) / 32; i++)
        bm[i] = 0xFFFFFFFFu;

    for (int k = 0; k < n; k++) {
        if (e[k].type != 1 || e[k].base_hi != 0)
            continue;
        uint32_t base = e[k].base_lo;
        if (base >= total_pages * 4096u)
            continue;
        uint32_t end;
        if (e[k].len_hi != 0 || base + e[k].len_lo < base ||
            base + e[k].len_lo > total_pages * 4096u)
            end = total_pages * 4096u;
        else
            end = base + e[k].len_lo;
        uint32_t pbase = base;
        if (pbase < pool_start_page * 4096u)
            pbase = pool_start_page * 4096u;
        uint32_t first = (pbase + 0xFFFu) / 4096u;
        uint32_t last = end / 4096u;
        if (first < last)
            mm_set_range(bm, total_pages, first, last, 0);
    }

    for (int k = 0; k < n; k++) {
        if (e[k].type == 1 || e[k].base_hi != 0)
            continue;
        uint32_t base = e[k].base_lo;
        if (base >= total_pages * 4096u)
            continue;
        uint32_t end;
        if (e[k].len_hi != 0 || base + e[k].len_lo < base ||
            base + e[k].len_lo > total_pages * 4096u)
            end = total_pages * 4096u;
        else
            end = base + e[k].len_lo;
        uint32_t first = base / 4096u;
        uint32_t last = (end + 0xFFFu) / 4096u;
        mm_set_range(bm, total_pages, first, last, 1);
    }

    uint32_t count = 0;
    for (uint32_t p = 0; p < total_pages; p++)
        if (!(bm[p / 32] & (1u << (p % 32))))
            count++;
    return count;
}

#endif
