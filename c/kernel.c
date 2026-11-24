/* =============================================================================
 *  MiniOS 2.0 - 32-bit protected-mode C kernel (graphical shell)
 *  Assembly: boot.asm, entry.asm, interrupts.asm.
 * ============================================================================= */

#include "gui.h"

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

char cpu[13];
volatile u32 ticks;
volatile int mouse_x = 160;
volatile int mouse_y = 100;
volatile int mouse_buttons = 0;
u32 heap_used;

static u8 heap[64 * 1024];

static volatile u8 kbd_buf[64];
static volatile u8 kbd_head, kbd_tail;
static u8 shift_down, extended;

static volatile u8 mouse_cycle;
static volatile u8 mouse_packet[3];
static volatile u8 mouse_event;

/* -----------------------------------------------------------------------------
 * I/O
 * ----------------------------------------------------------------------------- */
static inline u8 inb(u16 port) {
    u8 v;
    __asm__ __volatile__("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outb(u16 port, u8 v) {
    __asm__ __volatile__("outb %0, %1" : : "a"(v), "Nd"(port));
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

static inline void interrupts_on(void) {
    __asm__ __volatile__("sti");
}

void machine_halt(void) {
    for (;;)
        __asm__ __volatile__("cli; hlt");
}

void machine_reboot(void) {
    outb(0x64, 0xFE);
    machine_halt();
}

void *kmalloc(u32 size) {
    size = (size + 3) & ~3u;
    if (heap_used + size > sizeof(heap))
        return 0;
    void *p = &heap[heap_used];
    heap_used += size;
    return p;
}

/* -----------------------------------------------------------------------------
 * KBC helpers
 * ----------------------------------------------------------------------------- */
static int kbc_wait_input(void) {
    for (int i = 0; i < 100000; i++) {
        if (!(inb(0x64) & 2))
            return 1;
    }
    return 0;
}

static int kbc_wait_output(void) {
    for (int i = 0; i < 100000; i++) {
        if (inb(0x64) & 1)
            return 1;
    }
    return 0;
}

static void kbc_cmd(u8 cmd) {
    if (kbc_wait_input())
        outb(0x64, cmd);
}

static void kbc_data(u8 data) {
    if (kbc_wait_input())
        outb(0x60, data);
}

static int kbc_read(void) {
    if (!kbc_wait_output())
        return -1;
    return inb(0x60);
}

static void kbc_flush(void) {
    for (int i = 0; i < 64; i++) {
        if (!(inb(0x64) & 1))
            break;
        (void)inb(0x60);
    }
}

static void mouse_init(void) {
    kbc_flush();
    kbc_cmd(0xA8);          /* enable auxiliary device */

    kbc_cmd(0x20);          /* read controller config */
    int cfg = kbc_read();
    if (cfg >= 0) {
        u8 c = (u8)cfg;
        c |= 0x02;          /* IRQ12 on */
        c &= (u8)~0x20;     /* aux clock on */
        kbc_cmd(0x60);
        kbc_data(c);
    }

    kbc_cmd(0xD4);          /* next byte to mouse */
    kbc_data(0xF6);         /* set defaults */
    (void)kbc_read();

    kbc_cmd(0xD4);
    kbc_data(0xF4);         /* enable data reporting */
    (void)kbc_read();
    kbc_flush();
}

/* -----------------------------------------------------------------------------
 * Keyboard
 * ----------------------------------------------------------------------------- */
static const char keymap[128] = {
    [0x01] = 27, [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9',
    [0x0B] = '0', [0x0C] = '-', [0x0D] = '=', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[', [0x1B] = ']', [0x1C] = '\n',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = ';',
    [0x28] = '\'', [0x29] = '`', [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '/',
    [0x39] = ' '
};

static void kbd_push(u8 ch) {
    u8 next = (u8)((kbd_head + 1) & 63);
    if (next != kbd_tail) {
        kbd_buf[kbd_head] = ch;
        kbd_head = next;
    }
}

static void keyboard_scancode(u8 sc) {
    if (sc == 0xE0) {
        extended = 1;
        return;
    }
    if (extended) {
        extended = 0;
        return;
    }
    if (sc & 0x80) {
        u8 make = sc & 0x7F;
        if (make == 0x2A || make == 0x36)
            shift_down = 0;
        return;
    }
    if (sc == 0x2A || sc == 0x36) {
        shift_down = 1;
        return;
    }
    if (sc == 0x3B) { kbd_push(0xF1); return; }
    if (sc == 0x3C) { kbd_push(0xF2); return; }
    if (sc == 0x3D) { kbd_push(0xF3); return; }

    char ch = keymap[sc];
    if (!ch)
        return;
    if (shift_down && ch >= 'a' && ch <= 'z')
        ch = (char)(ch - 32);
    kbd_push((u8)ch);
}

void irq1_handler(void) {
    u8 sc = inb(0x60);
    keyboard_scancode(sc);
    outb(0x20, 0x20);
}

/* -----------------------------------------------------------------------------
 * Mouse
 * ----------------------------------------------------------------------------- */
void irq12_handler(void) {
    u8 data = inb(0x60);
    u8 cycle = mouse_cycle;

    if (cycle == 0 && !(data & 0x08)) {
        outb(0xA0, 0x20);
        outb(0x20, 0x20);
        return;
    }
    mouse_packet[cycle] = data;
    cycle++;
    if (cycle == 3) {
        cycle = 0;
        int dx = (signed char)mouse_packet[1];
        int dy = (signed char)mouse_packet[2];
        int nx = mouse_x + dx;
        int ny = mouse_y - dy;
        if (nx < 0) nx = 0;
        if (nx >= 320) nx = 319;
        if (ny < 0) ny = 0;
        if (ny >= 200) ny = 199;
        mouse_x = nx;
        mouse_y = ny;
        mouse_buttons = mouse_packet[0] & 7;
        mouse_event = 1;
    }
    mouse_cycle = cycle;

    outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

/* -----------------------------------------------------------------------------
 * IDT / PIC / PIT
 * ----------------------------------------------------------------------------- */
struct idt_entry {
    u16 base_lo;
    u16 selector;
    u8  zero;
    u8  flags;
    u16 base_hi;
} __attribute__((packed));

struct idt_ptr {
    u16 limit;
    u32 base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtp;

extern void irq0_stub(void);
extern void irq1_stub(void);
extern void irq12_stub(void);
extern void isr_default(void);

static void idt_set(int n, u32 handler) {
    idt[n].base_lo = (u16)(handler & 0xFFFF);
    idt[n].selector = 0x08;
    idt[n].zero = 0;
    idt[n].flags = 0x8E;
    idt[n].base_hi = (u16)(handler >> 16);
}

void irq0_handler(void) {
    ticks++;
    outb(0x20, 0x20);
}

static void pic_remap(void) {
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait();
    outb(0xA1, 0x28); io_wait();
    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();

    outb(0x21, 0xF8);       /* IRQ0, IRQ1, cascade IRQ2 */
    outb(0xA1, 0xEF);       /* IRQ12 only */
}

static void pit_init(void) {
    u32 divisor = 11932;
    outb(0x43, 0x36);
    outb(0x40, (u8)(divisor & 0xFF));
    outb(0x40, (u8)(divisor >> 8));
}

static void interrupts_setup(void) {
    __asm__ __volatile__("cli");

    for (int i = 0; i < 256; i++)
        idt_set(i, (u32)isr_default);
    idt_set(0x20, (u32)irq0_stub);
    idt_set(0x21, (u32)irq1_stub);
    idt_set(0x2C, (u32)irq12_stub);

    idtp.limit = (u16)(sizeof(idt) - 1);
    idtp.base = (u32)&idt;
    __asm__ __volatile__("lidt %0" : : "m"(idtp));

    pic_remap();
    pit_init();
}

/* -----------------------------------------------------------------------------
 * CPU
 * ----------------------------------------------------------------------------- */
static void cpu_vendor(void) {
    u32 a, b, c, d;
    __asm__ __volatile__("cpuid"
                         : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                         : "a"(0));
    (void)a;
    u32 regs[3] = { b, d, c };
    for (int i = 0; i < 3; i++) {
        cpu[i * 4 + 0] = (char)(regs[i] & 0xFF);
        cpu[i * 4 + 1] = (char)((regs[i] >> 8) & 0xFF);
        cpu[i * 4 + 2] = (char)((regs[i] >> 16) & 0xFF);
        cpu[i * 4 + 3] = (char)((regs[i] >> 24) & 0xFF);
    }
    cpu[12] = 0;
}

/* -----------------------------------------------------------------------------
 * Entry
 * ----------------------------------------------------------------------------- */
void kmain(void) {
    cpu_vendor();
    gui_init();
    interrupts_setup();
    mouse_init();

    interrupts_on();
    gui_redraw();

    u32 last_draw = ticks;

    for (;;) {
        int event = 0;

        while (kbd_tail != kbd_head) {
            u8 ch = kbd_buf[kbd_tail];
            kbd_tail = (u8)((kbd_tail + 1) & 63);
            if (ch >= 0xF1 && ch <= 0xF3)
                gui_special(ch - 0xF0);
            else
                gui_key((char)ch);
            event = 1;
        }

        if (mouse_event) {
            mouse_event = 0;
            gui_mouse_event(mouse_x, mouse_y, mouse_buttons);
            event = 1;
        }

        if (event || (u32)(ticks - last_draw) >= 10) {
            gui_redraw();
            last_draw = ticks;
        }

        __asm__ __volatile__("hlt");
    }
}
