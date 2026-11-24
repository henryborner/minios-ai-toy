#include "gui.h"

#define W 320
#define H 200
#define TASKBAR_Y 186
#define CONSOLE_W 296
#define CONSOLE_H 144
#define SYSTEM_W 224
#define SYSTEM_H 96
#define ABOUT_W 200
#define ABOUT_H 104
#define MENU_X 2
#define MENU_Y 132
#define MENU_W 108
#define MENU_H 52

static u8 fb[W * H] __attribute__((aligned(4)));
static const u8 *const FONT = (const u8 *)0x000FFA6E;

static int console_x = 12, console_y = 22;
static int system_x = 50, system_y = 46;
static int about_x = 60, about_y = 52;
static u8 console_visible = 1, system_visible = 0, about_visible = 0, menu_open = 0;
static int drag_win = 0, drag_dx = 0, drag_dy = 0;
static int last_buttons = 0;

static char term[14][40];
static int term_n = 1, term_cur = 0;
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
 * Palette / framebuffer
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

static void raise_box(int x, int y, int w, int h) {
    fill_rect(x, y, w, h, 0);
    fill_rect(x + 1, y + 1, w - 2, h - 2, 7);
    fill_rect(x + 1, y + 1, w - 2, 1, 15);
    fill_rect(x + 1, y + 1, 1, h - 2, 15);
    fill_rect(x + 1, y + h - 2, w - 2, 1, 8);
    fill_rect(x + w - 2, y + 1, 1, h - 2, 8);
}

static void blit(void) {
    u32 *dst = (u32 *)0x000A0000;
    const u32 *src = (const u32 *)fb;
    for (int i = 0; i < (W * H) / 4; i++)
        dst[i] = src[i];
}

/* -----------------------------------------------------------------------------
 * Clock / RTC
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
 * Terminal / shell
 * ----------------------------------------------------------------------------- */
static int line_len(int index) {
    int n = 0;
    while (n < 39 && term[index][n])
        n++;
    return n;
}

static void term_clear(void) {
    for (int r = 0; r < 14; r++)
        for (int c = 0; c < 40; c++)
            term[r][c] = 0;
    term_n = 1;
    term_cur = 0;
}

static void term_newline(void) {
    if (term_n < 14) {
        for (int c = 0; c < 40; c++)
            term[term_n][c] = 0;
        term_cur = term_n;
        term_n++;
    } else {
        for (int r = 1; r < 14; r++)
            for (int c = 0; c < 40; c++)
                term[r - 1][c] = term[r][c];
        for (int c = 0; c < 40; c++)
            term[13][c] = 0;
        term_cur = 13;
    }
}

static void term_putc(char ch) {
    if (ch == '\n') {
        term_newline();
        return;
    }
    int len = line_len(term_cur);
    if (len >= 35) {
        term_newline();
        len = 0;
    }
    term[term_cur][len] = ch;
    term[term_cur][len + 1] = 0;
}

static void term_puts(const char *s) {
    while (*s)
        term_putc(*s++);
}

static void term_prompt(void) {
    term_puts("C:\\> ");
}

