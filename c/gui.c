#include "gui.h"

#define TASKBAR_Y (scr_h - 14)

#define TERM_ROWS 96
#define TERM_COLS 80
#define GUI_KEY_PGUP 0xFE
#define GUI_KEY_PGDN 0xFF
#define GUI_KEY_UP 0xF7
#define GUI_KEY_DOWN 0xF8
#define GUI_KEY_LEFT 0xF9
#define GUI_KEY_RIGHT 0xFA
#define GUI_KEY_HOME 0xFB
#define GUI_KEY_END 0xFC
#define GUI_KEY_DEL 0xFD
#define SYS_W 236
#define SYS_H 98
#define ABOUT_W 208
#define ABOUT_H 104

#define NCONS 3
#define WIN_C0 0
#define WIN_C1 1
#define WIN_C2 2
#define WIN_SYS 3
#define WIN_ABOUT 4
#define WIN_APP 5
#define WIN_COUNT 6

#define MENU_X 2
#define MENU_W 120
#define MENU_ITEM_H 12
#define MENU_ITEMS 7
#define MENU_H (MENU_ITEMS * MENU_ITEM_H + 4)
#define MENU_Y (TASKBAR_Y - MENU_H - 4)

#define LIST_W 188
#define LIST_H 112
#define LIST_X ((scr_w - LIST_W) / 2)
#define LIST_Y ((TASKBAR_Y - LIST_H) / 2)

#define HIT_NONE 0
#define HIT_START 1
#define HIT_TB_C1 2
#define HIT_TB_C2 3
#define HIT_TB_SYS 4
#define HIT_TB_WIN 5
#define HIT_TB_C3 6
#define HIT_TB_APP 7
#define HIT_MENU_BASE 100
#define HIT_CLOSE_BASE 200
#define HIT_OK 300
#define HIT_LIST_BASE 400
#define HIT_TITLE_BASE 500
#define HIT_BODY_BASE 600
#define HIT_SCROLL_BASE 700
#define HIT_MIN_BASE 800
#define HIT_MAX_BASE 900
#define HIT_CTX_BASE 1000
#define HIT_RESIZE_BASE 1500
#define CANVAS_MAX_W 600
#define CANVAS_MAX_H 420

struct console {
    int x, y, w, h;
    char term[TERM_ROWS][TERM_COLS];
    int n;
    int cur;
    int col;
    int scroll;
    int cols, rows;
    u8 esc;
    char cmd_buf[128];
    char history[8][128];
    int cmd_len, cmd_pos;
    int cmd_start, cmd_rows;
    int hist_n, hist_next, hist_pos;
};

struct window {
    int x, y, w, h;
    int saved_x, saved_y, saved_w, saved_h;
    u8 visible;
    u8 minimized;
    u8 maximized;
    const char *title;
};

static u8 *fb;
static const u8 *const FONT = (const u8 *)0x000FFA6E;

static int con_w = 296;
static int con_h = 144;
static int term_vis = 14;
static int term_width = 35;
static int canvas_w = 180;
static int canvas_h = 120;

static struct window wins[WIN_COUNT];
static struct console consoles[NCONS];
static int zorder[WIN_COUNT];
static int focus_win;
static int last_console;
static struct console *sys_output;

static int menu_open;
static int list_open;
static int list_sel;
static int ctx_open;
static int ctx_x, ctx_y;
static int ctx_count;
static int ctx_target;

static u8 *canvas_buf;
static int canvas_open;
static char canvas_title[32] = "Canvas";
static struct gui_canvas_event canvas_ev;
static u8 canvas_ev_pending;

static int pressed_hit;
static int drag_win;
static int drag_dx, drag_dy;
static int resize_win = -1;
static int resize_dir;
static int resize_sx, resize_sy, resize_sw, resize_sh;
static int scroll_drag = -1;
static int last_buttons;

static char clock_hm[6];
static char clock_hms[9];
static char date_str[11];
static char uptime_str[9];
static volatile u8 gui_dirty = 1;
static u32 rtc_last_tick;
static u8 rtc_valid;

static const char cursor_art[18][13] = {
    "X...........", "XX..........", "XWX.........", "XWWX........",
    "XWWWX.......", "XWWWWX......", "XWWWWWX.....", "XWWWWWWX....",
    "XWWWWWWWX...", "XWWWWWWWWX..", "XWWWWWWWWWX.", "XWWWWWXXXXXX",
    "XWWWXWX.....", "XWWX.XWX....", "XWX..XWX....", "XX....XWX...",
    "X.....XWX...", "......XXX..."
};
static u8 cursor_bg[18][12];
static int cursor_px = -1, cursor_py = -1;
static u8 cursor_bg_valid;
static int cursor_shape;

static inline u8 inb(u16 port) {
    u8 value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(u16 port, u8 value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

/* -----------------------------------------------------------------------------
 * Framebuffer primitives
 * ----------------------------------------------------------------------------- */
static const u8 base_palette[48] = {
    0, 0, 0,       0, 0, 42,      0, 42, 0,       0, 42, 42,
    42, 0, 0,      42, 0, 42,     42, 42, 0,      42, 42, 42,
    21, 21, 21,    0, 0, 63,      0, 63, 0,       0, 63, 63,
    63, 0, 0,      63, 0, 63,     63, 63, 0,      63, 63, 63
};

static void set_palette(void) {
    u8 pal[768];
    for (int i = 0; i < 48; i++)
        pal[i] = base_palette[i];
    for (int r = 0; r < 6; r++) {
        for (int g = 0; g < 6; g++) {
            for (int b = 0; b < 6; b++) {
                int i = 16 + r * 36 + g * 6 + b;
                pal[i * 3 + 0] = (u8)((r * 63 + 2) / 5);
                pal[i * 3 + 1] = (u8)((g * 63 + 2) / 5);
                pal[i * 3 + 2] = (u8)((b * 63 + 2) / 5);
            }
        }
    }
    for (int i = 232; i < 256; i++) {
        int v = 2 + ((i - 232) * 61 + 11) / 23;
        pal[i * 3 + 0] = (u8)v;
        pal[i * 3 + 1] = (u8)v;
        pal[i * 3 + 2] = (u8)v;
    }
    outb(0x3C8, 0);
    for (int i = 0; i < 768; i++)
        outb(0x3C9, pal[i]);
}

static void plot(int x, int y, u8 color) {
    if ((unsigned)x < (unsigned)scr_w && (unsigned)y < (unsigned)scr_h)
        fb[y * scr_w + x] = color;
}

static void fill_rect(int x, int y, int w, int h, u8 color) {
    if (w <= 0 || h <= 0)
        return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > scr_w) w = scr_w - x;
    if (y + h > scr_h) h = scr_h - y;
    if (w <= 0 || h <= 0)
        return;
    for (int yy = 0; yy < h; yy++) {
        u8 *p = &fb[(y + yy) * scr_w + x];
        for (int xx = 0; xx < w; xx++)
            p[xx] = color;
    }
}

static void draw_char(int x, int y, char ch, u8 color, int scale) {
    const u8 *glyph = FONT + (u8)ch * 8;
    for (int r = 0; r < 8; r++) {
        u8 bits = glyph[r];
        for (int c = 0; c < 8; c++) {
            if (bits & (0x80u >> c)) {
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        plot(x + c * scale + sx, y + r * scale + sy, color);
            }
        }
    }
}

static void text(int x, int y, const char *s, u8 color) {
    while (*s) {
        draw_char(x, y, *s++, color, 1);
        x += 8;
        if (x >= scr_w)
            break;
    }
}

static void text2(int x, int y, const char *s, u8 color) {
    while (*s) {
        draw_char(x, y, *s++, color, 2);
        x += 16;
        if (x >= scr_w)
            break;
    }
}

static int inside(int x, int y, int rx, int ry, int rw, int rh);

static u8 is_pressed(int hit) {
    return pressed_hit == hit && (mouse_buttons & 1);
}

static void raise_box_col(int x, int y, int w, int h, u8 face) {
    fill_rect(x, y, w, h, 0);
    fill_rect(x + 1, y + 1, w - 2, h - 2, face);
    fill_rect(x + 1, y + 1, w - 2, 1, 15);
    fill_rect(x + 1, y + 1, 1, h - 2, 15);
    fill_rect(x + 1, y + h - 2, w - 2, 1, 8);
    fill_rect(x + w - 2, y + 1, 1, h - 2, 8);
}

static void sunken_box(int x, int y, int w, int h) {
    fill_rect(x, y, w, h, 0);
    fill_rect(x + 1, y + 1, w - 2, h - 2, 7);
    fill_rect(x + 1, y + 1, w - 2, 1, 8);
    fill_rect(x + 1, y + 1, 1, h - 2, 8);
    fill_rect(x + 1, y + h - 2, w - 2, 1, 15);
    fill_rect(x + w - 2, y + 1, 1, h - 2, 15);
}

static void blit(void) {
    if (vbe_active && vbe_lfb) {
        for (int y = 0; y < scr_h; y++) {
            volatile u32 *dst = (volatile u32 *)(vbe_lfb + (u32)y * vbe_pitch);
            const u32 *src = (const u32 *)(fb + (u32)y * (u32)scr_w);
            for (int i = 0; i < scr_w / 4; i++)
                dst[i] = src[i];
        }
        return;
    }
    u32 *dst = (u32 *)0x000A0000;
    const u32 *src = (const u32 *)fb;
    for (int i = 0; i < (scr_w * scr_h) / 4; i++)
        dst[i] = src[i];
}

static void blit_rect(int x, int y, int w, int h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > scr_w) w = scr_w - x;
    if (y + h > scr_h) h = scr_h - y;
    if (w <= 0 || h <= 0)
        return;
    if (vbe_active && vbe_lfb) {
        for (int yy = 0; yy < h; yy++) {
            volatile u8 *dst = vbe_lfb + (u32)(y + yy) * vbe_pitch + (u32)x;
            const u8 *src = fb + (u32)(y + yy) * (u32)scr_w + (u32)x;
            for (int xx = 0; xx < w; xx++)
                dst[xx] = src[xx];
        }
        return;
    }
    u8 *dst = (u8 *)0x000A0000;
    for (int yy = 0; yy < h; yy++) {
        for (int xx = 0; xx < w; xx++)
            dst[(y + yy) * scr_w + x + xx] = fb[(y + yy) * scr_w + x + xx];
    }
}

/* -----------------------------------------------------------------------------
 * RTC / time strings
 * ----------------------------------------------------------------------------- */
static u8 cmos_read(u8 reg) {
    outb(0x70, (u8)(0x80 | reg));
    return inb(0x71);
}

static int bcd2(u8 v) {
    return (v >> 4) * 10 + (v & 0x0F);
}

