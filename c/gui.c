#include "gui.h"

#define W 320
#define H 200
#define TASKBAR_Y 186

#define CON_W 296
#define CON_H 144
#define SYS_W 224
#define SYS_H 96
#define ABOUT_W 200
#define ABOUT_H 104

#define NCONS 3
#define WIN_C0 0
#define WIN_C1 1
#define WIN_C2 2
#define WIN_SYS 3
#define WIN_ABOUT 4
#define WIN_COUNT 5

#define MENU_X 2
#define MENU_Y 94
#define MENU_W 120
#define MENU_ITEM_H 12
#define MENU_ITEMS 7
#define MENU_H (MENU_ITEMS * MENU_ITEM_H + 4)

#define LIST_W 180
#define LIST_H 112
#define LIST_X ((W - LIST_W) / 2)
#define LIST_Y 36

#define HIT_NONE 0
#define HIT_START 1
#define HIT_TB_C1 2
#define HIT_TB_C2 3
#define HIT_TB_SYS 4
#define HIT_TB_WIN 5
#define HIT_MENU_BASE 100
#define HIT_CLOSE_BASE 200
#define HIT_OK 300
#define HIT_LIST_BASE 400
#define HIT_TITLE_BASE 500
#define HIT_BODY_BASE 600

struct console {
    int x, y, w, h;
    char term[14][40];
    int n;
    int cur;
    u8 esc;
};

struct window {
    int x, y, w, h;
    u8 visible;
    const char *title;
};

static u8 fb[W * H] __attribute__((aligned(4)));
static const u8 *const FONT = (const u8 *)0x000FFA6E;

static struct window wins[WIN_COUNT];
static struct console consoles[NCONS];
static int zorder[WIN_COUNT];
static int focus_win;
static struct console *sys_output;

static int menu_open;
static int list_open;
static int list_sel;

static int pressed_hit;
static int drag_win;
static int drag_dx, drag_dy;
static int last_buttons;

static char clock_hm[6];
static char clock_hms[9];
static char date_str[11];
static char uptime_str[9];

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
static const u8 palette[48] = {
    0, 0, 0,       0, 0, 42,      0, 42, 0,       0, 42, 42,
    42, 0, 0,      42, 0, 42,     42, 42, 0,      42, 42, 42,
    21, 21, 21,    0, 0, 63,      0, 63, 0,       0, 63, 63,
    63, 0, 0,      63, 0, 63,     63, 63, 0,      63, 63, 63
};

static void set_palette(void) {
    outb(0x3C8, 0);
    for (int i = 0; i < 48; i++)
        outb(0x3C9, palette[i]);
}

static void plot(int x, int y, u8 color) {
    if ((unsigned)x < W && (unsigned)y < H)
        fb[y * W + x] = color;
}

static void fill_rect(int x, int y, int w, int h, u8 color) {
    if (w <= 0 || h <= 0)
        return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    if (w <= 0 || h <= 0)
        return;
    for (int yy = 0; yy < h; yy++) {
        u8 *p = &fb[(y + yy) * W + x];
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
        if (x >= W)
            break;
    }
}