static void term_u32(u32 v) {
    char b[12];
    int i = 0;
    if (!v) {
        term_putc('0');
        return;
    }
    while (v) {
        b[i++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (i)
        term_putc(b[--i]);
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

static void term_init(void) {
    term_clear();
    term_puts("MiniOS 2.0 - C GUI\n");
    term_puts("32-bit protected mode\n\n");
    term_prompt();
}

static void run_command(char *line) {
    while (*line == ' ')
        line++;
    if (!*line)
        return;

    if (streq(line, "help")) {
        term_puts("Commands:\n");
        term_puts("help ver cpu mem time date\n");
        term_puts("uptime clear echo sysinfo\n");
        term_puts("about ring3 tasks reboot halt\n");
    } else if (streq(line, "ver")) {
        term_puts("MiniOS 2.0 C GUI (32-bit)\n");
    } else if (streq(line, "cpu")) {
        term_puts("CPU: ");
        term_puts(cpu);
        term_puts("\n");
    } else if (streq(line, "mem")) {
        term_puts("Heap: ");
        term_u32(heap_used);
        term_puts(" / 65536 bytes\n");
    } else if (streq(line, "time")) {
        update_clock();
        term_puts("Time: ");
        term_puts(clock_hms);
        term_puts("\n");
    } else if (streq(line, "date")) {
        update_clock();
        term_puts("Date: ");
        term_puts(date_str);
        term_puts("\n");
    } else if (streq(line, "uptime")) {
        make_uptime();
        term_puts("Uptime: ");
        term_puts(uptime_str);
        term_puts("\n");
    } else if (streq(line, "clear")) {
        term_clear();
        term_puts("MiniOS 2.0 - C GUI\n\n");
    } else if (streq(line, "echo")) {
        term_puts("\n");
    } else if (starts_with(line, "echo ")) {
        term_puts(line + 5);
        term_puts("\n");
    } else if (streq(line, "sysinfo")) {
        system_visible = 1;
        term_puts("System window opened.\n");
    } else if (streq(line, "about")) {
        about_visible = 1;
        term_puts("About window opened.\n");
    } else if (streq(line, "ring3")) {
        extern void enter_ring3_demo(void);
        term_puts("Entering Ring 3 (paging on)...\n");
        gui_redraw();
        enter_ring3_demo();
    } else if (streq(line, "tasks")) {
        extern void run_task_demo(void);
        term_puts("Starting two cooperative Ring 3 tasks...\n");
        gui_redraw();
        run_task_demo();
    } else if (streq(line, "reboot")) {
        term_puts("Rebooting...\n");
        machine_reboot();
    } else if (streq(line, "halt")) {
        term_puts("CPU halted.\n");
        machine_halt();
    } else {
        term_puts("Bad command or file name\n");
    }
}

static void term_key(char ch) {
    if (ch == '\n') {
        char *line = term[term_cur];
        char command[40];
        int i = 0;
        while (i < 39 && line[5 + i]) {
            command[i] = line[5 + i];
            i++;
        }
        command[i] = 0;
        term_newline();
        run_command(command);
        term_prompt();
        return;
    }
    if (ch == '\b') {
        int len = line_len(term_cur);
        if (len > 5) {
            term[term_cur][len - 1] = 0;
        }
        return;
    }
    if (ch >= 32 && ch < 127) {
        if (line_len(term_cur) < 35)
            term_putc(ch);
    }
}

/* -----------------------------------------------------------------------------
 * Window chrome
 * ----------------------------------------------------------------------------- */
static void window_frame(int x, int y, int w, int h, const char *title) {
    fill_rect(x + 3, y + 3, w, h, 8);
    fill_rect(x, y, w, h, 0);
    fill_rect(x + 1, y + 1, w - 2, h - 2, 7);
    fill_rect(x + 1, y + 1, w - 2, 10, 1);
    text(x + 4, y + 2, title, 15);
    raise_box(x + w - 14, y + 2, 12, 8);
    draw_char(x + w - 12, y + 2, 'x', 0, 1);
    fill_rect(x + 1, y + 11, w - 2, h - 12, 15);
}

static void draw_desktop(void) {
    fill_rect(0, 0, W, TASKBAR_Y, 3);
    text(8, 5, "MiniOS 2.0", 15);
    text(206, 5, "1.44 MB C GUI", 11);
    fill_rect(0, 17, W, 1, 2);
}

static void draw_taskbar(void) {
    fill_rect(0, TASKBAR_Y, W, 14, 7);
    fill_rect(0, TASKBAR_Y, W, 1, 15);
    fill_rect(0, TASKBAR_Y + 1, W, 1, 8);

    raise_box(2, 188, 50, 10);
    text(6, 189, "Start", 0);
    raise_box(56, 188, 62, 10);
    text(59, 189, "Console", 0);
    raise_box(122, 188, 56, 10);
    text(124, 189, "System", 0);
    text(184, 189, "MiniOS", 1);
    fill_rect(264, 188, 52, 10, 0);
    fill_rect(265, 189, 50, 8, 15);
    text(274, 189, clock_hm, 0);
}

static void draw_menu(void) {
    if (!menu_open)
        return;
    fill_rect(MENU_X + 3, MENU_Y + 3, MENU_W, MENU_H, 8);
    fill_rect(MENU_X, MENU_Y, MENU_W, MENU_H, 0);
    fill_rect(MENU_X + 1, MENU_Y + 1, MENU_W - 2, MENU_H - 2, 7);

    const char *items[4] = { "System info", "Console", "About MiniOS", "Reboot" };
    for (int i = 0; i < 4; i++) {
        int iy = MENU_Y + 2 + i * 12;
        if (mouse_x >= MENU_X + 2 && mouse_x < MENU_X + MENU_W - 2 &&
            mouse_y >= iy && mouse_y < iy + 12) {
            fill_rect(MENU_X + 2, iy, MENU_W - 4, 12, 1);
            text(MENU_X + 8, iy + 2, items[i], 15);
        } else {
            text(MENU_X + 8, iy + 2, items[i], 0);
        }
    }
}

static void draw_term(void) {
    for (int r = 0; r < 14; r++) {
        text(console_x + 4, console_y + 14 + r * 9, term[r], 0);
    }
    if (!about_visible && ((ticks / 50) & 1)) {
        int len = line_len(term_cur);
        fill_rect(console_x + 4 + len * 8, console_y + 14 + term_cur * 9, 1, 8, 0);
    }
}

static void draw_console(void) {
    if (!console_visible)
        return;
    window_frame(console_x, console_y, CONSOLE_W, CONSOLE_H, "Console - MiniOS");
    draw_term();
}

static void draw_system(void) {
    if (!system_visible)
        return;
    int x = system_x, y = system_y;
    window_frame(x, y, SYSTEM_W, SYSTEM_H, "System Information");
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
    if (!about_visible)
        return;
    int x = about_x, y = about_y;
    window_frame(x, y, ABOUT_W, ABOUT_H, "About MiniOS");
    text2(x + 52, y + 16, "MiniOS", 1);
    text(x + 24, y + 42, "A C GUI in 1.44 MB", 0);
    text(x + 24, y + 53, "32-bit protected mode", 0);
    text(x + 8, y + 64, "(c) 2026 MiniOS Project", 8);
    raise_box(x + 80, y + 80, 40, 14);
    text(x + 92, y + 83, "OK", 0);
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
    draw_console();
    draw_system();
    draw_about();
    draw_menu();
    draw_cursor();
    blit();
}

/* -----------------------------------------------------------------------------
 * Mouse hit testing / clicks
 * ----------------------------------------------------------------------------- */
static int inside(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static void start_drag(int win, int x, int y) {
    drag_win = win;
    if (win == 1) {
        drag_dx = x - console_x;
        drag_dy = y - console_y;
    } else if (win == 2) {
        drag_dx = x - system_x;
        drag_dy = y - system_y;
    } else {
        drag_dx = x - about_x;
        drag_dy = y - about_y;
    }
}

static void drag_move(int x, int y) {
    if (drag_win == 1) {
        int nx = x - drag_dx;
        int ny = y - drag_dy;
        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;
        if (nx > W - CONSOLE_W) nx = W - CONSOLE_W;
        if (ny > TASKBAR_Y - CONSOLE_H) ny = TASKBAR_Y - CONSOLE_H;
        console_x = nx;
        console_y = ny;
    } else if (drag_win == 2) {
        int nx = x - drag_dx;
        int ny = y - drag_dy;
        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;
        if (nx > W - SYSTEM_W) nx = W - SYSTEM_W;
        if (ny > TASKBAR_Y - SYSTEM_H) ny = TASKBAR_Y - SYSTEM_H;
        system_x = nx;
        system_y = ny;
    } else if (drag_win == 3) {
        int nx = x - drag_dx;
        int ny = y - drag_dy;
        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;
        if (nx > W - ABOUT_W) nx = W - ABOUT_W;
        if (ny > TASKBAR_Y - ABOUT_H) ny = TASKBAR_Y - ABOUT_H;
        about_x = nx;
        about_y = ny;
    }
}

static void menu_action(int index) {
    menu_open = 0;
    if (index == 0)
        system_visible = 1;
    else if (index == 1)
        console_visible = 1;
    else if (index == 2)
        about_visible = 1;
    else
        machine_reboot();
}

static void click(int x, int y) {
    if (about_visible) {
        if (!inside(x, y, about_x, about_y, ABOUT_W, ABOUT_H))
            return;
        if (inside(x, y, about_x + ABOUT_W - 20, about_y, 20, 12) ||
            inside(x, y, about_x + 76, about_y + 77, 48, 19)) {
            about_visible = 0;
            return;
        }
        if (inside(x, y, about_x + 1, about_y + 1, ABOUT_W - 2, 10))
            start_drag(3, x, y);
        return;
    }

    if (menu_open) {
        if (inside(x, y, MENU_X, MENU_Y, MENU_W, MENU_H)) {
            int idx = (y - MENU_Y - 2) / 12;
            if (idx >= 0 && idx < 4)
                menu_action(idx);
            return;
        }
        menu_open = 0;
        return;
    }

    if (y >= TASKBAR_Y) {
        if (inside(x, y, 2, TASKBAR_Y, 50, 14)) {
            menu_open = 1;
        } else if (inside(x, y, 56, TASKBAR_Y, 62, 14)) {
            console_visible = 1;
        } else if (inside(x, y, 122, TASKBAR_Y, 56, 14)) {
            system_visible = 1;
        }
        return;
    }

    if (system_visible && inside(x, y, system_x, system_y, SYSTEM_W, SYSTEM_H)) {
        if (inside(x, y, system_x + SYSTEM_W - 20, system_y, 20, 12)) {
            system_visible = 0;
            return;
        }
        if (inside(x, y, system_x + 1, system_y + 1, SYSTEM_W - 2, 10))
            start_drag(2, x, y);
        return;
    }

    if (console_visible && inside(x, y, console_x, console_y, CONSOLE_W, CONSOLE_H)) {
        if (inside(x, y, console_x + CONSOLE_W - 20, console_y, 20, 12)) {
            console_visible = 0;
            return;
        }
        if (inside(x, y, console_x + 1, console_y + 1, CONSOLE_W - 2, 10))
            start_drag(1, x, y);
        return;
    }
}

void gui_mouse_event(int x, int y, int buttons) {
    int pressed = (buttons & 1) && !(last_buttons & 1);
    if (pressed)
        click(x, y);
    if ((buttons & 1) && drag_win)
        drag_move(x, y);
    if (!(buttons & 1))
        drag_win = 0;
    last_buttons = buttons;
}

/* -----------------------------------------------------------------------------
 * Keyboard
 * ----------------------------------------------------------------------------- */
static void escape_action(void) {
    if (about_visible) {
        about_visible = 0;
    } else if (system_visible) {
        system_visible = 0;
    } else if (menu_open) {
        menu_open = 0;
    }
}

void gui_key(char ch) {
    if (ch == 27) {
        escape_action();
        return;
    }
    if (about_visible) {
        if (ch == '\n')
            about_visible = 0;
        return;
    }
    if (console_visible)
        term_key(ch);
}

void gui_special(int key) {
    if (key == 1) {
        about_visible = 1;
        menu_open = 0;
    } else if (key == 2) {
        system_visible = 1;
        menu_open = 0;
    } else if (key == 3) {
        console_visible = 1;
        menu_open = 0;
    }
}

void gui_sys_print(const char *s) {
    while (*s)
        term_putc(*s++);
    gui_redraw();
}

void gui_init(void) {
    set_palette();
    term_init();
    console_visible = 1;
    system_visible = 0;
    about_visible = 0;
    menu_open = 0;
    drag_win = 0;
    last_buttons = 0;
}