static void two_digits(char *out, int value) {
    out[0] = (char)('0' + (value / 10) % 10);
    out[1] = (char)('0' + value % 10);
}

static void update_clock(int force) {
    if (!force && rtc_valid && (u32)(ticks - rtc_last_tick) < 50)
        return;
    rtc_last_tick = ticks;
    rtc_valid = 1;
    int sec = bcd2(cmos_read(0x00));
    int min = bcd2(cmos_read(0x02));
    int hour = bcd2(cmos_read(0x04));
    int day = bcd2(cmos_read(0x07));
    int month = bcd2(cmos_read(0x08));
    int year = bcd2(cmos_read(0x09));
    int century = bcd2(cmos_read(0x32));

    clock_hm[0] = (char)('0' + hour / 10);
    clock_hm[1] = (char)('0' + hour % 10);
    clock_hm[2] = ':';
    clock_hm[3] = (char)('0' + min / 10);
    clock_hm[4] = (char)('0' + min % 10);
    clock_hm[5] = 0;

    two_digits(&clock_hms[0], hour);
    clock_hms[2] = ':';
    two_digits(&clock_hms[3], min);
    clock_hms[5] = ':';
    two_digits(&clock_hms[6], sec);
    clock_hms[8] = 0;

    two_digits(&date_str[0], century);
    two_digits(&date_str[2], year);
    date_str[4] = '-';
    two_digits(&date_str[5], month);
    date_str[7] = '-';
    two_digits(&date_str[8], day);
    date_str[10] = 0;
}

static void make_uptime(void) {
    u32 sec = ticks / 100;
    u32 h = (sec / 3600) % 100;
    u32 m = (sec / 60) % 60;
    u32 s = sec % 60;
    two_digits(&uptime_str[0], (int)h);
    uptime_str[2] = ':';
    two_digits(&uptime_str[3], (int)m);
    uptime_str[5] = ':';
    two_digits(&uptime_str[6], (int)s);
    uptime_str[8] = 0;
}

/* -----------------------------------------------------------------------------
 * Window management
 * ----------------------------------------------------------------------------- */
static void bring_front(int id) {
    int pos = 0;
    for (int i = 0; i < WIN_COUNT; i++) {
        if (zorder[i] == id) {
            pos = i;
            break;
        }
    }
    for (int i = pos; i < WIN_COUNT - 1; i++)
        zorder[i] = zorder[i + 1];
    zorder[WIN_COUNT - 1] = id;
}

static void focus_window(int id) {
    if (id < 0 || id >= WIN_COUNT)
        return;
    wins[id].visible = 1;
    wins[id].minimized = 0;
    focus_win = id;
    bring_front(id);
    if (id >= WIN_C0 && id <= WIN_C2) {
        sys_output = &consoles[id];
        last_console = id;
    }
}

int gui_focused_console(void) {
    if (focus_win >= WIN_C0 && focus_win <= WIN_C2)
        return focus_win;
    return last_console;
}

int gui_runtime_keyboard_wanted(void) {
    return menu_open || list_open || focus_win == WIN_ABOUT || focus_win == WIN_SYS;
}

static void focus_fallback(void) {
    for (int i = WIN_COUNT - 1; i >= 0; i--) {
        int id = zorder[i];
        if (wins[id].visible) {
            focus_window(id);
            return;
        }
    }
    focus_win = -1;
}

static void hide_window(int id) {
    wins[id].visible = 0;
    wins[id].minimized = 0;
    if (focus_win == id)
        focus_fallback();
}

static void minimize_window(int id) {
    wins[id].visible = 0;
    wins[id].minimized = 1;
    if (focus_win == id)
        focus_fallback();
}

static void init_windows(void) {
    if (scr_w >= 640) {
        term_width = 72;
        term_vis = 26;
        con_w = 596;
        con_h = 252;
        canvas_w = 300;
        canvas_h = 200;

        wins[WIN_C0].x = 12;  wins[WIN_C0].y = 24;
        wins[WIN_C1].x = 28;  wins[WIN_C1].y = 198;
        wins[WIN_C2].x = 20;  wins[WIN_C2].y = 110;

        wins[WIN_SYS].x = 388; wins[WIN_SYS].y = 256;
        wins[WIN_ABOUT].x = (scr_w - ABOUT_W) / 2;
        wins[WIN_ABOUT].y = (TASKBAR_Y - ABOUT_H) / 2;
        wins[WIN_APP].x = scr_w - (canvas_w + 4) - 12;
        wins[WIN_APP].y = 26;
    } else {
        term_width = 35;
        term_vis = 14;
        con_w = 296;
        con_h = 144;
        canvas_w = 180;
        canvas_h = 120;

        wins[WIN_C0].x = 12;  wins[WIN_C0].y = 22;
        wins[WIN_C1].x = 16;  wins[WIN_C1].y = 28;
        wins[WIN_C2].x = 20;  wins[WIN_C2].y = 34;

        wins[WIN_SYS].x = 42; wins[WIN_SYS].y = 42;
        wins[WIN_ABOUT].x = 56; wins[WIN_ABOUT].y = 48;
        wins[WIN_APP].x = 68; wins[WIN_APP].y = 34;
    }

    for (int i = 0; i < NCONS; i++) {
        wins[i].w = con_w;
        wins[i].h = con_h;
        wins[i].visible = (i == 0);
        wins[i].minimized = 0;
    }
    wins[WIN_C0].title = "Console 1 - MiniOS";
    wins[WIN_C1].title = "Console 2 - MiniOS";
    wins[WIN_C2].title = "Console 3 - MiniOS";

    wins[WIN_SYS].w = SYS_W; wins[WIN_SYS].h = SYS_H;
    wins[WIN_SYS].visible = 0;
    wins[WIN_SYS].minimized = 0;
    wins[WIN_SYS].title = "System Information";

    wins[WIN_ABOUT].w = ABOUT_W; wins[WIN_ABOUT].h = ABOUT_H;
    wins[WIN_ABOUT].visible = 0;
    wins[WIN_ABOUT].minimized = 0;
    wins[WIN_ABOUT].title = "About MiniOS";

    wins[WIN_APP].w = canvas_w + 4;
    wins[WIN_APP].h = canvas_h + 14;
    wins[WIN_APP].visible = 0;
    wins[WIN_APP].minimized = 0;
    wins[WIN_APP].title = canvas_title;

    for (int i = 0; i < WIN_COUNT; i++)
        zorder[i] = i;
    focus_win = WIN_C0;
}

/* -----------------------------------------------------------------------------
 * Terminal / shell
 * ----------------------------------------------------------------------------- */
static int line_len(struct console *c, int index) {
    int n = 0;
    while (n < TERM_COLS - 1 && c->term[index][n])
        n++;
    return n;
}

static void term_clear(struct console *c) {
    for (int r = 0; r < TERM_ROWS; r++)
        for (int col = 0; col < TERM_COLS; col++)
            c->term[r][col] = 0;
    c->n = 1;
    c->cur = 0;
    c->col = 0;
    c->scroll = 0;
    c->esc = 0;
}

static void term_newline(struct console *c) {
    int was_scrolled = c->scroll > 0;
    if (c->n < TERM_ROWS) {
        for (int col = 0; col < TERM_COLS; col++)
            c->term[c->n][col] = 0;
        c->cur = c->n;
        c->n++;
    } else {
        for (int r = 1; r < TERM_ROWS; r++)
            for (int col = 0; col < TERM_COLS; col++)
                c->term[r - 1][col] = c->term[r][col];
        for (int col = 0; col < TERM_COLS; col++)
            c->term[TERM_ROWS - 1][col] = 0;
        c->cur = TERM_ROWS - 1;
    }
    if (was_scrolled) {
        c->scroll++;
        int max = c->n > c->rows ? c->n - c->rows : 0;
        if (c->scroll > max)
            c->scroll = max;
    }
    c->col = 0;
}

static void term_scroll_by(struct console *c, int delta) {
    int max = c->n > c->rows ? c->n - c->rows : 0;
    c->scroll += delta;
    if (c->scroll < 0)
        c->scroll = 0;
    if (c->scroll > max)
        c->scroll = max;
}

static void scroll_metrics(struct console *c, int *bx, int *by, int *bh,
                           int *thumb, int *pos) {
    int track_h = c->rows * 9 - 2;
    int t = track_h * c->rows / (c->n > 0 ? c->n : 1);
    if (t < 8) t = 8;
    if (t > track_h) t = track_h;
    int max = c->n > c->rows ? c->n - c->rows : 0;
    int travel = track_h - t;
    int p = max > 0 ? travel * (max - c->scroll) / max : 0;
    *bx = c->x + c->w - 8;
    *by = c->y + 14;
    *bh = track_h;
    *thumb = t;
    *pos = p;
}

static int scrollbar_hit(struct console *c, int x, int y) {
    if (c->n <= c->rows)
        return 0;
    int bx, by, bh, thumb, pos;
    scroll_metrics(c, &bx, &by, &bh, &thumb, &pos);
    (void)thumb;
    (void)pos;
    return x >= bx - 1 && x < bx + 5 && y >= by && y < by + bh;
}

static void scrollbar_set_from_y(struct console *c, int y) {
    if (c->n <= c->rows)
        return;
    int bx, by, bh, thumb, pos;
    scroll_metrics(c, &bx, &by, &bh, &thumb, &pos);
    (void)bx;
    (void)pos;
    int max = c->n - c->rows;
    int travel = bh - thumb;
    int rel = y - by - thumb / 2;
    if (rel < 0) rel = 0;
    if (rel > travel) rel = travel;
    c->scroll = max - (travel > 0 ? (rel * max + travel / 2) / travel : 0);
    if (c->scroll < 0) c->scroll = 0;
    if (c->scroll > max) c->scroll = max;
}

static void scrollbar_draw(struct console *c) {
    if (c->n <= c->rows)
        return;
    int bx, by, bh, thumb, pos;
    scroll_metrics(c, &bx, &by, &bh, &thumb, &pos);
    fill_rect(bx, by, 4, bh, 8);
    fill_rect(bx, by + pos, 4, thumb, 11);
}

static int term_ansi_final(struct console *c, char ch) {
    if (ch == 'D') {
        if (c->col > 0)
            c->col--;
    } else if (ch == 'C') {
        if (c->col < c->cols - 1)
            c->col++;
    } else if (ch == 'H') {
        c->col = 0;
    } else if (ch == 'K') {
        for (int i = c->col; i < TERM_COLS; i++)
            c->term[c->cur][i] = 0;
    }
    return 1;
}

