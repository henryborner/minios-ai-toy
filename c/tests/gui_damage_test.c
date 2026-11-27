/*
 * In-kernel GUI damage test.
 *
 * This is a test-only translation unit.  It includes the production GUI with
 * gui_redraw renamed, then exports a gui_redraw() wrapper.  The first call,
 * made by kmain after the boot UI has been initialised, runs the scenarios and
 * reports on COM1.  The host runner stops its own temporary QEMU afterwards.
 *
 * Public GUI entry points are used to drive behaviour.  The only internals
 * touched directly are the explicitly provided pending_damage queue and the
 * window rectangles needed to aim mouse events (there is no public getter for
 * window geometry).
 */
#include "../gui.h"
#include "../gui_damage.h"

#define gui_redraw gui_redraw_production
void gui_redraw_production(void);
#include "../gui.c"
#undef gui_redraw

extern void serial_puts(const char *s);
extern void serial_u32(u32 v);
extern void *kernel_alloc_pages_zero(u32 pages);
extern void kernel_free_pages_ptr(void *p, u32 pages);

static int tdt_checks;
static int tdt_failed;
static u8 *tdt_snap;
static u32 tdt_snap_pages;
static u32 tdt_irq_flags;
static int tdt_ran;

/* ------------------------------------------------------------------------- */
/* Reporting                                                                 */
/* ------------------------------------------------------------------------- */

static const char tdt_hexdigits[] = "0123456789ABCDEF";

static void tdt_hex8(u8 v) {
    char b[5];
    b[0] = '0';
    b[1] = 'x';
    b[2] = tdt_hexdigits[v >> 4];
    b[3] = tdt_hexdigits[v & 15];
    b[4] = 0;
    serial_puts(b);
}

static void tdt_begin_fail(const char *where) {
    if (tdt_failed)
        return;
    tdt_failed = 1;
    serial_puts("GUI_DAMAGE_TEST FAIL ");
    serial_puts(where);
}

static void tdt_fail_msg(const char *where, const char *why) {
    tdt_begin_fail(where);
    if (tdt_failed && why) {
        serial_puts(" ");
        serial_puts(why);
    }
    serial_puts("\r\n");
}

static void tdt_fail_pixel(const char *where, int x, int y, u8 exp, u8 act) {
    tdt_begin_fail(where);
    if (tdt_failed) {
        serial_puts(" x=");
        serial_u32((u32)x);
        serial_puts(" y=");
        serial_u32((u32)y);
        serial_puts(" expected=");
        tdt_hex8(exp);
        serial_puts(" actual=");
        tdt_hex8(act);
    }
    serial_puts("\r\n");
}

/* ------------------------------------------------------------------------- */
/* Physical framebuffer snapshot                                             */
/* ------------------------------------------------------------------------- */

static int tdt_screen_setup(void) {
    u32 bytes = (u32)scr_w * (u32)scr_h;
    if (vbe_active && !vbe_lfb) {
        tdt_fail_msg("screen-setup", "VBE active without LFB");
        return 0;
    }
    if (vbe_active && vbe_pitch < (u32)scr_w) {
        tdt_fail_msg("screen-setup", "VBE pitch shorter than width");
        return 0;
    }
    tdt_snap_pages = (bytes + 4095u) / 4096u;
    tdt_snap = (u8 *)kernel_alloc_pages_zero(tdt_snap_pages);
    if (!tdt_snap) {
        tdt_fail_msg("screen-setup", "snapshot allocation failed");
        return 0;
    }
    return 1;
}

static u8 tdt_screen_read(int x, int y) {
    if (vbe_active && vbe_lfb)
        return vbe_lfb[(u32)y * vbe_pitch + (u32)x];
    return ((volatile u8 *)0x000A0000)[(u32)y * (u32)scr_w + (u32)x];
}

