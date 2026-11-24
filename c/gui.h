#ifndef MINIOS_GUI_H
#define MINIOS_GUI_H

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

void gui_init(void);
void gui_redraw(void);
void gui_key(char ch);
void gui_special(int key);
void gui_mouse_event(int x, int y, int buttons);
void gui_sys_print(const char *s);

extern volatile u32 ticks;
extern char cpu[13];
extern u32 heap_used;
extern volatile int mouse_x;
extern volatile int mouse_y;
extern volatile int mouse_buttons;

void machine_reboot(void);
void machine_halt(void);

#endif