static void term_putc_mode(struct console *c, char ch, int insert) {
    if (c->esc) {
        if (c->esc == 1) {
            if (ch == '[')
                c->esc = 2;
            else
                c->esc = 0;
        } else {
            if ((ch >= '0' && ch <= '9') || ch == ';' || ch == '?')
                return;
            c->esc = 0;
            term_ansi_final(c, ch);
        }
        return;
    }
    if (ch == 27) {
        c->esc = 1;
        return;
    }
    if (ch == '\r') {
        c->col = 0;
        return;
    }
    if (ch == '\n') {
        term_newline(c);
        return;
    }
    if (ch == '\b') {
        int len = line_len(c, c->cur);
        if (c->col > 0) {
            for (int i = c->col - 1; i < len - 1; i++)
                c->term[c->cur][i] = c->term[c->cur][i + 1];
            c->term[c->cur][len > 0 ? len - 1 : 0] = 0;
            c->col--;
        }
        return;
    }
    if (ch == '\t') {
        int spaces = 4 - (c->col & 3);
        while (spaces-- > 0)
            term_putc_mode(c, ' ', insert);
        return;
    }
    if (ch < 32)
        return;
    if (c->col >= c->cols)
        term_newline(c);
    int len = line_len(c, c->cur);
    if (insert && c->col < len && len < TERM_COLS - 2) {
        for (int i = len; i > c->col; i--)
            c->term[c->cur][i] = c->term[c->cur][i - 1];
        c->term[c->cur][len + 1] = 0;
    } else if (c->col > len) {
        for (int i = len; i < c->col; i++)
            c->term[c->cur][i] = ' ';
    }
    c->term[c->cur][c->col] = ch;
    c->col++;
    if (c->col >= len)
        c->term[c->cur][c->col] = 0;
}

static void term_putc(struct console *c, char ch) {
    term_putc_mode(c, ch, 0);
}

static void term_puts(struct console *c, const char *s) {
    while (*s)
        term_putc(c, *s++);
}

static int streq(const char *a, const char *b);
static int hit_test(int x, int y);

static void term_prompt(struct console *c) {
    c->cmd_start = c->cur;
    c->cmd_len = 0;
    c->cmd_pos = 0;
    c->cmd_buf[0] = 0;
    c->cmd_rows = 1;
    c->hist_pos = -1;
    term_puts(c, "C:\\> ");
}

static void cmd_render(struct console *c) {
    int w = c->cols;
    int newrows = (5 + c->cmd_len + w - 1) / w;
    int rows = newrows > c->cmd_rows ? newrows : c->cmd_rows;
    if (newrows < 1) newrows = 1;
    for (int r = 0; r < rows; r++) {
        int row = c->cmd_start + r;
        if (row < 0 || row >= TERM_ROWS)
            break;
        for (int x = 0; x < TERM_COLS; x++)
            c->term[row][x] = 0;
        if (row >= c->n)
            c->n = row + 1;
    }
    if (c->n < c->cmd_start + newrows)
        c->n = c->cmd_start + newrows;
    if (c->n > TERM_ROWS)
        c->n = TERM_ROWS;
    const char *prompt = "C:\\> ";
    if (c->cmd_start < TERM_ROWS) {
        for (int i = 0; i < 5 && i < TERM_COLS; i++)
            c->term[c->cmd_start][i] = prompt[i];
    }
    for (int i = 0; i < c->cmd_len; i++) {
        int abs = 5 + i;
        int row = c->cmd_start + abs / w;
        int col = abs % w;
        if (row >= 0 && row < TERM_ROWS && col < TERM_COLS)
            c->term[row][col] = c->cmd_buf[i];
    }
    for (int r = 0; r < newrows; r++) {
        int row = c->cmd_start + r;
        if (row < 0 || row >= TERM_ROWS)
            break;
        int used = 5 + c->cmd_len - r * w;
        if (used > w) used = w;
        if (used < 0) used = 0;
        if (used < TERM_COLS)
            c->term[row][used] = 0;
    }
    c->cmd_rows = newrows;
    int abs = 5 + c->cmd_pos;
    c->cur = c->cmd_start + abs / w;
    c->col = abs % w;
    if (c->cur >= TERM_ROWS)
        c->cur = TERM_ROWS - 1;
    if (c->cur >= c->n)
        c->n = c->cur + 1;
    c->scroll = 0;
}

static void hist_add(struct console *c) {
    if (c->cmd_len == 0)
        return;
    if (c->hist_n > 0) {
        int last = (c->hist_next + 7) & 7;
        if (streq(c->history[last], c->cmd_buf))
            return;
    }
    int i = 0;
    while (i < 127 && c->cmd_buf[i]) {
        c->history[c->hist_next][i] = c->cmd_buf[i];
        i++;
    }
    c->history[c->hist_next][i] = 0;
    c->hist_next = (c->hist_next + 1) & 7;
    if (c->hist_n < 8)
        c->hist_n++;
}

static void hist_load(struct console *c, int pos) {
    if (pos < 0 || pos >= c->hist_n) {
        c->cmd_len = 0;
        c->cmd_pos = 0;
        c->cmd_buf[0] = 0;
        c->hist_pos = -1;
        cmd_render(c);
        return;
    }
    int idx = (c->hist_next - 1 - pos + 16) & 7;
    int n = 0;
    while (n < 127 && c->history[idx][n]) {
        c->cmd_buf[n] = c->history[idx][n];
        n++;
    }
    c->cmd_buf[n] = 0;
    c->cmd_len = n;
    c->cmd_pos = n;
    c->hist_pos = pos;
    cmd_render(c);
}

