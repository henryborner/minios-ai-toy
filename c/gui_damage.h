#ifndef MINIOS_GUI_DAMAGE_H
#define MINIOS_GUI_DAMAGE_H

/* Half-open screen or surface coordinates. All stored rectangles are clipped. */
struct rect { int x0, y0, x1, y1; };
#define DAMAGE_LIMIT 16
struct damage {
    int count;
    struct rect rects[DAMAGE_LIMIT];
};

static int min_i(int a, int b) { return a < b ? a : b; }
static int max_i(int a, int b) { return a > b ? a : b; }

static struct rect rect_clip(struct rect r, int w, int h) {
    return (struct rect){max_i(r.x0, 0), max_i(r.y0, 0),
                         min_i(r.x1, w), min_i(r.y1, h)};
}

static struct rect rect_union(struct rect a, struct rect b) {
    return (struct rect){min_i(a.x0, b.x0), min_i(a.y0, b.y0),
                         max_i(a.x1, b.x1), max_i(a.y1, b.y1)};
}

static int rect_area(struct rect r) {
    return (r.x1 - r.x0) * (r.y1 - r.y0);
}

static int rect_equal(struct rect a, struct rect b) {
    return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
}

static void damage_add(struct damage *d, struct rect r, int w, int h) {
    r = rect_clip(r, w, h);
    if (r.x0 >= r.x1 || r.y0 >= r.y1)
        return;
    for (int i = 0; i < d->count;) {
        struct rect joined = rect_union(r, d->rects[i]);
        if (rect_area(joined) <= rect_area(r) + rect_area(d->rects[i])) {
            r = joined;
            d->rects[i] = d->rects[--d->count];
            i = 0;
        } else {
            i++;
        }
    }
    if (d->count == DAMAGE_LIMIT) {
        /* Keep every damaged pixel even under a fragmented burst. */
        for (int i = 0; i < d->count; i++)
            r = rect_union(r, d->rects[i]);
        d->count = 0;
    }
    d->rects[d->count++] = r;
}
#endif