static void tdt_screen_capture(void) {
    for (int y = 0; y < scr_h; y++) {
        u8 *d = tdt_snap + (u32)y * (u32)scr_w;
        if (vbe_active && vbe_lfb) {
            const volatile u8 *s = vbe_lfb + (u32)y * vbe_pitch;
            for (int x = 0; x < scr_w; x++)
                d[x] = s[x];
        } else {
            const volatile u8 *s = (const volatile u8 *)0x000A0000 +
                                   (u32)y * (u32)scr_w;
            for (int x = 0; x < scr_w; x++)
                d[x] = s[x];
        }
    }
}

/*
 * Apply the real (incremental) redraw, remember what reached the physical
 * framebuffer, force a full-screen redraw of the same state, and compare.
 */
static int tdt_step(const char *where) {
    if (tdt_failed)
        return 0;

    gui_redraw_production();
    tdt_screen_capture();

    gui_mark_dirty();
    gui_redraw_production();

    if (pending_damage.count != 0) {
        tdt_fail_msg(where, "damage queue not drained by full redraw");
        return 0;
    }
    for (int y = 0; y < scr_h; y++) {
        const u8 *e = tdt_snap + (u32)y * (u32)scr_w;
        for (int x = 0; x < scr_w; x++) {
            u8 a = tdt_screen_read(x, y);
            if (a != e[x]) {
                tdt_fail_pixel(where, x, y, e[x], a);
                return 0;
            }
        }
    }
    tdt_checks++;
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Event helpers                                                             */
/* ------------------------------------------------------------------------- */

static void tdt_mouse(int x, int y, int buttons) {
    gui_mouse_event(x, y, buttons);
}

static void tdt_click(int x, int y) {
    tdt_mouse(x, y, 0);
    tdt_mouse(x, y, 1);
    tdt_mouse(x, y, 0);
}

static void tdt_right_click(int x, int y) {
    tdt_mouse(x, y, 2);
    tdt_mouse(x, y, 0);
}

static void tdt_drag(int sx, int sy, int ex, int ey) {
    tdt_mouse(sx, sy, 0);
    tdt_mouse(sx, sy, 1);
    tdt_mouse((sx + ex) / 2, (sy + ey) / 2, 1);
    tdt_mouse(ex, ey, 1);
    tdt_mouse(ex, ey, 0);
}

static int tdt_taskbar_y(void) {
    return scr_h - 14;
}

static int tdt_menu_item_cy(int index) {
    /* Mirrors the ten-item Start menu: MENU_H = 10*12+4, four px above bar. */
    int menu_h = 10 * 12 + 4;
    return tdt_taskbar_y() - menu_h - 4 + 2 + index * 12 + 6;
}

static void tdt_context_pos(int x, int y, int items, int *cx, int *cy) {
    /* Mirrors open_context's placement/clamping. */
    int px = x + 2;
    int py = y + 2;
    int height = items * 12 + 4;
    if (px + 128 > scr_w)
        px = scr_w - 128;
    if (py + height > tdt_taskbar_y() - 2)
        py = tdt_taskbar_y() - 2 - height;
    if (px < 0)
        px = 0;
    if (py < 0)
        py = 0;
    *cx = px;
    *cy = py;
}

static int tdt_streq(const char *a, const char *b) {
    if (!a || !b)
        return 0;
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int tdt_window_with_title(const char *title) {
    for (int i = 0; i < win_live; i++)
        if (wins[i].visible && tdt_streq(wins[i].title, title))
            return i;
    return -1;
}

static int tdt_damage_has(int x, int y) {
    for (int i = 0; i < pending_damage.count; i++) {
        struct rect r = pending_damage.rects[i];
        if (x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1)
            return 1;
    }
    return 0;
}

static int tdt_rect_full(struct rect r) {
    return r.x0 <= 0 && r.y0 <= 0 && r.x1 >= scr_w && r.y1 >= scr_h;
}

/* ------------------------------------------------------------------------- */
/* Scenarios                                                                 */
/* ------------------------------------------------------------------------- */

#define TDT_REQUIRE(cond, where, why)                                         \
    do {                                                                      \
        if (!(cond)) {                                                        \
            tdt_fail_msg((where), (why));                                     \
            return;                                                           \
        }                                                                     \
    } while (0)

#define TDT_STEP(name)                                                        \
    do {                                                                      \
        if (!tdt_step(name))                                                  \
            return;                                                           \
    } while (0)

static void tdt_canvas_cmd_simple(u32 op, u32 x, u32 y, u32 w, u32 h,
                                  u32 color) {
    struct gui_canvas_cmd cmd;
    for (u32 i = 0; i < sizeof(cmd); i++)
        ((u8 *)&cmd)[i] = 0;
    cmd.op = op;
    cmd.x = x;
    cmd.y = y;
    cmd.w = w;
    cmd.h = h;
    cmd.color = color;
    (void)gui_canvas_cmd(&cmd);
}

static void tdt_run(void) {
    /* Freeze tick-derived text so incremental and reference agree. */
    ticks = 100000;

    if (!tdt_screen_setup())
        return;

    /* The first production redraw called by kmain is the real incremental
     * path for the boot state. */
    gui_redraw_production();
    TDT_STEP("initial-incremental");

    /* Empty queue: drain and a no-op redraw must stay harmless. */
    pending_damage.count = 0;
    gui_redraw_production();
    TDT_REQUIRE(pending_damage.count == 0, "empty-damage",
                "queue grew on empty redraw");
    TDT_STEP("empty-damage");

    /* Two far-apart, small dirty regions must stay small. */
    pending_damage.count = 0;
    damage_add(&pending_damage, (struct rect){5, 5, 15, 15}, scr_w, scr_h);
    damage_add(&pending_damage,
               (struct rect){scr_w - 15, scr_h - 15, scr_w - 5, scr_h - 5},
               scr_w, scr_h);
    TDT_REQUIRE(pending_damage.count == 2, "damage-far",
                "disjoint rects were merged");
    TDT_REQUIRE(!tdt_rect_full(pending_damage.rects[0]), "damage-far",
                "first far rect became full screen");
    TDT_REQUIRE(!tdt_rect_full(pending_damage.rects[1]), "damage-far",
                "second far rect became full screen");
    TDT_STEP("damage-far");

    /* Fragmented burst beyond capacity must remain bounded and complete. */
    {
        int px[DAMAGE_LIMIT + 8];
        int py[DAMAGE_LIMIT + 8];
        int n = DAMAGE_LIMIT + 8;
        pending_damage.count = 0;
        for (int i = 0; i < n; i++) {
            px[i] = 3 + (i * 37) % (scr_w - 8);
            py[i] = 3 + (i * 23) % (scr_h - 8);
            damage_add(&pending_damage,
                       (struct rect){px[i], py[i], px[i] + 2, py[i] + 2},
                       scr_w, scr_h);
        }
        TDT_REQUIRE(pending_damage.count > 0, "damage-overflow",
                    "overflow discarded all damage");
        TDT_REQUIRE(pending_damage.count <= DAMAGE_LIMIT, "damage-overflow",
                    "count exceeded DAMAGE_LIMIT");
        for (int i = 0; i < n; i++)
            TDT_REQUIRE(tdt_damage_has(px[i], py[i]), "damage-overflow",
                        "fragmented pixel was lost");
    }
    TDT_STEP("damage-overflow");

    /* Pointer motion, hover, and edges. */
    tdt_mouse(10, tdt_taskbar_y() + 8, 0);
    TDT_STEP("hover-start");
    tdt_mouse(scr_w / 2, scr_h / 2 - 20, 0);
    TDT_STEP("pointer-center");
    tdt_mouse(-5, scr_h + 5, 0);
    TDT_STEP("pointer-edge-outside");
    tdt_mouse(scr_w - 1, tdt_taskbar_y() - 1, 0);
    TDT_STEP("pointer-bottom-right");

    /* Start menu, menu hover, and focusing the second console. */
    tdt_click(10, tdt_taskbar_y() + 8);
    TDT_STEP("menu-open");
    tdt_mouse(20, tdt_menu_item_cy(1), 0);
    TDT_STEP("menu-hover");
    tdt_click(20, tdt_menu_item_cy(1));
    TDT_STEP("menu-focus-console2");

    TDT_REQUIRE(wins[1].visible, "menu-focus-console2",
                "console 2 did not become visible");

    /* Window list: taskbar toggle, tab selection, enter activation. */
    tdt_click(110, tdt_taskbar_y() + 8);
    TDT_STEP("list-open");
    gui_key('\t');
    TDT_STEP("list-tab");
    gui_key('\n');
    TDT_STEP("list-enter");

    /* Right-click context menu on console 2, then minimize/restore. */
    {
        int x = wins[1].x;
        int y = wins[1].y;
        int cx, cy;
        tdt_right_click(x + wins[1].w / 2, y + wins[1].h / 2);
        TDT_STEP("context-open");
        tdt_context_pos(x + wins[1].w / 2, y + wins[1].h / 2, 7, &cx, &cy);
        tdt_click(cx + 4, cy + 2 + 3 * 12 + 6);
        TDT_STEP("context-minimize");
        gui_special(5);
        TDT_STEP("context-minimize-restore");
    }

    /* Drag, resize, overlap/shadow, maximize/restore, minimize/restore,
     * close/restore. */
    {
        int x = wins[1].x;
        int y = wins[1].y;
        int w = wins[1].w;
        int h = wins[1].h;
        int nw = (w > 360) ? 300 : (w > 250 ? w - 40 : w - 20);
        int nh = (h > 200) ? 150 : h - 30;
        if (nw < 210)
            nw = 210;
        if (nh < 100)
            nh = 100;
        tdt_drag(x + w - 1, y + h - 1, x + nw - 1, y + nh - 1);
        TDT_STEP("geometry-resize");
    }
    {
        int x = wins[1].x;
        int y = wins[1].y;
        int tx = (scr_w >= 640) ? 200 : 80;
        int ty = (scr_w >= 640) ? 90 : 60;
        if (tx + wins[1].w > scr_w - 3)
            tx = scr_w - 3 - wins[1].w;
        if (tx < 3)
            tx = 3;
        tdt_drag(x + 40, y + 8, tx + 40, ty + 8);
        TDT_STEP("geometry-overlap-shadow");
    }
    {
        int x = wins[1].x;
        int y = wins[1].y;
        int w = wins[1].w;
        tdt_click(x + w - 28 + 6, y + 8);
        TDT_STEP("maximize");
        x = wins[1].x;
        y = wins[1].y;
        w = wins[1].w;
        tdt_click(x + w - 28 + 6, y + 8);
        TDT_STEP("maximize-restore");
    }
    {
        int x = wins[1].x;
        int y = wins[1].y;
        int w = wins[1].w;
        tdt_click(x + w - 42 + 6, y + 8);
        TDT_STEP("minimize");
        gui_special(5);
        TDT_STEP("minimize-restore");
    }
    {
        int x = wins[1].x;
        int y = wins[1].y;
        int w = wins[1].w;
        tdt_click(x + w - 14 + 6, y + 8);
        TDT_STEP("close");
        gui_special(5);
        TDT_STEP("close-restore");
    }

    /* Desktop right-click themes (context menu without a console target). */
    {
        int x = scr_w - 20;
        int y = 10;
        int cx, cy;
        tdt_right_click(x, y);
        TDT_STEP("context-desktop-open");
        tdt_context_pos(x, y, 11, &cx, &cy);
        tdt_click(cx + 4, cy + 2 + 9 * 12 + 6);
        TDT_STEP("context-theme-midnight");
        tdt_right_click(x, y);
        TDT_STEP("context-desktop-open-2");
        tdt_context_pos(x, y, 11, &cx, &cy);
        tdt_click(cx + 4, cy + 2 + 8 * 12 + 6);
        TDT_STEP("context-theme-classic");
    }

    /* Explicit terminal themes. */
    gui_theme(1);
    TDT_STEP("theme-dark");
    gui_theme(0);
    TDT_STEP("theme-light");

    /* Keyboard editing, ANSI erase-to-end-of-line/screen, newline, scroll. */
    gui_special(3);
    gui_key('a');
    gui_key('b');
    gui_key('\b');
    gui_key('\b');
    TDT_STEP("keyboard-edit");
    gui_sys_print_console(0, "ansi-k\x1b[K");
    TDT_STEP("ansi-K");
    gui_sys_print_console(0, "ansi-j\x1b[J");
    TDT_STEP("ansi-J");
    gui_key('\n');
    TDT_STEP("keyboard-newline");
    gui_mouse_wheel(3);
    TDT_STEP("scroll-up");
    gui_mouse_wheel(-3);
    TDT_STEP("scroll-down");

    /* Canvas: clear, point, rect, line, text, frame, circle, tile, batch,
     * repeated present, clipping and events. */
    gui_canvas_open("damage-canvas");
    TDT_REQUIRE(gui_canvas_w() > 0 && gui_canvas_h() > 0, "canvas-open",
                "canvas has no size");

    tdt_canvas_cmd_simple(0, 0, 0, 0, 0, 7);
    TDT_STEP("canvas-clear");
    tdt_canvas_cmd_simple(1, 5, 5, 0, 0, 15);
    tdt_canvas_cmd_simple(2, 10, 10, 40, 20, 4);
    tdt_canvas_cmd_simple(3, 2, 30, 70, 2, 11);
    {
        struct gui_canvas_cmd cmd;
        for (u32 i = 0; i < sizeof(cmd); i++)
            ((u8 *)&cmd)[i] = 0;
        cmd.op = 4;
        cmd.x = 8;
        cmd.y = 8;
        cmd.color = 14;
        cmd.text = "HELLO";
        cmd.len = 5;
        TDT_REQUIRE(gui_canvas_cmd(&cmd) == 0, "canvas-text",
                    "text command rejected");
    }
    tdt_canvas_cmd_simple(5, 15, 40, 50, 25, 12);
    tdt_canvas_cmd_simple(6, 60, 50, 12, 3, 1);
    {
        u8 tile[4 * 3] = {
            1, 2, 3, 4,
            5, 6, 7, 8,
            9, 10, 11, 12
        };
        struct gui_canvas_cmd cmd;
        for (u32 i = 0; i < sizeof(cmd); i++)
            ((u8 *)&cmd)[i] = 0;
        cmd.op = 7;
        cmd.x = 20;
        cmd.y = 20;
        cmd.w = 4;
        cmd.h = 3;
        cmd.text = (const char *)tile;
        cmd.len = sizeof(tile);
        TDT_REQUIRE(gui_canvas_cmd(&cmd) == 0, "canvas-tile",
                    "tile command rejected");
    }
    {
        /* Two op-9 circles: x,y,r,color,pad,fill per 20-byte record. */
        u32 circles[10];
        struct gui_canvas_cmd cmd;
        for (u32 i = 0; i < sizeof(cmd); i++)
            ((u8 *)&cmd)[i] = 0;
        circles[0] = 30; circles[1] = 30; circles[2] = 8;
        circles[3] = 6;  circles[4] = 0;
        circles[5] = 70; circles[6] = 30; circles[7] = 12;
        circles[8] = 9;  circles[9] = 1;
        cmd.op = 9;
        cmd.text = (const char *)circles;
        cmd.len = sizeof(circles);
        TDT_REQUIRE(gui_canvas_cmd(&cmd) == 0, "canvas-circles",
                    "multi-circle command rejected");
    }
    TDT_STEP("canvas-primitives");

    /* Out-of-bounds drawing must clip, not fault or leak. */
    tdt_canvas_cmd_simple(1, (u32)-10, (u32)-10, 0, 0, 15);
    tdt_canvas_cmd_simple(1, (u32)gui_canvas_w() + 10,
                          (u32)gui_canvas_h() + 10, 0, 0, 15);
    tdt_canvas_cmd_simple(2, (u32)-30, (u32)-30, 20, 20, 15);
    tdt_canvas_cmd_simple(3, (u32)-20, 0,
                          (u32)gui_canvas_w() + 20,
                          (u32)gui_canvas_h(), 15);
    TDT_STEP("canvas-clipping");

    /* Batch mode and repeated present. */
    gui_canvas_batch(1);
    tdt_canvas_cmd_simple(2, 5, 5, 30, 10, 13);
    tdt_canvas_cmd_simple(3, 3, 3, 60, 40, 14);
    gui_canvas_present();
    TDT_STEP("canvas-batch-present");
    gui_canvas_present();
    TDT_STEP("canvas-repeat-present");
    gui_canvas_batch(0);
    tdt_canvas_cmd_simple(1, 4, 4, 0, 0, 10);
    TDT_STEP("canvas-after-batch");

    /* Canvas key and mouse event delivery. */
    {
        int cid = tdt_window_with_title("damage-canvas");
        TDT_REQUIRE(cid >= 0, "canvas-window", "canvas window not found");
        gui_canvas_capture(1);
        gui_key('Q');
        {
            struct gui_canvas_event ev;
            TDT_REQUIRE(gui_canvas_event(&ev) == 1 &&
                        ev.type == GUI_EV_KEY && ev.key == 'Q',
                        "canvas-key-event", "key event missing");
        }
        tdt_mouse(wins[cid].x + 10, wins[cid].y + 20, 1);
        tdt_mouse(wins[cid].x + 10, wins[cid].y + 20, 0);
        {
            struct gui_canvas_event ev;
            TDT_REQUIRE(gui_canvas_event(&ev) == 1 &&
                        ev.type == GUI_EV_MOUSE_DOWN,
                        "canvas-mouse-event", "mouse down missing");
            TDT_REQUIRE(gui_canvas_event(&ev) == 1 &&
                        ev.type == GUI_EV_MOUSE_UP,
                        "canvas-mouse-event", "mouse up missing");
        }
        gui_canvas_capture(0);
    }
    gui_canvas_close();
    TDT_STEP("canvas-close");

    gui_special(3);
    TDT_STEP("final-restore-console1");
}

/* ------------------------------------------------------------------------- */
/* Entry point                                                               */
/* ------------------------------------------------------------------------- */

void gui_redraw(void) {
    if (tdt_ran) {
        gui_redraw_production();
        return;
    }
    tdt_ran = 1;

    __asm__ __volatile__("pushf; pop %0; cli" : "=r"(tdt_irq_flags) : : "memory");

    tdt_run();

    if (!tdt_failed) {
        serial_puts("GUI_DAMAGE_TEST PASS mode=");
        serial_puts((vbe_active && vbe_lfb) ? "VBE" : "VGA");
        serial_puts(" resolution=");
        serial_u32((u32)scr_w);
        serial_puts("x");
        serial_u32((u32)scr_h);
        if (vbe_active && vbe_lfb) {
            serial_puts(" pitch=");
            serial_u32(vbe_pitch);
        }
        serial_puts(" checks=");
        serial_u32((u32)tdt_checks);
        serial_puts("\r\n");
    }

    if (tdt_snap) {
        kernel_free_pages_ptr(tdt_snap, tdt_snap_pages);
        tdt_snap = 0;
    }

    __asm__ __volatile__("push %0; popf" : : "r"(tdt_irq_flags) : "memory", "cc");
    machine_halt();
}
