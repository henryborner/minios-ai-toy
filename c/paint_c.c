#include "syscall.h"

#define OP_CLEAR 0u
#define OP_PIXEL 1u
#define OP_RECT  2u
#define OP_LINE  3u
#define OP_TEXT  4u

static struct minios_canvas_event ev;

static void ctl(uint32_t op, const char *arg) {
    (void)sys_canvas_ctl(op, arg);
}

static void draw_cmd(uint32_t op, int x, int y, int w, int h,
                     uint32_t color, const char *text, uint32_t len) {
    struct minios_canvas_cmd cmd;
    cmd.op = op;
    cmd.x = (uint32_t)x;
    cmd.y = (uint32_t)y;
    cmd.w = (uint32_t)w;
    cmd.h = (uint32_t)h;
    cmd.color = color;
    cmd.text = text;
    cmd.len = len;
    (void)sys_canvas_draw(&cmd);
}

static void line(int x0, int y0, int x1, int y1, uint32_t color) {
    draw_cmd(OP_LINE, x0, y0, x1, y1, color, 0, 0);
}

static void present(void) {
    ctl(7, 0);
}

static uint32_t text_len(const char *s) {
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}

static void banner(uint32_t color) {
    const char *s = "C PAINT drag c=clear 0-9 q=quit";
    draw_cmd(OP_TEXT, 4, 3, 0, 0, color, s, text_len(s));
}

void _start(void) {
    uint32_t color = 15;
    int dragging = 0;
    int last_x = 0, last_y = 0;
    uint32_t size;

    if (sys_canvas_ctl(1, "C Paint") < 0)
        sys_exit_status(1);
    size = (uint32_t)sys_canvas_ctl(2, 0);
    (void)size;
    ctl(8, (const char *)1);

    draw_cmd(OP_CLEAR, 0, 0, 0, 0, 0, 0, 0);
    banner(15);
    present();

    for (;;) {
        if (sys_canvas_event(&ev) > 0) {
            if (ev.type == 5)
                break;
            if (ev.type == 4) {
                uint32_t k = ev.key;
                if (k == 'q' || k == 'Q' || k == 27)
                    break;
                if (k == 'c' || k == 'C') {
                    draw_cmd(OP_CLEAR, 0, 0, 0, 0, 0, 0, 0);
                    banner(color);
                    present();
                } else if (k >= '0' && k <= '9') {
                    color = k - '0';
                    banner(color);
                    present();
                }
                continue;
            }
            if (ev.type == 1) {
                dragging = 1;
                last_x = ev.x;
                last_y = ev.y;
                draw_cmd(OP_PIXEL, ev.x, ev.y, 0, 0, color, 0, 0);
                present();
            } else if (ev.type == 3 && dragging) {
                line(last_x, last_y, ev.x, ev.y, color);
                last_x = ev.x;
                last_y = ev.y;
                present();
            } else if (ev.type == 2) {
                dragging = 0;
            }
        } else {
            sys_sleep_ms(10);
        }
    }

    ctl(0, 0);
    sys_exit_status(0);
}
