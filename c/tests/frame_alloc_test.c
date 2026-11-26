#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "frame_alloc_search.h"

#define PAGES 64
static uint32_t bm[2];

static void all_used(void) { bm[0] = bm[1] = 0xFFFFFFFFu; }
static void make_free(uint32_t first, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        uint32_t p = first + i;
        bm[p / 32] &= ~(1u << (p % 32));
    }
}
static int free_count(void) {
    int n = 0;
    for (uint32_t i = 0; i < PAGES; i++)
        if (!(bm[i / 32] & (1u << (i % 32)))) n++;
    return n;
}
static int take(uint32_t start, uint32_t pages) {
    for (uint32_t i = 0; i < pages; i++)
        bm[(start + i) / 32] |= 1u << ((start + i) % 32);
    return 0;
}

int main(void) {
    /* 64 single-page requests from a clean 64-page pool. */
    all_used();
    make_free(0, PAGES);
    if (free_count() != PAGES) return 1;
    uint32_t got[PAGES];
    for (uint32_t i = 0; i < PAGES; i++) {
        uint32_t p = frame_find_run(bm, PAGES, 1);
        if (p == 0xFFFFFFFFu) {
            printf("FAIL single %u\n", i);
            return 1;
        }
        for (uint32_t j = 0; j < i; j++)
            if (got[j] == p) return 1;
        got[i] = p;
        take(p, 1);
    }
    if (free_count() != 0) return 1;
    if (frame_find_run(bm, PAGES, 1) != 0xFFFFFFFFu) return 1;
    make_free(0, PAGES);

    /* Cross-word and multi-page runs. */
    const uint32_t sizes[] = {1, 2, 31, 32, 33};
    for (unsigned k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++) {
        all_used();
        make_free(0, PAGES);
        uint32_t p = frame_find_run(bm, PAGES, sizes[k]);
        if (p != 0 || sizes[k] > PAGES) return 1;
        take(p, sizes[k]);
    }

    /* Interleaved occupancy: a single page works, a contiguous pair does not. */
    all_used();
    for (uint32_t i = 0; i < PAGES; i += 2)
        bm[i / 32] &= ~(1u << (i % 32));
    if (frame_find_run(bm, PAGES, 1) == 0xFFFFFFFFu) return 1;
    if (frame_find_run(bm, PAGES, 2) != 0xFFFFFFFFu) return 1;

    /* Invalid requests. */
    all_used();
    if (frame_find_run(bm, PAGES, 0) != 0xFFFFFFFFu) return 1;
    if (frame_find_run(bm, PAGES, PAGES + 1) != 0xFFFFFFFFu) return 1;

    printf("FRAME_ALLOC_TEST PASS 64 singles, cross-word, multi-page, interleaved\n");
    return 0;
}