static void text2(int x, int y, const char *s, u8 color) {
    while (*s) {
        draw_char(x, y, *s++, color, 2);
        x += 16;
        if (x >= W)
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
    u32 *dst = (u32 *)0x000A0000;
    const u32 *src = (const u32 *)fb;
    for (int i = 0; i < (W * H) / 4; i++)
        dst[i] = src[i];
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

static void update_clock(void) {
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
static int win_visible(int id) {
    return wins[id].visible;
}

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
    focus_win = id;
    bring_front(id);
    if (id >= WIN_C0 && id <= WIN_C2)
        sys_output = &consoles[id];
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
    if (focus_win == id)
        focus_fallback();
}

static void init_windows(void) {
    wins[WIN_C0].x = 12;  wins[WIN_C0].y = 22;
    wins[WIN_C1].x = 16;  wins[WIN_C1].y = 28;
    wins[WIN_C2].x = 20;  wins[WIN_C2].y = 34;
    for (int i = 0; i < NCONS; i++) {
        wins[i].w = CON_W;
        wins[i].h = CON_H;
        wins[i].visible = (i == 0);
    }
    wins[WIN_C0].title = "Console 1 - MiniOS";
    wins[WIN_C1].title = "Console 2 - MiniOS";
    wins[WIN_C2].title = "Console 3 - MiniOS";

    wins[WIN_SYS].x = 56; wins[WIN_SYS].y = 42;
    wins[WIN_SYS].w = SYS_W; wins[WIN_SYS].h = SYS_H;
    wins[WIN_SYS].visible = 0;
    wins[WIN_SYS].title = "System Information";

    wins[WIN_ABOUT].x = 60; wins[WIN_ABOUT].y = 48;
    wins[WIN_ABOUT].w = ABOUT_W; wins[WIN_ABOUT].h = ABOUT_H;
    wins[WIN_ABOUT].visible = 0;
    wins[WIN_ABOUT].title = "About MiniOS";

    for (int i = 0; i < WIN_COUNT; i++)
        zorder[i] = i;
    focus_win = WIN_C0;
}

/* -----------------------------------------------------------------------------
 * Terminal / shell
 * ----------------------------------------------------------------------------- */
static int line_len(struct console *c, int index) {
    int n = 0;
    while (n < 39 && c->term[index][n])
        n++;
    return n;
}

static void term_clear(struct console *c) {
    for (int r = 0; r < 14; r++)
        for (int col = 0; col < 40; col++)
            c->term[r][col] = 0;
    c->n = 1;
    c->cur = 0;
    c->esc = 0;
}

static void term_newline(struct console *c) {
    if (c->n < 14) {
        for (int col = 0; col < 40; col++)
            c->term[c->n][col] = 0;
        c->cur = c->n;
        c->n++;
    } else {
        for (int r = 1; r < 14; r++)
            for (int col = 0; col < 40; col++)
                c->term[r - 1][col] = c->term[r][col];
        for (int col = 0; col < 40; col++)
            c->term[13][col] = 0;
        c->cur = 13;
    }
}

static void term_putc(struct console *c, char ch) {
    if (c->esc) {
        if (c->esc == 1) {
            if (ch == '[')
                c->esc = 2;
            else
                c->esc = 0;
        } else {
            if ((ch >= '0' && ch <= '9') || ch == ';')
                return;
            c->esc = 0;
        }
        return;
    }
    if (ch == 27) {
        c->esc = 1;
        return;
    }
    if (ch == '\r')
        return;
    if (ch == '\n') {
        term_newline(c);
        return;
    }
    if (ch == '\b') {
        int len = line_len(c, c->cur);
        if (len > 0)
            c->term[c->cur][len - 1] = 0;
        return;
    }
    if (ch == '\t') {
        int len = line_len(c, c->cur);
        int spaces = 4 - (len & 3);
        while (spaces-- > 0)
            term_putc(c, ' ');
        return;
    }
    int len = line_len(c, c->cur);
    if (len >= 35) {
        term_newline(c);
        len = 0;
    }
    c->term[c->cur][len] = ch;
    c->term[c->cur][len + 1] = 0;
}

static void term_puts(struct console *c, const char *s) {
    while (*s)
        term_putc(c, *s++);
}

static void term_prompt(struct console *c) {
    term_puts(c, "C:\\> ");
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

static void run_command(struct console *c, char *line) {
    while (*line == ' ')
        line++;
    if (!*line)
        return;

    if (streq(line, "help")) {
        term_puts(c, "Commands:\n");
        term_puts(c, "help ver cpu mem time date\n");
        term_puts(c, "uptime clear echo console windows\n");
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
        update_clock();
        term_puts(c, "Time: ");
        term_puts(c, clock_hms);
        term_puts(c, "\n");
    } else if (streq(line, "date")) {
        update_clock();
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
    if (ch == '\n') {
        char *line = c->term[c->cur];
        char command[40];
        int i = 0;
        while (i < 39 && line[5 + i]) {
            command[i] = line[5 + i];
            i++;
        }
        command[i] = 0;
        sys_output = c;
        term_newline(c);
        run_command(c, command);
        term_prompt(c);
        return;
    }
    if (ch == '\b') {
        int len = line_len(c, c->cur);
        if (len > 5)
            c->term[c->cur][len - 1] = 0;
        return;
    }
    if (ch >= 32 && ch < 127) {
        if (line_len(c, c->cur) < 35)
            term_putc(c, ch);
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
    fill_rect(0, 0, W, TASKBAR_Y, 3);
    text(8, 5, "MiniOS 2.0", 15);
    text(188, 5, "F4 windows", 11);
    fill_rect(0, 17, W, 1, 2);
}

static void draw_taskbar(void) {
    fill_rect(0, TASKBAR_Y, W, 14, 7);
    fill_rect(0, TASKBAR_Y, W, 1, 15);
    fill_rect(0, TASKBAR_Y + 1, W, 1, 8);

    taskbar_button(2, 188, 44, "Start", HIT_START, menu_open);
    taskbar_button(48, 188, 50, "Con1", HIT_TB_C1, focus_win == WIN_C0);
    taskbar_button(100, 188, 50, "Con2", HIT_TB_C2, focus_win == WIN_C1);
    taskbar_button(152, 188, 46, "Sys", HIT_TB_SYS, focus_win == WIN_SYS);
    taskbar_button(200, 188, 56, "Wins", HIT_TB_WIN, list_open);

    fill_rect(264, 188, 52, 10, 0);
    fill_rect(265, 189, 50, 8, 15);
    text(274, 189, clock_hm, 0);
}

static void window_frame(int id) {
    struct window *w = &wins[id];
    int active = (focus_win == id);
    fill_rect(w->x + 3, w->y + 3, w->w, w->h, 8);
    fill_rect(w->x, w->y, w->w, w->h, 0);
    fill_rect(w->x + 1, w->y + 1, w->w - 2, w->h - 2, 7);
    fill_rect(w->x + 1, w->y + 1, w->w - 2, 10, active ? 1 : 8);
    text(w->x + 4, w->y + 2, w->title, 15);

    int close_hit = HIT_CLOSE_BASE + id;
    if (is_pressed(close_hit))
        sunken_box(w->x + w->w - 14, w->y + 2, 12, 8);
    else
        raise_box_col(w->x + w->w - 14, w->y + 2, 12, 8, 7);
    draw_char(w->x + w->w - 12, w->y + 2, 'x', 0, 1);
    fill_rect(w->x + 1, w->y + 11, w->w - 2, w->h - 12, 15);
}

static void draw_console(int id) {
    struct console *c = &consoles[id];
    window_frame(id);
    for (int r = 0; r < 14; r++)
        text(c->x + 4, c->y + 14 + r * 9, c->term[r], 0);
    if (focus_win == id && ((ticks / 50) & 1)) {
        int len = line_len(c, c->cur);
        fill_rect(c->x + 4 + len * 8, c->y + 14 + c->cur * 9, 1, 8, 0);
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
    text(x + 6, y + 41, "Video: VGA 320x200x256", 0);
    text(x + 6, y + 50, "Boot: 1.44MB floppy", 0);
    text(x + 6, y + 59, "Mode: 32-bit protected", 0);
    text(x + 6, y + 68, "Uptime:", 0);
    text(x + 70, y + 68, uptime_str, 0);
    text(x + 6, y + 77, "Date:", 0);
    text(x + 54, y + 77, date_str, 0);
}

static void draw_about(void) {
    struct window *w = &wins[WIN_ABOUT];
    window_frame(WIN_ABOUT);
    int x = w->x, y = w->y;
    text2(x + 52, y + 16, "MiniOS", 1);
    text(x + 24, y + 42, "A C GUI in 1.44 MB", 0);
    text(x + 24, y + 53, "32-bit protected mode", 0);
    text(x + 8, y + 64, "(c) 2026 MiniOS Project", 8);
    if (is_pressed(HIT_OK))
        sunken_box(x + 80, y + 80, 40, 14);
    else
        raise_box_col(x + 80, y + 80, 40, 14, 7);
    text(x + 92, y + 83, "OK", 0);
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
        else if (!wins[i].visible)
            text(LIST_X + LIST_W - 16, iy + 3, "-", 8);
    }
    text(LIST_X + 6, LIST_Y + LIST_H - 12, "Tab/Enter/Esc", 8);
}

static void draw_cursor(void) {
    static const char art[18][13] = {
        "X...........", "XX..........", "XWX.........", "XWWX........",
        "XWWWX.......", "XWWWWX......", "XWWWWWX.....", "XWWWWWWX....",
        "XWWWWWWWX...", "XWWWWWWWWX..", "XWWWWWWWWWX.", "XWWWWWXXXXXX",
        "XWWWXWX.....", "XWWX.XWX....", "XWX..XWX....", "XX....XWX...",
        "X.....XWX...", "......XXX..."
    };
    for (int r = 0; r < 18; r++) {
        int py = mouse_y + r;
        if ((unsigned)py >= H)
            continue;
        for (int c = 0; c < 12; c++) {
            int px = mouse_x + c;
            if ((unsigned)px >= W)
                continue;
            char ch = art[r][c];
            if (ch == '.')
                continue;
            fb[py * W + px] = (ch == 'X') ? 0 : 15;
        }
    }
}

void gui_redraw(void) {
    update_clock();
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
    }
    draw_menu();
    draw_list();
    draw_cursor();
    blit();
}

/* -----------------------------------------------------------------------------
 * Hit testing / focus / actions
 * ----------------------------------------------------------------------------- */
static int inside(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static int hit_test(int x, int y) {
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
        return HIT_NONE;
    }

    for (int i = WIN_COUNT - 1; i >= 0; i--) {
        int id = zorder[i];
        struct window *w = &wins[id];
        if (!w->visible || !inside(x, y, w->x, w->y, w->w, w->h))
            continue;
        if (inside(x, y, w->x + w->w - 20, w->y, 20, 12))
            return HIT_CLOSE_BASE + id;
        if (id == WIN_ABOUT && inside(x, y, w->x + 76, w->y + 77, 48, 19))
            return HIT_OK;
        if (inside(x, y, w->x + 1, w->y + 1, w->w - 2, 10))
            return HIT_TITLE_BASE + id;
        return HIT_BODY_BASE + id;
    }
    return HIT_NONE;
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
    if (nx > W - w->w) nx = W - w->w;
    if (ny > TASKBAR_Y - w->h) ny = TASKBAR_Y - w->h;
    w->x = nx;
    w->y = ny;
    if (drag_win >= WIN_C0 && drag_win <= WIN_C2) {
        consoles[drag_win].x = nx;
        consoles[drag_win].y = ny;
    }
}

static void activate_hit(int hit) {
    if (hit == HIT_START) {
        menu_open = !menu_open;
        list_open = 0;
        return;
    }
    if (hit == HIT_TB_C1) { focus_window(WIN_C0); return; }
    if (hit == HIT_TB_C2) { focus_window(WIN_C1); return; }
    if (hit == HIT_TB_SYS) { focus_window(WIN_SYS); return; }
    if (hit == HIT_TB_WIN) {
        list_open = !list_open;
        list_sel = focus_win >= 0 ? focus_win : 0;
        menu_open = 0;
        return;
    }
    if (hit >= HIT_CLOSE_BASE && hit < HIT_CLOSE_BASE + WIN_COUNT) {
        hide_window(hit - HIT_CLOSE_BASE);
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

void gui_mouse_event(int x, int y, int buttons) {
    int press = (buttons & 1) && !(last_buttons & 1);
    int release = !(buttons & 1) && (last_buttons & 1);
    int hit = hit_test(x, y);

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

    if (press) {
        pressed_hit = hit;
        if (hit >= HIT_TITLE_BASE && hit < HIT_TITLE_BASE + WIN_COUNT) {
            int id = hit - HIT_TITLE_BASE;
            focus_window(id);
            start_drag(id, x, y);
        } else if (hit >= HIT_BODY_BASE && hit < HIT_BODY_BASE + WIN_COUNT) {
            focus_window(hit - HIT_BODY_BASE);
        } else if (hit == HIT_NONE) {
            focus_win = -1;
        }
    }

    if ((buttons & 1) && drag_win >= 0)
        drag_move(x, y);

    if (release) {
        if (pressed_hit != HIT_NONE && hit == pressed_hit)
            activate_hit(pressed_hit);
        pressed_hit = HIT_NONE;
        drag_win = -1;
    }
    last_buttons = buttons;
}

/* -----------------------------------------------------------------------------
 * Keyboard
 * ----------------------------------------------------------------------------- */
void gui_key(char ch) {
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
    gui_redraw();
}

void gui_sys_print(const char *s) {
    struct console *c = active_console();
    while (*s)
        term_putc(c, *s++);
    gui_redraw();
}

void gui_init(void) {
    set_palette();
    init_windows();
    for (int i = 0; i < NCONS; i++) {
        consoles[i].x = wins[i].x;
        consoles[i].y = wins[i].y;
        consoles[i].w = wins[i].w;
        consoles[i].h = wins[i].h;
        term_init(&consoles[i], i);
    }
    sys_output = &consoles[0];
    focus_win = WIN_C0;
    menu_open = 0;
    list_open = 0;
    list_sel = 0;
    pressed_hit = HIT_NONE;
    drag_win = -1;
    last_buttons = 0;
}