static void term_u32(struct console *c, u32 v) {
    char b[12];
    int i = 0;
    if (!v) {
        term_putc(c, '0');
        return;
    }
    while (v) {
        b[i++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (i)
        term_putc(c, b[--i]);
}

static void term_init(struct console *c, int index) {
    term_clear(c);
    term_puts(c, "MiniOS 2.0 - C GUI\n");
    char line[24];
    line[0] = 'C'; line[1] = 'o'; line[2] = 'n'; line[3] = 's'; line[4] = 'o';
    line[5] = 'l'; line[6] = 'e'; line[7] = ' ';
    line[8] = (char)('1' + index);
    line[9] = 0;
    term_puts(c, line);
    term_puts(c, "\n\n");
    term_prompt(c);
}

static int streq(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static int starts_with(const char *s, const char *p) {
    while (*p) {
        if (*s++ != *p++)
            return 0;
    }
    return 1;
}

static void launch_python(struct console *c, const char *rest) {
    extern int run_user_module_args(const char *name, int argc,
                                    const char *const *argv);
    char buf[128];
    const char *argv[8];
    int argc = 0;
    argv[argc++] = "py";

    int bi = 0;
    while (*rest && argc < 8 && bi < 126) {
        while (*rest == ' ')
            rest++;
        if (!*rest)
            break;
        char quote = 0;
        if (*rest == '"' || *rest == '\'') {
            quote = *rest++;
        }
        argv[argc++] = &buf[bi];
        while (*rest && bi < 126) {
            if (quote) {
                if (*rest == quote) {
                    rest++;
                    break;
                }
            } else if (*rest == ' ') {
                break;
            }
            buf[bi++] = *rest++;
        }
        buf[bi++] = 0;
    }

    if (!run_user_module_args("MPY.BIN", argc, argv))
        term_puts(c, "Python launch failed.\n");
}

static void launch_tool(struct console *c, const char *file, const char *arg) {
    char line[128];
    int n = 0;
    while (file[n] && n < 40) {
        line[n] = file[n];
        n++;
    }
    if (arg && *arg) {
        line[n++] = ' ';
        while (*arg && n < 126)
            line[n++] = *arg++;
    }
    line[n] = 0;
    launch_python(c, line);
}

static void run_python_expr(struct console *c, const char *rest) {
    extern int run_user_module_args(const char *name, int argc,
                                    const char *const *argv);
    char expr[120];
    int n = 0;
    while (*rest == ' ')
        rest++;
    while (*rest && n < 118)
        expr[n++] = *rest++;
    expr[n] = 0;
    if (n == 0) {
        term_puts(c, "usage: = <expression>\n");
        return;
    }
    const char *argv[3] = { "py", "-e", expr };
    if (!run_user_module_args("MPY.BIN", 3, argv))
        term_puts(c, "Python expression failed.\n");
}

static void py_stats(struct console *c) {
    extern volatile u32 user_module_last_ticks;
    extern volatile u32 user_module_cold_ticks;
    extern volatile u32 user_module_warm_ticks;
    extern volatile u32 user_module_last_size;
    extern volatile u8 user_module_cache_hit;
    term_puts(c, "mpy last: ");
    term_puts(c, user_module_cache_hit ? "warm" : "cold");
    term_puts(c, " ticks=");
    term_u32(c, user_module_last_ticks);
    term_puts(c, " (cold=");
    term_u32(c, user_module_cold_ticks);
    term_puts(c, " warm=");
    term_u32(c, user_module_warm_ticks);
    term_puts(c, ") bytes=");
    term_u32(c, user_module_last_size);
    term_puts(c, "\n");
}

static void run_command(struct console *c, char *line) {
    while (*line == ' ')
        line++;
    if (!*line)
        return;

    extern int kernel_runtime_busy(void);
    if (kernel_runtime_busy() &&
        (*line == '=' ||
         streq(line, "ls") || streq(line, "disktest") ||
         streq(line, "ring3") || streq(line, "tasks") ||
         streq(line, "progs") || streq(line, "utest") ||
         streq(line, "py") || streq(line, "python") ||
         streq(line, "modtest") || streq(line, "uargs") ||
         streq(line, "big512") || streq(line, "serial") ||
         streq(line, "calc") || streq(line, "top") || streq(line, "bench") ||
         streq(line, "demos") || streq(line, "plot") || streq(line, "futil") ||
         streq(line, "xxd") || streq(line, "edit") ||
         starts_with(line, "calc ") || starts_with(line, "xxd ") ||
         starts_with(line, "plot ") ||
         starts_with(line, "edit ") || streq(line, "sync") ||
         streq(line, "reboot") ||
         streq(line, "halt") ||
         starts_with(line, "py ") || starts_with(line, "python ") ||
         starts_with(line, "exec ") || starts_with(line, "run ") ||
         starts_with(line, "mod ") || starts_with(line, "upload ") ||
         starts_with(line, "download "))) {
        term_puts(c, "[busy] command unavailable while a task is running.\n");
        return;
    }

    if (streq(line, "help")) {
        term_puts(c, "Commands:\n");
        term_puts(c, "help ver cpu mem time date ps kill\n");
        term_puts(c, "py python clear echo console windows\n");
        term_puts(c, "serial upload NAME download NAME sync\n");
        term_puts(c, "py pystat = EXPR calc top demos bench\n");
        term_puts(c, "xxd FILE edit FILE futil ps kill PID\n");
        term_puts(c, "sysinfo about ring3 tasks progs\n");
        term_puts(c, "run NAME ls exec disktest\n");
        term_puts(c, "modtest mod NAME utest reboot halt\n");
    } else if (streq(line, "ver")) {
        term_puts(c, "MiniOS 2.0 C GUI (32-bit)\n");
    } else if (streq(line, "cpu")) {
        term_puts(c, "CPU: ");
        term_puts(c, cpu);
        term_puts(c, "\n");
    } else if (streq(line, "mem")) {
        term_puts(c, "Heap: ");
        term_u32(c, heap_used);
        term_puts(c, " / 65536 bytes\n");
    } else if (streq(line, "time")) {
        update_clock(1);
        term_puts(c, "Time: ");
        term_puts(c, clock_hms);
        term_puts(c, "\n");
    } else if (streq(line, "date")) {
        update_clock(1);
        term_puts(c, "Date: ");
        term_puts(c, date_str);
        term_puts(c, "\n");
    } else if (streq(line, "uptime")) {
        make_uptime();
        term_puts(c, "Uptime: ");
        term_puts(c, uptime_str);
        term_puts(c, "\n");
    } else if (streq(line, "clear")) {
        term_clear(c);
        term_puts(c, "MiniOS 2.0 - C GUI\n\n");
    } else if (streq(line, "echo")) {
        term_puts(c, "\n");
    } else if (starts_with(line, "echo ")) {
        term_puts(c, line + 5);
        term_puts(c, "\n");
    } else if (streq(line, "windows")) {
        list_open = 1;
        list_sel = focus_win >= 0 ? focus_win : 0;
        menu_open = 0;
        gui_redraw();
    } else if (starts_with(line, "console ")) {
        int n = (line[8] >= '1' && line[8] <= '3') ? line[8] - '1' : -1;
        if (n >= 0) {
            focus_window(n);
            term_puts(c, "Console switched.\n");
        } else {
            term_puts(c, "Usage: console 1|2|3\n");
        }
    } else if (streq(line, "ps")) {
        extern void task_ps(void);
        task_ps();
    } else if (starts_with(line, "kill ")) {
        extern int task_kill_pid(u32 pid);
        u32 pid = 0;
        const char *p = line + 5;
        while (*p >= '0' && *p <= '9') {
            pid = pid * 10 + (u32)(*p - '0');
            p++;
        }
        int r = task_kill_pid(pid);
        if (r == 1) term_puts(c, "killed.\n");
        else if (r == 2) term_puts(c, "kill pending; scheduler will reap it.\n");
        else term_puts(c, "no such task.\n");
    } else if (streq(line, "big512")) {
        extern int run_user_module(const char *name);
        term_puts(c, "Large UMP1 payload test...\n");
        if (!run_user_module("BIG512.BIN"))
            term_puts(c, "big512 failed.\n");
    } else if (streq(line, "uargs")) {
        extern int run_user_module_args(const char *name, int argc,
                                        const char *const *argv);
        static const char *args[2] = { "alpha", "beta" };
        term_puts(c, "Large task argv test...\n");
        if (!run_user_module_args("BIGARG.BIN", 2, args))
            term_puts(c, "uargs failed.\n");
    } else if (streq(line, "sync")) {
        extern void sync_savefs(void);
        term_puts(c, "Saving RAMFS to floppy...\n");
        sync_savefs();
    } else if (streq(line, "serial")) {
        extern void serial_info(void);
        serial_info();
        term_puts(c, "COM1 115200 8N1 ready.\n");
    } else if (starts_with(line, "upload ")) {
        extern int serial_upload_file(const char *name);
        term_puts(c, "Receiving from COM1...\n");
        if (!serial_upload_file(line + 7))
            term_puts(c, "Upload failed.\n");
    } else if (starts_with(line, "download ")) {
        extern int serial_download_file(const char *name);
        if (!serial_download_file(line + 9))
            term_puts(c, "Download failed.\n");
    } else if (streq(line, "sysinfo")) {
        focus_window(WIN_SYS);
        term_puts(c, "System window opened.\n");
    } else if (streq(line, "about")) {
        focus_window(WIN_ABOUT);
        term_puts(c, "About window opened.\n");
    } else if (streq(line, "ring3")) {
        extern void enter_ring3_demo(void);
        term_puts(c, "Entering Ring 3 (paging on)...\n");
        gui_redraw();
        enter_ring3_demo();
    } else if (streq(line, "tasks")) {
        extern void run_task_demo(void);
        term_puts(c, "Starting preemptive Ring 3 tasks...\n");
        gui_redraw();
        run_task_demo();
    } else if (streq(line, "progs")) {
        extern void list_programs(void);
        list_programs();
    } else if (streq(line, "disktest")) {
        extern void disk_test(void);
        disk_test();
    } else if (streq(line, "modtest")) {
        extern int run_module(const char *name);
        term_puts(c, "Loading kernel module from MiniFS...\n");
        gui_redraw();
        run_module("MODTEST.BIN");
    } else if (starts_with(line, "mod ")) {
        extern int run_module(const char *name);
        term_puts(c, "Loading kernel module from MiniFS...\n");
        gui_redraw();
        run_module(line + 4);
    } else if (streq(line, "calc")) {
        launch_tool(c, "-m calc", "");
    } else if (starts_with(line, "calc ")) {
        launch_tool(c, "-m calc", line + 5);
    } else if (streq(line, "top")) {
        launch_tool(c, "-m top", "");
    } else if (streq(line, "bench")) {
        launch_tool(c, "-m bench", "");
    } else if (streq(line, "demos") || streq(line, "plot")) {
        launch_tool(c, "-m demos", "");
    } else if (starts_with(line, "plot ")) {
        launch_tool(c, "-m demos", line + 5);
    } else if (streq(line, "futil")) {
        launch_tool(c, "-m futil", "");
    } else if (starts_with(line, "xxd ")) {
        launch_tool(c, "-m xxd", line + 4);
    } else if (starts_with(line, "edit ")) {
        launch_tool(c, "-m ed", line + 5);
    } else if (*line == '=') {
        run_python_expr(c, line + 1);
    } else if (streq(line, "pystat")) {
        py_stats(c);
    } else if (streq(line, "py") || streq(line, "python")) {
        launch_python(c, "");
    } else if (starts_with(line, "py ")) {
        launch_python(c, line + 3);
    } else if (starts_with(line, "python ")) {
        launch_python(c, line + 7);
    } else if (streq(line, "utest")) {
        extern int run_user_module(const char *name);
        term_puts(c, "Loading large Ring3 module from MiniFS...\n");
        gui_redraw();
        run_user_module("UMTEST.BIN");
    } else if (streq(line, "ls")) {
        extern void list_disk_files(void);
        extern void list_ram_files(void);
        list_disk_files();
        list_ram_files();
    } else if (starts_with(line, "exec ")) {
        extern int exec_disk(const char *name);
        term_puts(c, "Reading MiniFS from floppy...\n");
        gui_redraw();
        if (!exec_disk(line + 5))
            term_puts(c, "Exec failed.\n");
    } else if (starts_with(line, "run ")) {
        extern int run_pack_program(const char *name);
        term_puts(c, "Loading from program pack...\n");
        gui_redraw();
        if (!run_pack_program(line + 4))
            term_puts(c, "Run failed.\n");
    } else if (streq(line, "reboot")) {
        term_puts(c, "Rebooting...\n");
        machine_reboot();
    } else if (streq(line, "halt")) {
        term_puts(c, "CPU halted.\n");
        machine_halt();
    } else {
        term_puts(c, "Bad command or file name\n");
    }
}

static void term_key(struct console *c, char ch) {
    u8 key = (u8)ch;
    c->scroll = 0;
    if (key == '\n') {
        c->cmd_buf[c->cmd_len] = 0;
        hist_add(c);
        char command[128];
        int i = 0;
        while (i < 127 && c->cmd_buf[i]) {
            command[i] = c->cmd_buf[i];
            i++;
        }
        command[i] = 0;
        c->cur = c->cmd_start + (5 + c->cmd_len) / c->cols;
        c->col = (5 + c->cmd_len) % c->cols;
        sys_output = c;
        term_newline(c);
        run_command(c, command);
        term_prompt(c);
        return;
    }
    if (key == 0x15) { /* Ctrl-U */
        c->cmd_len = 0;
        c->cmd_pos = 0;
        c->cmd_buf[0] = 0;
        c->hist_pos = -1;
        cmd_render(c);
        return;
    }
    if (key == 0x0C) { /* Ctrl-L */
        char save[128];
        int slen = c->cmd_len, spos = c->cmd_pos;
        for (int i = 0; i <= slen; i++)
            save[i] = c->cmd_buf[i];
        term_clear(c);
        term_prompt(c);
        for (int i = 0; i <= slen; i++)
            c->cmd_buf[i] = save[i];
        c->cmd_len = slen;
        c->cmd_pos = spos;
        cmd_render(c);
        return;
    }
    if (key == GUI_KEY_UP) {
        int p = c->hist_pos < 0 ? 0 : c->hist_pos + 1;
        if (p < c->hist_n)
            hist_load(c, p);
        return;
    }
    if (key == GUI_KEY_DOWN) {
        int p = c->hist_pos - 1;
        if (c->hist_pos < 0)
            return;
        hist_load(c, p);
        return;
    }
    if (key == '\b') {
        if (c->cmd_pos > 0) {
            for (int i = c->cmd_pos - 1; i < c->cmd_len - 1; i++)
                c->cmd_buf[i] = c->cmd_buf[i + 1];
            c->cmd_len--;
            c->cmd_pos--;
            c->cmd_buf[c->cmd_len] = 0;
            cmd_render(c);
        }
        return;
    }
    if (key == GUI_KEY_LEFT) {
        if (c->cmd_pos > 0) {
            c->cmd_pos--;
            cmd_render(c);
        }
        return;
    }
    if (key == GUI_KEY_RIGHT) {
        if (c->cmd_pos < c->cmd_len) {
            c->cmd_pos++;
            cmd_render(c);
        }
        return;
    }
    if (key == GUI_KEY_HOME) {
        c->cmd_pos = 0;
        cmd_render(c);
        return;
    }
    if (key == GUI_KEY_END) {
        c->cmd_pos = c->cmd_len;
        cmd_render(c);
        return;
    }
    if (key == GUI_KEY_DEL) {
        if (c->cmd_pos < c->cmd_len) {
            for (int i = c->cmd_pos; i < c->cmd_len - 1; i++)
                c->cmd_buf[i] = c->cmd_buf[i + 1];
            c->cmd_len--;
            c->cmd_buf[c->cmd_len] = 0;
            cmd_render(c);
        }
        return;
    }
    if (ch >= 32 && ch < 127 && c->cmd_len < 127) {
        for (int i = c->cmd_len; i > c->cmd_pos; i--)
            c->cmd_buf[i] = c->cmd_buf[i - 1];
        c->cmd_buf[c->cmd_pos] = ch;
        c->cmd_len++;
        c->cmd_pos++;
        c->cmd_buf[c->cmd_len] = 0;
        cmd_render(c);
    }
}

static struct console *active_console(void) {
    if (sys_output)
        return sys_output;
    if (focus_win >= WIN_C0 && focus_win <= WIN_C2)
        return &consoles[focus_win];
    return &consoles[WIN_C0];
}

/* -----------------------------------------------------------------------------
 * Window drawing
 * ----------------------------------------------------------------------------- */
static void taskbar_button(int x, int y, int w, const char *label, int hit, int active) {
    if (is_pressed(hit))
        sunken_box(x, y, w, 10);
    else
        raise_box_col(x, y, w, 10, active ? 11 : 7);
    text(x + 4, y + 1, label, 0);
}

static void draw_desktop(void) {
    fill_rect(0, 0, scr_w, TASKBAR_Y, 3);
    if (scr_w >= 640) {
        text(8, 5, "MiniOS 2.0 - Live MicroPython Desktop", 15);
        text(scr_w - 232, 5, "640x480 VBE | F4 Windows", 11);
    } else {
        text(8, 5, "MiniOS 2.0", 15);
        text(188, 5, "F4 windows", 11);
    }
    fill_rect(0, 17, scr_w, 1, 2);
}

static void draw_taskbar(void) {
    int ty = TASKBAR_Y;
    fill_rect(0, ty, scr_w, 14, 7);
    fill_rect(0, ty, scr_w, 1, 15);
    fill_rect(0, ty + 1, scr_w, 1, 8);

    taskbar_button(2, ty + 2, 44, "Start", HIT_START, menu_open);
    taskbar_button(48, ty + 2, 50, "Con1", HIT_TB_C1, focus_win == WIN_C0);
    taskbar_button(100, ty + 2, 50, "Con2", HIT_TB_C2, focus_win == WIN_C1);
    taskbar_button(152, ty + 2, 46, "Sys", HIT_TB_SYS, focus_win == WIN_SYS);
    taskbar_button(200, ty + 2, 56, "Wins", HIT_TB_WIN, list_open);
    if (scr_w >= 640) {
        taskbar_button(258, ty + 2, 50, "Con3", HIT_TB_C3, focus_win == WIN_C2);
        taskbar_button(310, ty + 2, 58, "Canvas", HIT_TB_APP, focus_win == WIN_APP);
    }

    int cx = scr_w - 56;
    fill_rect(cx, ty + 2, 52, 10, 0);
    fill_rect(cx + 1, ty + 3, 50, 8, 15);
    text(cx + 10, ty + 3, clock_hm, 0);
}

static void window_frame(int id) {
    struct window *w = &wins[id];
    int active = (focus_win == id);
    fill_rect(w->x + 3, w->y + 3, w->w, w->h, 8);
    fill_rect(w->x, w->y, w->w, w->h, 0);
    fill_rect(w->x + 1, w->y + 1, w->w - 2, w->h - 2, 7);
    fill_rect(w->x + 1, w->y + 1, w->w - 2, 10, active ? 1 : 8);
    text(w->x + 4, w->y + 2, w->title, 15);

    int max_hit = HIT_MAX_BASE + id;
    if (is_pressed(max_hit))
        sunken_box(w->x + w->w - 42, w->y + 2, 12, 8);
    else
        raise_box_col(w->x + w->w - 42, w->y + 2, 12, 8, 7);
    draw_char(w->x + w->w - 39, w->y + 2, w->maximized ? 'o' : 'M', 0, 1);

    int min_hit = HIT_MIN_BASE + id;
    if (is_pressed(min_hit))
        sunken_box(w->x + w->w - 28, w->y + 2, 12, 8);
    else
        raise_box_col(w->x + w->w - 28, w->y + 2, 12, 8, 7);
    draw_char(w->x + w->w - 26, w->y + 3, '_', 0, 1);

    int close_hit = HIT_CLOSE_BASE + id;
    if (is_pressed(close_hit))
        sunken_box(w->x + w->w - 14, w->y + 2, 12, 8);
    else
        raise_box_col(w->x + w->w - 14, w->y + 2, 12, 8, 7);
    draw_char(w->x + w->w - 12, w->y + 2, 'x', 0, 1);
    fill_rect(w->x + 1, w->y + 11, w->w - 2, w->h - 12, 15);
    for (int i = 0; i < 4; i++)
        fill_rect(w->x + w->w - 3 - i, w->y + w->h - 3 - i, 2, 2, 8);
}

static void draw_console(int id) {
    struct console *c = &consoles[id];
    window_frame(id);
    int start = c->n > c->rows ? c->n - c->rows - c->scroll : 0;
    for (int r = 0; r < c->rows; r++) {
        int idx = start + r;
        if (idx >= 0 && idx < c->n)
            text(c->x + 4, c->y + 14 + r * 9, c->term[idx], 0);
    }
    scrollbar_draw(c);
    if (focus_win == id && c->scroll == 0 && ((ticks / 50) & 1)) {
        int row = c->cur - start;
        if (row >= 0 && row < c->rows)
            fill_rect(c->x + 4 + c->col * 8, c->y + 14 + row * 9, 1, 8, 0);
    }
}

static void draw_system(void) {
    struct window *w = &wins[WIN_SYS];
    window_frame(WIN_SYS);
    int x = w->x, y = w->y;
    text(x + 6, y + 14, "MiniOS 2.0 - System Info", 1);
    text(x + 6, y + 23, "CPU:", 0);
    text(x + 46, y + 23, cpu, 0);
    text(x + 6, y + 32, "Heap:", 0);
    char tmp[16];
    int n = 0;
    u32 v = heap_used;
    if (!v)
        tmp[n++] = '0';
    while (v) {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    int p = 0;
    while (n)
        tmp[p++] = tmp[--n];
    tmp[p] = 0;
    text(x + 46, y + 32, tmp, 0);
    text(x + 6, y + 41,
         vbe_active ? "Video: VBE 640x480x256 LFB" : "Video: VGA 320x200x256",
         0);
    text(x + 6, y + 50, "Boot: 1.44MB floppy", 0);
    text(x + 6, y + 59, "Mode: 32-bit Ring0+Ring3", 0);
    text(x + 6, y + 68, "Uptime:", 0);
    text(x + 70, y + 68, uptime_str, 0);
    text(x + 6, y + 77, "Date:", 0);
    text(x + 54, y + 77, date_str, 0);
}

static void draw_canvas(void) {
    struct window *w = &wins[WIN_APP];
    window_frame(WIN_APP);
    int cx = w->x + 2;
    int cy = w->y + 12;
    if (!canvas_buf) {
        fill_rect(cx, cy, canvas_w, canvas_h, 0);
        return;
    }
    for (int y = 0; y < canvas_h; y++) {
        int py = cy + y;
        if ((unsigned)py >= (unsigned)scr_h)
            continue;
        for (int x = 0; x < canvas_w; x++) {
            int px = cx + x;
            if ((unsigned)px < (unsigned)scr_w)
                fb[py * scr_w + px] = canvas_buf[y * CANVAS_MAX_W + x];
        }
    }
}

static void draw_about(void) {
    struct window *w = &wins[WIN_ABOUT];
    window_frame(WIN_ABOUT);
    int x = w->x, y = w->y;
    text2(x + 56, y + 16, "MiniOS", 1);
    text(x + 12, y + 42, "C + MicroPython in 1.44MB", 0);
    text(x + 28, y + 53, "32-bit Ring 3 desktop", 0);
    text(x + 12, y + 64, "(c) 2026 MiniOS Project", 8);
    if (is_pressed(HIT_OK))
        sunken_box(x + 84, y + 80, 40, 14);
    else
        raise_box_col(x + 84, y + 80, 40, 14, 7);
    text(x + 96, y + 83, "OK", 0);
}

static void draw_menu(void) {
    if (!menu_open)
        return;
    fill_rect(MENU_X + 3, MENU_Y + 3, MENU_W, MENU_H, 8);
    fill_rect(MENU_X, MENU_Y, MENU_W, MENU_H, 0);
    fill_rect(MENU_X + 1, MENU_Y + 1, MENU_W - 2, MENU_H - 2, 7);

    const char *items[MENU_ITEMS] = {
        "Console 1", "Console 2", "Console 3", "System info",
        "Windows", "About", "Reboot"
    };
    for (int i = 0; i < MENU_ITEMS; i++) {
        int iy = MENU_Y + 2 + i * MENU_ITEM_H;
        int hov = inside(mouse_x, mouse_y, MENU_X + 2, iy, MENU_W - 4, MENU_ITEM_H);
        int hit = HIT_MENU_BASE + i;
        if (is_pressed(hit)) {
            fill_rect(MENU_X + 2, iy, MENU_W - 4, MENU_ITEM_H, 2);
            text(MENU_X + 8, iy + 2, items[i], 15);
        } else if (hov) {
            fill_rect(MENU_X + 2, iy, MENU_W - 4, MENU_ITEM_H, 1);
            text(MENU_X + 8, iy + 2, items[i], 15);
        } else {
            text(MENU_X + 8, iy + 2, items[i], 0);
        }
    }
}

static void draw_list(void) {
    if (!list_open)
        return;
    fill_rect(LIST_X + 3, LIST_Y + 3, LIST_W, LIST_H, 8);
    fill_rect(LIST_X, LIST_Y, LIST_W, LIST_H, 0);
    fill_rect(LIST_X + 1, LIST_Y + 1, LIST_W - 2, LIST_H - 2, 7);
    fill_rect(LIST_X + 1, LIST_Y + 1, LIST_W - 2, 10, 1);
    text(LIST_X + 4, LIST_Y + 2, "Windows (F4)", 15);

    for (int i = 0; i < WIN_COUNT; i++) {
        int iy = LIST_Y + 14 + i * 16;
        int hit = HIT_LIST_BASE + i;
        int selected = (list_sel == i);
        if (is_pressed(hit) || selected) {
            fill_rect(LIST_X + 3, iy, LIST_W - 6, 14, selected ? 1 : 2);
            text(LIST_X + 8, iy + 3, wins[i].title, 15);
        } else {
            text(LIST_X + 8, iy + 3, wins[i].title, 0);
        }
        if (focus_win == i)
            text(LIST_X + LIST_W - 16, iy + 3, "*", 2);
        else if (wins[i].minimized)
            text(LIST_X + LIST_W - 16, iy + 3, "m", 8);
        else if (!wins[i].visible)
            text(LIST_X + LIST_W - 16, iy + 3, "-", 8);
    }
    text(LIST_X + 6, LIST_Y + LIST_H - 12, "Tab/Enter/Esc", 8);
}

static void cursor_capture(void) {
    for (int r = 0; r < 18; r++) {
        for (int c = 0; c < 12; c++) {
            int px = mouse_x + c;
            int py = mouse_y + r;
            cursor_bg[r][c] = ((unsigned)px < (unsigned)scr_w &&
                               (unsigned)py < (unsigned)scr_h)
                                  ? fb[py * scr_w + px] : 0;
        }
    }
    cursor_px = mouse_x;
    cursor_py = mouse_y;
    cursor_bg_valid = 1;
}

static void cursor_set(int r, int c, u8 col) {
    int px = mouse_x + c;
    int py = mouse_y + r;
    if ((unsigned)px < (unsigned)scr_w && (unsigned)py < (unsigned)scr_h)
        fb[py * scr_w + px] = col;
}

static void cursor_draw_art(void) {
    if (cursor_shape == 0) {
        for (int r = 0; r < 18; r++) {
            int py = mouse_y + r;
            if ((unsigned)py >= (unsigned)scr_h)
                continue;
            for (int c = 0; c < 12; c++) {
                int px = mouse_x + c;
                if ((unsigned)px >= (unsigned)scr_w)
                    continue;
                char ch = cursor_art[r][c];
                if (ch == '.')
                    continue;
                fb[py * scr_w + px] = (ch == 'X') ? 0 : 15;
            }
        }
        return;
    }
    if (cursor_shape == 1 || cursor_shape == 2) {
        for (int r = 3; r < 15; r++)
            cursor_set(r, 6, 15);
        for (int i = 0; i < 4; i++) {
            cursor_set(3 + i, 6 - i, 15);
            cursor_set(3 + i, 6 + i, 15);
            cursor_set(14 - i, 6 - i, 15);
            cursor_set(14 - i, 6 + i, 15);
        }
    } else if (cursor_shape == 3 || cursor_shape == 4) {
        for (int c = 2; c < 10; c++)
            cursor_set(9, c, 15);
        for (int i = 0; i < 4; i++) {
            cursor_set(9 - i, 2 + i, 15);
            cursor_set(9 + i, 2 + i, 15);
            cursor_set(9 - i, 9 - i, 15);
            cursor_set(9 + i, 9 - i, 15);
        }
    } else {
        for (int i = 0; i < 10; i++) {
            cursor_set(3 + i, 2 + i, 15);
            if (cursor_shape == 6 || cursor_shape == 7)
                cursor_set(12 - i, 2 + i, 15);
        }
    }
}

static void cursor_restore(void) {
    if (!cursor_bg_valid)
        return;
    for (int r = 0; r < 18; r++) {
        int py = cursor_py + r;
        if ((unsigned)py >= (unsigned)scr_h)
            continue;
        for (int c = 0; c < 12; c++) {
            int px = cursor_px + c;
            if ((unsigned)px >= (unsigned)scr_w)
                continue;
            fb[py * scr_w + px] = cursor_bg[r][c];
        }
    }
    blit_rect(cursor_px, cursor_py, 12, 18);
    cursor_bg_valid = 0;
}

static const char *ctx_label(int i) {
    if (ctx_target >= 0) {
        static const char *items[5] = {
            "Scroll top", "Scroll bottom", "Clear", "Minimize", "Close"
        };
        return (i >= 0 && i < 5) ? items[i] : "";
    }
    static const char *items[6] = {
        "Console 1", "Console 2", "Console 3", "Windows", "System", "About"
    };
    return (i >= 0 && i < 6) ? items[i] : "";
}

static int ctx_height(void) {
    return ctx_count * 12 + 4;
}

static void open_context(int x, int y) {
    ctx_target = -1;
    for (int i = WIN_COUNT - 1; i >= 0; i--) {
        int id = zorder[i];
        if (id <= WIN_C2 && wins[id].visible &&
            inside(x, y, wins[id].x, wins[id].y, wins[id].w, wins[id].h)) {
            ctx_target = id;
            break;
        }
    }
    ctx_count = ctx_target >= 0 ? 5 : 6;
    ctx_x = x + 2;
    ctx_y = y + 2;
    if (ctx_x + 128 > scr_w) ctx_x = scr_w - 128;
    if (ctx_y + ctx_height() > TASKBAR_Y - 2)
        ctx_y = TASKBAR_Y - 2 - ctx_height();
    if (ctx_x < 0) ctx_x = 0;
    if (ctx_y < 0) ctx_y = 0;
    ctx_open = 1;
    menu_open = 0;
    list_open = 0;
}

static void activate_context(int idx) {
    int target = ctx_target;
    ctx_open = 0;
    if (target >= 0) {
        struct console *c = &consoles[target];
        if (idx == 0) term_scroll_by(c, TERM_ROWS);
        else if (idx == 1) term_scroll_by(c, -TERM_ROWS);
        else if (idx == 2) { term_clear(c); term_prompt(c); }
        else if (idx == 3) minimize_window(target);
        else if (idx == 4) hide_window(target);
        return;
    }
    if (idx >= 0 && idx <= 2) focus_window(idx);
    else if (idx == 3) { list_open = 1; list_sel = focus_win >= 0 ? focus_win : 0; }
    else if (idx == 4) focus_window(WIN_SYS);
    else if (idx == 5) focus_window(WIN_ABOUT);
}

static void draw_context(void) {
    if (!ctx_open)
        return;
    int w = 128;
    int h = ctx_height();
    fill_rect(ctx_x + 3, ctx_y + 3, w, h, 8);
    fill_rect(ctx_x, ctx_y, w, h, 0);
    fill_rect(ctx_x + 1, ctx_y + 1, w - 2, h - 2, 7);
    for (int i = 0; i < ctx_count; i++) {
        int iy = ctx_y + 2 + i * 12;
        int hit = HIT_CTX_BASE + i;
        if (is_pressed(hit)) {
            fill_rect(ctx_x + 2, iy, w - 4, 12, 2);
            text(ctx_x + 8, iy + 2, ctx_label(i), 15);
        } else if (inside(mouse_x, mouse_y, ctx_x + 2, iy, w - 4, 12)) {
            fill_rect(ctx_x + 2, iy, w - 4, 12, 1);
            text(ctx_x + 8, iy + 2, ctx_label(i), 15);
        } else {
            text(ctx_x + 8, iy + 2, ctx_label(i), 0);
        }
    }
}

static void canvas_pixel(int x, int y, u8 color) {
    if (canvas_buf && (unsigned)x < (unsigned)canvas_w &&
        (unsigned)y < (unsigned)canvas_h)
        canvas_buf[y * CANVAS_MAX_W + x] = color;
}

int gui_canvas_w(void) {
    return canvas_w;
}

int gui_canvas_h(void) {
    return canvas_h;
}

static void canvas_rect(int x, int y, int w, int h, u8 color) {
    if (w <= 0 || h <= 0)
        return;
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++)
            canvas_pixel(xx, yy, color);
}

static void canvas_line(int x0, int y0, int x1, int y1, u8 color) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        canvas_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = err * 2;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gui_canvas_open(const char *title) {
    extern void *kernel_alloc_pages_zero(u32 pages);
    if (!canvas_buf) {
        u32 pages = ((u32)(CANVAS_MAX_W * CANVAS_MAX_H) + 4095u) / 4096u;
        canvas_buf = (u8 *)kernel_alloc_pages_zero(pages);
    }
    wins[WIN_APP].w = canvas_w + 4;
    wins[WIN_APP].h = canvas_h + 14;
    int i = 0;
    if (title) {
        while (i < 31 && title[i]) {
            canvas_title[i] = title[i];
            i++;
        }
    }
    canvas_title[i] = 0;
    wins[WIN_APP].title = canvas_title;
    canvas_open = 1;
    canvas_ev.closed = 0;
    focus_window(WIN_APP);
    gui_dirty = 1;
}

void gui_canvas_close(void) {
    canvas_open = 0;
    canvas_ev.closed = 1;
    canvas_ev_pending = 1;
    hide_window(WIN_APP);
    gui_dirty = 1;
}

int gui_canvas_cmd(const struct gui_canvas_cmd *cmd) {
    if (!cmd || !canvas_buf)
        return -1;
    if (cmd->op == 0) {
        for (int i = 0; i < canvas_w * canvas_h; i++)
            canvas_buf[i] = (u8)cmd->color;
    } else if (cmd->op == 1) {
        canvas_pixel((int)cmd->x, (int)cmd->y, (u8)cmd->color);
    } else if (cmd->op == 2) {
        canvas_rect((int)cmd->x, (int)cmd->y, (int)cmd->w, (int)cmd->h,
                    (u8)cmd->color);
    } else if (cmd->op == 3) {
        canvas_line((int)cmd->x, (int)cmd->y, (int)cmd->w, (int)cmd->h,
                    (u8)cmd->color);
    } else if (cmd->op == 4) {
        if (!cmd->text || cmd->len > 256)
            return -1;
        int px = (int)cmd->x;
        int py = (int)cmd->y;
        for (u32 i = 0; i < cmd->len; i++) {
            const u8 *glyph = FONT + (u8)cmd->text[i] * 8;
            for (int r = 0; r < 8; r++)
                for (int c = 0; c < 8; c++)
                    if (glyph[r] & (0x80u >> c))
                        canvas_pixel(px + c, py + r, (u8)cmd->color);
            px += 8;
        }
    } else {
        return -1;
    }
    gui_dirty = 1;
    return 0;
}

int gui_canvas_event(struct gui_canvas_event *out) {
    if (!canvas_ev_pending || !out)
        return 0;
    *out = canvas_ev;
    canvas_ev_pending = 0;
    return 1;
}

static void draw_cursor(void) {
    cursor_capture();
    cursor_draw_art();
}

int gui_cursor_only_motion(int x, int y, int buttons) {
    if ((buttons & 7) || (last_buttons & 7) ||
        menu_open || list_open || ctx_open ||
        drag_win >= 0 || resize_win >= 0 || scroll_drag >= 0)
        return 0;
    int shape = 0;
    int hit = hit_test(x, y);
    if (hit >= HIT_RESIZE_BASE && hit < HIT_RESIZE_BASE + WIN_COUNT * 9) {
        int d = (hit - HIT_RESIZE_BASE) % 9;
        if (d == 1 || d == 2) shape = 1;
        else if (d == 3 || d == 4) shape = 2;
        else if (d == 5 || d == 6) shape = 3;
        else if (d == 7 || d == 8) shape = 4;
    }
    if (x == mouse_x && y == mouse_y && shape == cursor_shape)
        return 1;

    cursor_restore();
    cursor_shape = shape;
    mouse_x = x;
    mouse_y = y;
    draw_cursor();
    blit_rect(mouse_x, mouse_y, 12, 18);
    return 1;
}

void gui_alloc_fb(void) {
    extern void *kernel_alloc_pages_zero(u32 pages);
    if (!fb) {
        u32 pages = ((u32)(scr_w * scr_h) + 4095u) / 4096u;
        fb = (u8 *)kernel_alloc_pages_zero(pages);
    }
}

void gui_redraw(void) {
    static u8 redraw_busy;
    if (redraw_busy || !fb)
        return;
    redraw_busy = 1;
    gui_dirty = 0;
    update_clock(0);
    make_uptime();
    draw_desktop();
    draw_taskbar();
    for (int i = 0; i < WIN_COUNT; i++) {
        int id = zorder[i];
        if (!wins[id].visible)
            continue;
        if (id >= WIN_C0 && id <= WIN_C2)
            draw_console(id);
        else if (id == WIN_SYS)
            draw_system();
        else if (id == WIN_ABOUT)
            draw_about();
        else if (id == WIN_APP)
            draw_canvas();
    }
    draw_menu();
    draw_list();
    draw_context();
    draw_cursor();
    blit();
    redraw_busy = 0;
}

int gui_dirty_pending(void) {
    return gui_dirty;
}

void gui_mark_dirty(void) {
    gui_dirty = 1;
}

void gui_flush(void) {
    if (gui_dirty)
        gui_redraw();
}

/* -----------------------------------------------------------------------------
 * Hit testing / focus / actions
 * ----------------------------------------------------------------------------- */
static int inside(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static int hit_test(int x, int y) {
    if (ctx_open) {
        for (int i = 0; i < ctx_count; i++) {
            int iy = ctx_y + 2 + i * 12;
            if (inside(x, y, ctx_x + 2, iy, 124, 12))
                return HIT_CTX_BASE + i;
        }
        return HIT_NONE;
    }
    if (list_open) {
        for (int i = 0; i < WIN_COUNT; i++) {
            int iy = LIST_Y + 14 + i * 16;
            if (inside(x, y, LIST_X + 3, iy, LIST_W - 6, 14))
                return HIT_LIST_BASE + i;
        }
        return HIT_NONE;
    }
    if (menu_open) {
        for (int i = 0; i < MENU_ITEMS; i++) {
            int iy = MENU_Y + 2 + i * MENU_ITEM_H;
            if (inside(x, y, MENU_X + 2, iy, MENU_W - 4, MENU_ITEM_H))
                return HIT_MENU_BASE + i;
        }
        return HIT_NONE;
    }

    if (y >= TASKBAR_Y) {
        if (inside(x, y, 2, TASKBAR_Y, 44, 14)) return HIT_START;
        if (inside(x, y, 48, TASKBAR_Y, 50, 14)) return HIT_TB_C1;
        if (inside(x, y, 100, TASKBAR_Y, 50, 14)) return HIT_TB_C2;
        if (inside(x, y, 152, TASKBAR_Y, 46, 14)) return HIT_TB_SYS;
        if (inside(x, y, 200, TASKBAR_Y, 56, 14)) return HIT_TB_WIN;
        if (scr_w >= 640) {
            if (inside(x, y, 258, TASKBAR_Y, 50, 14)) return HIT_TB_C3;
            if (inside(x, y, 310, TASKBAR_Y, 58, 14)) return HIT_TB_APP;
        }
        return HIT_NONE;
    }

    for (int i = WIN_COUNT - 1; i >= 0; i--) {
        int id = zorder[i];
        struct window *w = &wins[id];
        if (!w->visible || !inside(x, y, w->x, w->y, w->w, w->h))
            continue;
        if (inside(x, y, w->x + w->w - 46, w->y + 1, 13, 10))
            return HIT_MAX_BASE + id;
        if (inside(x, y, w->x + w->w - 32, w->y + 1, 13, 10))
            return HIT_MIN_BASE + id;
        if (inside(x, y, w->x + w->w - 20, w->y, 20, 12))
            return HIT_CLOSE_BASE + id;
        if (id == WIN_ABOUT && inside(x, y, w->x + 80, w->y + 77, 48, 19))
            return HIT_OK;
        if (!w->maximized) {
            int edge = 4;
            int n = (y >= w->y - edge && y <= w->y + edge);
            int s = (y >= w->y + w->h - edge && y <= w->y + w->h + edge);
            int l = (x >= w->x - edge && x <= w->x + edge);
            int r = (x >= w->x + w->w - edge && x <= w->x + w->w + edge);
            if ((n || s || l || r) &&
                x >= w->x - edge && x <= w->x + w->w + edge &&
                y >= w->y - edge && y <= w->y + w->h + edge) {
                int d = 0;
                if (n) d = 1;
                else if (s) d = 2;
                else if (r) d = 3;
                else d = 4;
                if (n && r) d = 5;
                else if (n && l) d = 6;
                else if (s && r) d = 7;
                else if (s && l) d = 8;
                return HIT_RESIZE_BASE + id * 9 + d;
            }
        }
        if (inside(x, y, w->x + 1, w->y + 1, w->w - 2, 10))
            return HIT_TITLE_BASE + id;
        if (id >= WIN_C0 && id <= WIN_C2 &&
            scrollbar_hit(&consoles[id], x, y))
            return HIT_SCROLL_BASE + id;
        return HIT_BODY_BASE + id;
    }
    return HIT_NONE;
}

void gui_toggle_maximize(int id) {
    struct window *w = &wins[id];
    if (!w->maximized) {
        w->saved_x = w->x;
        w->saved_y = w->y;
        w->saved_w = w->w;
        w->saved_h = w->h;
        int top = 18;
        int tby = scr_h - 14;
        w->x = 0;
        w->y = top;
        w->w = scr_w;
        w->h = tby - top;
        w->maximized = 1;
    } else {
        w->x = w->saved_x;
        w->y = w->saved_y;
        w->w = w->saved_w;
        w->h = w->saved_h;
        w->maximized = 0;
    }
    if (id >= WIN_C0 && id <= WIN_C2) {
        consoles[id].x = w->x;
        consoles[id].y = w->y;
        consoles[id].w = w->w;
        consoles[id].h = w->h;
    }
    focus_window(id);
    gui_dirty = 1;
}

static void start_drag(int id, int x, int y) {
    drag_win = id;
    drag_dx = x - wins[id].x;
    drag_dy = y - wins[id].y;
}

static void drag_move(int x, int y) {
    if (drag_win < 0)
        return;
    struct window *w = &wins[drag_win];
    int nx = x - drag_dx;
    int ny = y - drag_dy;
    if (nx < 0) nx = 0;
    if (ny < 0) ny = 0;
    if (nx > scr_w - w->w) nx = scr_w - w->w;
    if (ny > TASKBAR_Y - w->h) ny = TASKBAR_Y - w->h;
    w->x = nx;
    w->y = ny;
    if (drag_win >= WIN_C0 && drag_win <= WIN_C2) {
        consoles[drag_win].x = nx;
        consoles[drag_win].y = ny;
    }
}

static void resize_move(int x, int y) {
    if (resize_win < 0)
        return;
    struct window *w = &wins[resize_win];
    int left = resize_sx;
    int top = resize_sy;
    int right = resize_sx + resize_sw;
    int bottom = resize_sy + resize_sh;
    int d = resize_dir;
    int west = (d == 4 || d == 6 || d == 8);
    int east = (d == 3 || d == 5 || d == 7);
    int north = (d == 1 || d == 5 || d == 6);
    int south = (d == 2 || d == 7 || d == 8);
    if (west) left = x;
    if (east) right = x;
    if (north) top = y;
    if (south) bottom = y;
    int minw = (resize_win <= WIN_C2) ? 208 : 160;
    int minh = (resize_win <= WIN_C2) ? 96 : (resize_win == WIN_APP ? 110 : 90);
    if (right - left < minw) {
        if (west) left = right - minw;
        else right = left + minw;
    }
    if (bottom - top < minh) {
        if (north) top = bottom - minh;
        else bottom = top + minh;
    }
    if (left < 0) left = 0;
    if (top < 18) top = 18;
    if (right > scr_w) right = scr_w;
    if (bottom > scr_h - 14) bottom = scr_h - 14;
    w->x = left;
    w->y = top;
    w->w = right - left;
    w->h = bottom - top;
    if (resize_win >= WIN_C0 && resize_win <= WIN_C2) {
        struct console *c = &consoles[resize_win];
        c->x = w->x;
        c->y = w->y;
        c->w = w->w;
        c->h = w->h;
        int cols = (w->w - 16) / 8;
        int rows = (w->h - 18) / 9;
        if (cols < 24) cols = 24;
        if (cols > 76) cols = 76;
        if (rows < 6) rows = 6;
        if (rows > 46) rows = 46;
        c->cols = cols;
        c->rows = rows;
    } else if (resize_win == WIN_APP) {
        canvas_w = w->w - 4;
        canvas_h = w->h - 14;
        if (canvas_w < 160) canvas_w = 160;
        if (canvas_w > CANVAS_MAX_W) canvas_w = CANVAS_MAX_W;
        if (canvas_h < 110) canvas_h = 110;
        if (canvas_h > CANVAS_MAX_H) canvas_h = CANVAS_MAX_H;
        w->w = canvas_w + 4;
        w->h = canvas_h + 14;
    }
    gui_dirty = 1;
}

static void activate_hit(int hit) {
    if (hit == HIT_START) {
        menu_open = !menu_open;
        list_open = 0;
        return;
    }
    if (hit == HIT_TB_C1) { focus_window(WIN_C0); return; }
    if (hit == HIT_TB_C2) { focus_window(WIN_C1); return; }
    if (hit == HIT_TB_C3) { focus_window(WIN_C2); return; }
    if (hit == HIT_TB_APP) { focus_window(WIN_APP); return; }
    if (hit == HIT_TB_SYS) { focus_window(WIN_SYS); return; }
    if (hit == HIT_TB_WIN) {
        list_open = !list_open;
        list_sel = focus_win >= 0 ? focus_win : 0;
        menu_open = 0;
        return;
    }
    if (hit >= HIT_MAX_BASE && hit < HIT_MAX_BASE + WIN_COUNT) {
        extern void gui_toggle_maximize(int id);
        gui_toggle_maximize(hit - HIT_MAX_BASE);
        return;
    }
    if (hit >= HIT_MIN_BASE && hit < HIT_MIN_BASE + WIN_COUNT) {
        minimize_window(hit - HIT_MIN_BASE);
        return;
    }
    if (hit >= HIT_CLOSE_BASE && hit < HIT_CLOSE_BASE + WIN_COUNT) {
        int id = hit - HIT_CLOSE_BASE;
        if (id == WIN_APP) {
            canvas_open = 0;
            canvas_ev.closed = 1;
            canvas_ev_pending = 1;
        }
        hide_window(id);
        return;
    }
    if (hit == HIT_OK) {
        hide_window(WIN_ABOUT);
        focus_window(WIN_C0);
        return;
    }
    if (hit >= HIT_LIST_BASE && hit < HIT_LIST_BASE + WIN_COUNT) {
        int id = hit - HIT_LIST_BASE;
        list_open = 0;
        focus_window(id);
        return;
    }
    if (hit >= HIT_MENU_BASE && hit < HIT_MENU_BASE + MENU_ITEMS) {
        int idx = hit - HIT_MENU_BASE;
        menu_open = 0;
        if (idx < 3) {
            focus_window(WIN_C0 + idx);
        } else if (idx == 3) {
            focus_window(WIN_SYS);
        } else if (idx == 4) {
            list_open = 1;
            list_sel = focus_win >= 0 ? focus_win : 0;
        } else if (idx == 5) {
            focus_window(WIN_ABOUT);
        } else {
            machine_reboot();
        }
    }
}

void gui_mouse_wheel(int dz) {
    if (list_open) {
        list_sel -= dz;
        if (list_sel < 0) list_sel = 0;
        if (list_sel >= WIN_COUNT) list_sel = WIN_COUNT - 1;
        gui_dirty = 1;
        return;
    }
    if (focus_win >= WIN_C0 && focus_win <= WIN_C2) {
        term_scroll_by(&consoles[focus_win], dz * 3);
        gui_dirty = 1;
    }
}

void gui_mouse_event(int x, int y, int buttons) {
    int press = (buttons & 1) && !(last_buttons & 1);
    int release = !(buttons & 1) && (last_buttons & 1);
    int rpress = (buttons & 2) && !(last_buttons & 2);
    int hit = hit_test(x, y);

    if (ctx_open) {
        if (rpress) {
            ctx_open = 0;
            last_buttons = buttons;
            return;
        }
        if (press) {
            if (hit >= HIT_CTX_BASE && hit < HIT_CTX_BASE + ctx_count)
                pressed_hit = hit;
            else
                ctx_open = 0;
        }
        if (release) {
            if (pressed_hit >= HIT_CTX_BASE &&
                pressed_hit < HIT_CTX_BASE + ctx_count &&
                hit == pressed_hit)
                activate_context(pressed_hit - HIT_CTX_BASE);
            pressed_hit = HIT_NONE;
        }
        last_buttons = buttons;
        return;
    }

    if (rpress) {
        open_context(x, y);
        last_buttons = buttons;
        return;
    }

    if (list_open) {
        if (!(buttons & 1) && hit >= HIT_LIST_BASE && hit < HIT_LIST_BASE + WIN_COUNT)
            list_sel = hit - HIT_LIST_BASE;
        if (press) {
            if (hit >= HIT_LIST_BASE && hit < HIT_LIST_BASE + WIN_COUNT) {
                list_sel = hit - HIT_LIST_BASE;
                pressed_hit = hit;
            } else {
                list_open = 0;
                pressed_hit = HIT_NONE;
            }
        }
        if (release) {
            if (pressed_hit != HIT_NONE && hit == pressed_hit)
                activate_hit(pressed_hit);
            pressed_hit = HIT_NONE;
        }
        last_buttons = buttons;
        return;
    }

    if (menu_open) {
        if (press) {
            if (hit >= HIT_MENU_BASE && hit < HIT_MENU_BASE + MENU_ITEMS)
                pressed_hit = hit;
            else {
                menu_open = 0;
                pressed_hit = HIT_NONE;
            }
        }
        if (release) {
            if (pressed_hit != HIT_NONE && hit == pressed_hit)
                activate_hit(pressed_hit);
            pressed_hit = HIT_NONE;
        }
        last_buttons = buttons;
        return;
    }

    if (wins[WIN_APP].visible &&
        inside(x, y, wins[WIN_APP].x + 2, wins[WIN_APP].y + 12,
               canvas_w, canvas_h)) {
        canvas_ev.x = x - (wins[WIN_APP].x + 2);
        canvas_ev.y = y - (wins[WIN_APP].y + 12);
        canvas_ev.buttons = (u32)buttons;
        canvas_ev_pending = 1;
    }

    if (press) {
        pressed_hit = hit;
        if (hit >= HIT_SCROLL_BASE && hit < HIT_SCROLL_BASE + WIN_COUNT) {
            int id = hit - HIT_SCROLL_BASE;
            focus_window(id);
            scroll_drag = id;
            scrollbar_set_from_y(&consoles[id], y);
        } else if (hit >= HIT_RESIZE_BASE && hit < HIT_RESIZE_BASE + WIN_COUNT * 9) {
            int off = hit - HIT_RESIZE_BASE;
            int id = off / 9;
            int d = off % 9;
            focus_window(id);
            resize_win = id;
            resize_dir = d;
            resize_sx = wins[id].x;
            resize_sy = wins[id].y;
            resize_sw = wins[id].w;
            resize_sh = wins[id].h;
        } else if (hit >= HIT_TITLE_BASE && hit < HIT_TITLE_BASE + WIN_COUNT) {
            static u32 last_title_tick;
            static int last_title_id = -1;
            int id = hit - HIT_TITLE_BASE;
            if (last_title_id == id && (u32)(ticks - last_title_tick) < 20) {
                last_title_id = -1;
                gui_toggle_maximize(id);
            } else {
                last_title_id = id;
                last_title_tick = ticks;
                focus_window(id);
                start_drag(id, x, y);
            }
        } else if (hit >= HIT_BODY_BASE && hit < HIT_BODY_BASE + WIN_COUNT) {
            focus_window(hit - HIT_BODY_BASE);
        } else if (hit == HIT_NONE) {
            focus_win = -1;
        }
    }

    if ((buttons & 1) && resize_win >= 0) {
        resize_move(x, y);
    } else if ((buttons & 1) && scroll_drag >= 0) {
        scrollbar_set_from_y(&consoles[scroll_drag], y);
    } else if ((buttons & 1) && drag_win >= 0) {
        drag_move(x, y);
    }

    if (release) {
        if (pressed_hit != HIT_NONE && hit == pressed_hit)
            activate_hit(pressed_hit);
        pressed_hit = HIT_NONE;
        drag_win = -1;
        resize_win = -1;
        scroll_drag = -1;
    }
    last_buttons = buttons;
}

/* -----------------------------------------------------------------------------
 * Keyboard
 * ----------------------------------------------------------------------------- */
void gui_key(char ch) {
    u8 key = (u8)ch;
    if (ctx_open) {
        if (key == 27)
            ctx_open = 0;
        return;
    }
    if (list_open) {
        if (ch == 27) {
            list_open = 0;
        } else if (ch == '\t') {
            list_sel = (list_sel + 1) % WIN_COUNT;
        } else if (ch == '\n') {
            int id = list_sel;
            list_open = 0;
            focus_window(id);
        }
        return;
    }
    if (menu_open) {
        if (ch == 27)
            menu_open = 0;
        return;
    }
    if (ch == 27) {
        if (focus_win == WIN_ABOUT)
            hide_window(WIN_ABOUT);
        else if (focus_win == WIN_SYS)
            hide_window(WIN_SYS);
        if (focus_win < WIN_C0 || focus_win > WIN_C2)
            focus_window(WIN_C0);
        return;
    }
    if (focus_win == WIN_ABOUT) {
        if (ch == '\n') {
            hide_window(WIN_ABOUT);
            focus_window(WIN_C0);
        }
        return;
    }
    if (focus_win >= WIN_C0 && focus_win <= WIN_C2) {
        if (ch == '\t') {
            list_open = 1;
            list_sel = focus_win;
            return;
        }
        if (key == GUI_KEY_PGUP) {
            term_scroll_by(&consoles[focus_win], consoles[focus_win].rows - 1);
            return;
        }
        if (key == GUI_KEY_PGDN) {
            term_scroll_by(&consoles[focus_win], -(consoles[focus_win].rows - 1));
            return;
        }
        term_key(&consoles[focus_win], ch);
    }
}

void gui_special(int key) {
    if (key == 1) {
        focus_window(WIN_ABOUT);
        menu_open = 0;
        list_open = 0;
    } else if (key == 2) {
        focus_window(WIN_SYS);
        menu_open = 0;
        list_open = 0;
    } else if (key == 3) {
        focus_window(WIN_C0);
        menu_open = 0;
        list_open = 0;
    } else if (key == 4) {
        list_open = !list_open;
        list_sel = focus_win >= 0 ? focus_win : 0;
        menu_open = 0;
    } else if (key == 5) {
        focus_window(WIN_C1);
        menu_open = 0;
        list_open = 0;
    } else if (key == 6) {
        focus_window(WIN_C2);
        menu_open = 0;
        list_open = 0;
    }
}

void gui_sys_write(const char *s, u32 len) {
    struct console *c = active_console();
    for (u32 i = 0; i < len; i++)
        term_putc(c, s[i]);
    gui_dirty = 1;
}

void gui_sys_print(const char *s) {
    struct console *c = active_console();
    while (*s)
        term_putc(c, *s++);
    gui_dirty = 1;
}

void gui_sys_write_console(int console, const char *s, u32 len) {
    if (console < WIN_C0 || console > WIN_C2)
        return;
    struct console *c = &consoles[console];
    for (u32 i = 0; i < len; i++)
        term_putc(c, s[i]);
    gui_dirty = 1;
}

void gui_sys_print_console(int console, const char *s) {
    if (console < WIN_C0 || console > WIN_C2)
        return;
    struct console *c = &consoles[console];
    while (*s)
        term_putc(c, *s++);
    gui_dirty = 1;
}

void gui_init(void) {
    set_palette();
    init_windows();
    for (int i = 0; i < NCONS; i++) {
        consoles[i].x = wins[i].x;
        consoles[i].y = wins[i].y;
        consoles[i].w = wins[i].w;
        consoles[i].h = wins[i].h;
        consoles[i].cols = term_width;
        consoles[i].rows = term_vis;
        term_init(&consoles[i], i);
    }
    sys_output = &consoles[0];
    focus_win = WIN_C0;
    last_console = WIN_C0;
    menu_open = 0;
    list_open = 0;
    list_sel = 0;
    pressed_hit = HIT_NONE;
    drag_win = -1;
    last_buttons = 0;
}







