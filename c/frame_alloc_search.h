#ifndef MINIOS_FRAME_ALLOC_SEARCH_H
#define MINIOS_FRAME_ALLOC_SEARCH_H

#include <stdint.h>

/* Return the first page index with 'pages' consecutive clear bits.
   Returns 0xFFFFFFFF when no such run exists. */
static inline uint32_t frame_find_run_from(const uint32_t *bitmap,
                                           uint32_t total, uint32_t pages,
                                           uint32_t start) {
    if (pages == 0 || pages > total || start >= total)
        return 0xFFFFFFFFu;
    while (start + pages <= total) {
        uint32_t run = 0;
        while (run < pages) {
            uint32_t bit = start + run;
            if (bitmap[bit / 32] & (1u << (bit % 32)))
                break;
            run++;
        }
        if (run == pages)
            return start;
        start += run + 1;
    }
    return 0xFFFFFFFFu;
}

static inline uint32_t frame_find_run(const uint32_t *bitmap, uint32_t total,
                                      uint32_t pages) {
    return frame_find_run_from(bitmap, total, pages, 0);
}

#endif
