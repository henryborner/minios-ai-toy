#include <stdio.h>
#include <stdint.h>
#include "memmap_normalize.h"

#define PAGES 64
static uint32_t bm[2];

static uint32_t run(const memmap_entry *e, int n) {
    return memmap_build(bm, PAGES, 0, e, n);
}

int main(void) {
    memmap_entry e[8] = {0};

    /* Two overlapping available ranges -> exactly 64 unique pages. */
    e[0] = (memmap_entry){0, 0, 48 * 4096, 0, 1};
    e[1] = (memmap_entry){32 * 4096, 0, 32 * 4096, 0, 1};
    if (run(e, 2) != 64) { printf("FAIL overlap %u\n", run(e, 2)); return 1; }

    /* Reserved range over a 64-page available range -> 62 pages. */
    e[0] = (memmap_entry){0, 0, 64 * 4096, 0, 1};
    e[1] = (memmap_entry){0, 0, 0, 0, 0};
    e[2] = (memmap_entry){8 * 4096, 0, 2 * 4096, 0, 2};
    if (run(e, 3) != 62) { printf("FAIL reserved %u\n", run(e, 3)); return 1; }

    /* A one-byte reserved hole must invalidate the whole touched page. */
    e[2] = (memmap_entry){8 * 4096 + 100, 0, 1, 0, 2};
    if (run(e, 3) != 63) { printf("FAIL byte-hole %u\n", run(e, 3)); return 1; }

    /* High base (above 32-bit managed window) is ignored. */
    e[0] = (memmap_entry){0, 1, 64 * 4096, 0, 1};
    if (run(e, 1) != 0) return 1;

    /* High length clamps to the managed end. */
    e[0] = (memmap_entry){0, 0, 0, 1, 1};
    if (run(e, 1) != 64) return 1;

    /* Empty table -> zero managed pages. */
    if (run(e, 0) != 0) return 1;

    printf("MEMMAP_TEST PASS overlap, reserved, byte-hole, high-base, high-len, empty\n");
    return 0;
}
