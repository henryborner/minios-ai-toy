#ifndef MINIOS_GUI_H
#define MINIOS_GUI_H

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

void gui_init(void);
void gui_alloc_fb(void);
void gui_redraw(void);
void gui_key(char ch);
void gui_special(int key);
void gui_mouse_event(int x, int y, int buttons);
void gui_mouse_wheel(int dz);
void gui_theme(int dark);
int gui_cursor_only_motion(int x, int y, int buttons);
void gui_sys_print(const char *s);
void gui_sys_write(const char *s, u32 len);
void gui_sys_print_console(int console, const char *s);
void gui_sys_write_console(int console, const char *s, u32 len);
int gui_focused_console(void);
int gui_runtime_keyboard_wanted(void);
int gui_dirty_pending(void);
void gui_mark_dirty(void);
void gui_flush(void);

struct gui_canvas_cmd {
    u32 op;
    u32 x, y, w, h;
    u32 color;
    const char *text;
    u32 len;
};

struct gui_canvas_event {
    int x, y;
    u32 buttons;
    u32 closed;
    u32 width, height;
};

void gui_canvas_open(const char *title);
void gui_canvas_close(void);
int gui_canvas_w(void);
int gui_canvas_h(void);
int gui_canvas_cmd(const struct gui_canvas_cmd *cmd);
int gui_canvas_event(struct gui_canvas_event *out);

extern int scr_w;
extern int scr_h;
extern u8 vbe_active;
extern volatile u8 *vbe_lfb;
extern u32 vbe_pitch;
extern u32 vbe_phys_base;
extern volatile u32 ticks;
extern char cpu[13];
extern u32 heap_used;
extern volatile int mouse_x;
extern volatile int mouse_y;
extern volatile int mouse_buttons;

void machine_reboot(void);
void machine_halt(void);

#endif
