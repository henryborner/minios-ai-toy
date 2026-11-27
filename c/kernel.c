/* =============================================================================
 *  MiniOS 2.0 - 32-bit protected-mode C kernel (graphical shell)
 *  Assembly: boot.asm, entry.asm, interrupts.asm.
 * ============================================================================= */

#include "gui.h"
#include "frame_alloc_search.h"
#include "memmap_normalize.h"

/* pusha frame layout: EDI ESI EBP ESP EBX EDX ECX EAX. */
#define SYS_ARG1(f) (((u32 *)(f))[4]) /* EBX */
#define SYS_ARG2(f) (((u32 *)(f))[6]) /* ECX */
#define SYS_ARG3(f) (((u32 *)(f))[5]) /* EDX */
#define SYS_RET(f)  (((u32 *)(f))[7]) /* EAX */
#include "fdc.h"

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

char cpu[13];
volatile u32 ticks;
int scr_w = 320;
int scr_h = 200;
u8 vbe_active = 0;
volatile u8 *vbe_lfb = 0;
u32 vbe_pitch = 320;
u32 vbe_phys_base = 0x000A0000u;
volatile int mouse_x = 160;
volatile int mouse_y = 100;
volatile int mouse_buttons = 0;
static u32 irq_save_flags(void);
static void irq_restore_flags(u32 flags);
void serial_u32(u32 v);
int kernel_fdc_lock(void);
void kernel_fdc_unlock(void);
#define SIGINT       1u
#define SIGTERM      2u
#define SIGSTOP      4u
#define SIGCONT      8u
#define SIGCHLD      16u
#define SIGKILL      32u
#define SIG_TERM_MASK SIGTERM
int task_stop_pid(u32 pid);
int task_bg_pid(u32 pid);
int task_kill_pid(u32 pid);
static void task_signal_console(int cid, u8 sig);
static void wake_fdc_tasks(void);
static void sched_yield(void);
static int sched_block_current(void);
static volatile u8 scheduler_active;
u32 heap_used;

static u8 heap[64 * 1024];

void *kernel_alloc_pages_zero(u32 pages);
void kernel_free_pages_ptr(void *p, u32 pages);
static volatile u8 (*kbd_buf)[64];
static volatile u8 *kbd_head, *kbd_tail;
static int kbd_console_count;

int kernel_kbd_resize(int n) {
    if (n <= kbd_console_count)
        return 1;
    u32 buf_pages = ((u32)n * 64u + 4095u) / 4096u;
    u32 vec_pages = ((u32)n + 4095u) / 4096u;
    volatile u8 (*nb)[64] = (volatile u8 (*)[64])kernel_alloc_pages_zero(buf_pages);
    volatile u8 *nh = (volatile u8 *)kernel_alloc_pages_zero(vec_pages);
    volatile u8 *nt = (volatile u8 *)kernel_alloc_pages_zero(vec_pages);
    if (!nb || !nh || !nt) {
        if (nb) kernel_free_pages_ptr((void *)nb, buf_pages);
        if (nh) kernel_free_pages_ptr((void *)nh, vec_pages);
        if (nt) kernel_free_pages_ptr((void *)nt, vec_pages);
        return 0;
    }
    u32 flags;
    __asm__ __volatile__("pushf; pop %0; cli" : "=r"(flags));
    int old = kbd_console_count;
    volatile u8 (*ob)[64] = kbd_buf;
    volatile u8 *oh = kbd_head;
    volatile u8 *ot = kbd_tail;
    for (int i = 0; i < n; i++) {
        nh[i] = nt[i] = 0;
        for (int j = 0; j < 64; j++)
            nb[i][j] = 0;
    }
    if (ob) {
        for (int i = 0; i < old; i++) {
            nh[i] = oh[i];
            nt[i] = ot[i];
            for (int j = 0; j < 64; j++)
                nb[i][j] = ob[i][j];
        }
    }
    kbd_buf = nb;
    kbd_head = nh;
    kbd_tail = nt;
    kbd_console_count = n;
    if (ob) {
        kernel_free_pages_ptr((void *)ob, ((u32)old * 64u + 4095u) / 4096u);
        kernel_free_pages_ptr((void *)oh, ((u32)old + 4095u) / 4096u);
        kernel_free_pages_ptr((void *)ot, ((u32)old + 4095u) / 4096u);
    }
    irq_restore_flags(flags);
    return 1;
}
static volatile u8 gui_special_pending;
static int current_console_id(void);
static int console_has_live_task(int cid);
static void sched_yield(void);
static void module_wait_key(int cid);
static u8 shift_down, extended, alt_down;
static volatile u8 ctrl_down, ctrl_seen;

static volatile u8 mouse_cycle;
static volatile u8 mouse_packet[4];
static volatile u8 mouse_packet_size = 3;
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

static inline void wrmsr(u32 msr, u32 lo, u32 hi) {
    __asm__ __volatile__("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

static inline void rdmsr(u32 msr, u32 *lo, u32 *hi) {
    __asm__ __volatile__("rdmsr" : "=a"(*lo), "=d"(*hi) : "c"(msr));
}

static int cpu_has_mtrr(void) {
    u32 a = 1, b, c, d;
    __asm__ __volatile__("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    return (d & (1u << 12)) != 0;
}

/* Mark a 4 MiB-aligned physical span as write-combining if the CPU exposes
   a free variable MTRR and WC support. */
static int mtrr_wc_4m(u32 phys) {
    if (!cpu_has_mtrr())
        return 0;
    u32 cap_lo, cap_hi;
    rdmsr(0xFE, &cap_lo, &cap_hi);
    if (!(cap_lo & (1u << 10)))
        return 0;
    int count = (int)(cap_lo & 0xFF);
    if (count > 8)
        count = 8;
    for (int i = 0; i < count; i++) {
        u32 base_lo, base_hi, mask_lo, mask_hi;
        rdmsr(0x200u + (u32)i * 2u, &base_lo, &base_hi);
        rdmsr(0x201u + (u32)i * 2u, &mask_lo, &mask_hi);
        if (mask_lo & 0x800u)
            continue;
        base_lo = phys & 0xFFC00000u;
        wrmsr(0x200u + (u32)i * 2u, base_lo | 6u, 0);
        wrmsr(0x201u + (u32)i * 2u, 0xFFC00800u, 0);
        u32 def_lo, def_hi;
        rdmsr(0x2FF, &def_lo, &def_hi);
        wrmsr(0x2FF, def_lo | 0x800u, def_hi);
        return 1;
    }
    return 0;
}

/* COM1, 115200 8N1. */
#define COM1 0x3F8
static u8 serial_ready;

static void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
    serial_ready = 1;
}

static void serial_putc(char ch) {
    if (!serial_ready)
        return;
    while (!(inb(COM1 + 5) & 0x20)) {}
    outb(COM1, (u8)ch);
}

void serial_puts(const char *s) {
    while (*s)
        serial_putc(*s++);
}

static int serial_getc(void) {
    if (!serial_ready)
        return -1;
    while (!(inb(COM1 + 5) & 0x01)) {
        __asm__ __volatile__("sti");
        __asm__ __volatile__("hlt");
    }
    return (int)inb(COM1);
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

static void mouse_send(u8 b) {
    kbc_cmd(0xD4);
    kbc_data(b);
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

    mouse_send(0xF6);       /* set defaults before wheel probing */
    (void)kbc_read();

    /* IntelliMouse magic sample-rate sequence, then read Device ID. */
    const u8 rates[3] = { 200, 100, 80 };
    for (int i = 0; i < 3; i++) {
        mouse_send(0xF3);
        (void)kbc_read();
        mouse_send(rates[i]);
        (void)kbc_read();
    }
    mouse_send(0xF2);
    (void)kbc_read();
    int id = kbc_read();
    if (id == 3)
        mouse_packet_size = 4;
    else
        mouse_packet_size = 3;
    serial_puts("[mouse] id=");
    serial_putc(id >= 0 && id <= 9 ? (char)('0' + id) : '?');
    serial_puts(" packet=");
    serial_putc((char)('0' + mouse_packet_size));
    serial_puts("\r\n");

    mouse_send(0xF4);       /* enable data reporting */
    (void)kbc_read();
    mouse_cycle = 0;
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

static void wake_kbd_tasks(int c);

static void kbd_push(u8 ch) {
    int c = gui_focused_console();
    if (ch == 3)
        task_signal_console(c, SIGINT);
    if (c < 0 || c >= kbd_console_count)
        c = 0;
    u8 next = (u8)((kbd_head[c] + 1) & 63);
    if (next != kbd_tail[c]) {
        kbd_buf[c][kbd_head[c]] = ch;
        kbd_head[c] = next;
    }
    wake_kbd_tasks(c);
}

#define KEY_UP    0xF7
#define KEY_DOWN  0xF8
#define KEY_LEFT  0xF9
#define KEY_RIGHT 0xFA
#define KEY_HOME  0xFB
#define KEY_END   0xFC
#define KEY_DEL   0xFD
#define KEY_PGUP  0xFE
#define KEY_PGDN  0xFF

static char shift_char(char ch) {
    switch (ch) {
        case '1': return '!'; case '2': return '@'; case '3': return '#';
        case '4': return '$'; case '5': return '%'; case '6': return '^';
        case '7': return '&'; case '8': return '*'; case '9': return '(';
        case '0': return ')'; case '-': return '_'; case '=': return '+';
        case '[': return '{'; case ']': return '}'; case '\\': return '|';
        case ';': return ':'; case '\'': return '"'; case '`': return '~';
        case ',': return '<'; case '.': return '>'; case '/': return '?';
        default:
            if (ch >= 'a' && ch <= 'z')
                return (char)(ch - 32);
            return ch;
    }
}

static void keyboard_scancode(u8 sc) {
    if (sc == 0xE0) {
        extended = 1;
        return;
    }
    if (extended) {
        extended = 0;
        if (sc == 0x38) {
            alt_down = 1;
            return;
        }
        if (sc == 0xB8) {
            alt_down = 0;
            return;
        }
        if (sc & 0x80)
            return;
        if (sc == 0x48) kbd_push(KEY_UP);
        else if (sc == 0x50) kbd_push(KEY_DOWN);
        else if (sc == 0x4B) kbd_push(KEY_LEFT);
        else if (sc == 0x4D) kbd_push(KEY_RIGHT);
        else if (sc == 0x47) kbd_push(KEY_HOME);
        else if (sc == 0x4F) kbd_push(KEY_END);
        else if (sc == 0x53) kbd_push(KEY_DEL);
        else if (sc == 0x49) kbd_push(KEY_PGUP);
        else if (sc == 0x51) kbd_push(KEY_PGDN);
        return;
    }
    if (sc & 0x80) {
        u8 make = sc & 0x7F;
        if (make == 0x2A || make == 0x36)
            shift_down = 0;
        else if (make == 0x38)
            alt_down = 0;
        else if (make == 0x1D) {
            ctrl_down = 0;
            ctrl_seen = 1;
        }
        return;
    }
    if (sc == 0x38) {
        alt_down = 1;
        return;
    }
    if (sc == 0x0F && alt_down) {
        gui_special_pending = shift_down ? 8 : 7;
        return;
    }
    if (sc == 0x2A || sc == 0x36) {
        shift_down = 1;
        return;
    }
    if (sc == 0x1D) {
        ctrl_down = 1;
        ctrl_seen = 1;
        return;
    }
    if (sc == 0x3B) { gui_special_pending = 1; return; }
    if (sc == 0x3C) { gui_special_pending = 2; return; }
    if (sc == 0x3D) { gui_special_pending = 3; return; }
    if (sc == 0x3E) { gui_special_pending = 4; return; }
    if (sc == 0x3F) { gui_special_pending = 5; return; }
    if (sc == 0x40) { gui_special_pending = 6; return; }
    if (sc == 0x41) { gui_special_pending = 9; return; }
    if (sc == 0x42) { gui_special_pending = 10; return; }

    char ch = keymap[sc];
    if (!ch)
        return;
    if (ctrl_down) {
        if (ch >= 'a' && ch <= 'z') {
            kbd_push((u8)(ch - 'a' + 1));
            return;
        }
        if (ch == '[') {
            kbd_push(27);
            return;
        }
        if (ch == '\\') {
            kbd_push(28);
            return;
        }
    }
    if (shift_down)
        ch = shift_char(ch);
    kbd_push((u8)ch);
}

static void mouse_byte(u8 data) {
    u8 cycle = mouse_cycle;
    if (cycle == 0 && !(data & 0x08)) {
        mouse_cycle = 0;
        return;
    }
    mouse_packet[cycle] = data;
    cycle++;
    if (cycle < mouse_packet_size) {
        mouse_cycle = cycle;
        return;
    }
    cycle = 0;
    mouse_cycle = 0;

    u8 flags = mouse_packet[0];
    if (flags & 0xC0)
        return;
    int dx = (signed char)mouse_packet[1];
    int dy = (signed char)mouse_packet[2];
    int nx = mouse_x + dx;
    int ny = mouse_y - dy;
    int max_y = scr_h - 1;
    if (nx < 0) nx = 0;
    if (nx >= scr_w) nx = scr_w - 1;
    if (ny < 0) ny = 0;
    if (ny > max_y) ny = max_y;
    mouse_x = nx;
    mouse_y = ny;
    mouse_buttons = flags & 7;
    mouse_event = 1;

    if (mouse_packet_size == 4) {
        int dz = mouse_packet[3] & 0x0F;
        if (dz & 8)
            dz -= 16;
        if (dz)
            gui_mouse_wheel(dz);
    }
}

void irq1_handler(void) {
    u8 st = inb(0x64);
    u8 sc = inb(0x60);

    if (st & 0x20)
        mouse_byte(sc);
    else
        keyboard_scancode(sc);
    outb(0x20, 0x20);
}

void irq6_handler(void) {
    fdc_on_irq();
    wake_fdc_tasks();
    outb(0x20, 0x20);
}

/* -----------------------------------------------------------------------------
 * Mouse
 * ----------------------------------------------------------------------------- */
void irq12_handler(void) {
    u8 st = inb(0x64);
    u8 data = inb(0x60);

    if (st & 0x20)
        mouse_byte(data);
    else
        keyboard_scancode(data);
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
extern void irq6_stub(void);
extern void irq12_stub(void);
extern void syscall_stub(void);
extern void isr_default(void);
extern u32 exc_stubs[32];
extern void run_user(u32 entry, u32 user_stack);
extern void user_exit_to_kernel(void);
extern void scheduler_start(u32 first_task_esp);
extern void scheduler_exit_to_kernel(void);
extern void switch_to(u32 task_esp);
extern void swtch(u32 *old_kesp, u32 new_kesp);
extern void task_first_entry(void);
extern const u8 user_blob_start[];
extern const u8 user_blob_end[];

static void idt_set_flags(int n, u32 handler, u8 flags) {
    idt[n].base_lo = (u16)(handler & 0xFFFF);
    idt[n].selector = 0x08;
    idt[n].zero = 0;
    idt[n].flags = flags;
    idt[n].base_hi = (u16)(handler >> 16);
}

static void idt_set(int n, u32 handler) {
    idt_set_flags(n, handler, 0x8E);
}

static void idt_set_task_gate(int n, u16 selector, u8 flags) {
    idt[n].base_lo = 0;
    idt[n].selector = selector;
    idt[n].zero = 0;
    idt[n].flags = flags;
    idt[n].base_hi = 0;
}

static void sched_timer_tick(u32 *frame);

static volatile u8 runtime_busy;

static void gui_poll_runtime(void) {
    static u32 last_frame;
    static u32 last_blink;
    int changed = 0;
    if (gui_special_pending) {
        u8 special = gui_special_pending;
        gui_special_pending = 0;
        gui_special(special);
        changed = 1;
    }
    if (mouse_event) {
        mouse_event = 0;
        gui_mouse_event(mouse_x, mouse_y, mouse_buttons);
        changed = 1;
    }

    int cid = gui_focused_console();
    if (cid < 0 || cid >= kbd_console_count)
        cid = 0;
    int ring0_runtime = runtime_busy && !scheduler_active;
    if (!scheduler_active && !ring0_runtime &&
        (gui_runtime_keyboard_wanted() || !console_has_live_task(cid))) {
        while (kbd_tail[cid] != kbd_head[cid]) {
            u8 ch = kbd_buf[cid][kbd_tail[cid]];
            kbd_tail[cid] = (u8)((kbd_tail[cid] + 1) & 63);
            gui_key((char)ch);
            changed = 1;
        }
    }

    u32 blink = ticks / 50;
    int periodic = blink != last_blink;
    if (periodic)
        last_blink = blink;
    if (changed || periodic) {
        gui_redraw();
        last_frame = ticks;
    } else if (gui_dirty_pending() && (u32)(ticks - last_frame) >= 2) {
        gui_redraw();
        last_frame = ticks;
    }
}

void irq0_handler(u32 *frame) {
    ticks++;
    outb(0x20, 0x20);
    if (runtime_busy)
        gui_poll_runtime();
    sched_timer_tick(frame);
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

    outb(0x21, 0xB8);       /* IRQ0, IRQ1, cascade IRQ2, IRQ6 (FDC) */
    outb(0xA1, 0xEF);       /* IRQ12 (mouse) */
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
    for (int i = 0; i < 32; i++)
        idt_set(i, exc_stubs[i]);
    idt_set_task_gate(8, 0x30, 0x85);
    idt_set(0x20, (u32)irq0_stub);
    idt_set(0x21, (u32)irq1_stub);
    idt_set(0x26, (u32)irq6_stub);
    idt_set(0x2C, (u32)irq12_stub);

    idtp.limit = (u16)(sizeof(idt) - 1);
    idtp.base = (u32)&idt;
    __asm__ __volatile__("lidt %0" : : "m"(idtp));

    pic_remap();
    pit_init();
}

/* -----------------------------------------------------------------------------
 * GDT / TSS / Ring 3
 * ----------------------------------------------------------------------------- */
struct gdt_entry {
    u16 limit_lo;
    u16 base_lo;
    u8  base_mid;
    u8  access;
    u8  gran;
    u8  base_hi;
} __attribute__((packed));

struct gdt_ptr_t {
    u16 limit;
    u32 base;
} __attribute__((packed));

struct tss_entry {
    u32 prev_tss;
    u32 esp0;
    u32 ss0;
    u32 esp1;
    u32 ss1;
    u32 esp2;
    u32 ss2;
    u32 cr3;
    u32 eip;
    u32 eflags;
    u32 eax, ecx, edx, ebx, esp, ebp, esi, edi;
    u32 es, cs, ss, ds, fs, gs;
    u32 ldt;
    u16 trap;
    u16 iomap_base;
} __attribute__((packed));

static struct gdt_entry gdt[7];
static struct gdt_ptr_t gdtp;
static struct tss_entry tss;
static struct tss_entry df_tss;
static u8 df_stack[4096] __attribute__((aligned(16)));
static u8 tss_loaded;
static u8 gdt_ready;

#define VBE_VIRT_BASE 0xE0000000u
#define VBE_PDE_INDEX (VBE_VIRT_BASE >> 22)
static u32 vbe_pde_entry;
int vbe_wc;

struct vbe_boot_info {
    u8  magic[4];
    u16 width;
    u16 height;
    u16 pitch;
    u8  bpp;
    u8  active;
    u32 phys_base;
} __attribute__((packed));

static void gdt_set(int n, u32 base, u32 limit, u8 access, u8 gran) {
    gdt[n].limit_lo = (u16)(limit & 0xFFFF);
    gdt[n].base_lo = (u16)(base & 0xFFFF);
    gdt[n].base_mid = (u8)((base >> 16) & 0xFF);
    gdt[n].access = access;
    gdt[n].gran = (u8)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[n].base_hi = (u8)((base >> 24) & 0xFF);
}

static void gdt_install(void) {
    if (gdt_ready)
        return;
    for (int i = 0; i < 7; i++) {
        gdt[i].limit_lo = 0;
        gdt[i].base_lo = 0;
        gdt[i].base_mid = 0;
        gdt[i].access = 0;
        gdt[i].gran = 0;
        gdt[i].base_hi = 0;
    }
    gdt_set(1, 0, 0xFFFFF, 0x9A, 0xCF);     /* kernel code */
    gdt_set(2, 0, 0xFFFFF, 0x92, 0xCF);     /* kernel data */
    gdt_set(3, 0, 0xFFFFF, 0xFA, 0xCF);     /* user code, DPL3 */
    gdt_set(4, 0, 0xFFFFF, 0xF2, 0xCF);     /* user data, DPL3 */

    u8 *p = (u8 *)&tss;
    for (unsigned i = 0; i < sizeof(tss); i++)
        p[i] = 0;
    tss.ss0 = 0x10;
    tss.esp0 = 0x00080000;                  /* separate ring0 stack */
    tss.iomap_base = (u16)sizeof(tss);
    gdt_set(5, (u32)&tss, (u32)(sizeof(tss) - 1), 0x89, 0x00);
    gdt_set(6, (u32)&df_tss, (u32)(sizeof(df_tss) - 1), 0x89, 0x00);

    gdtp.limit = (u16)(sizeof(gdt) - 1);
    gdtp.base = (u32)&gdt;
    __asm__ __volatile__(
        "lgdt %0\n\t"
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "movw %%ax, %%ss\n\t"
        "ljmp $0x08, $1f\n\t"
        "1:\n\t"
        : : "m"(gdtp) : "eax", "memory"
    );

    if (!tss_loaded) {
        u16 tr = 0x28;
        __asm__ __volatile__("ltr %0" : : "rm"(tr));
        tss_loaded = 1;
    }
    gdt_ready = 1;
}

/* -----------------------------------------------------------------------------
 * Paging: identity-map the first 8 MB, mark the 4 MB user pages U/S.
 * Physical memory frames are discovered from the boot E820 map.  The
 * supervisor direct map covers 4..512 MiB at 16 MiB.
 * ----------------------------------------------------------------------------- */
#define FRAME_POOL_START  0x00800000u
#define LOW_POOL_END      0x01000000u
#define HIGH_POOL_START   0x01000000u
#define MAX_PHYS          0x80000000u
#define MAX_FRAME_PAGES   (MAX_PHYS / 0x1000u)
#define DIRECT_PHYS_START 0x00400000u
#define DIRECT_BASE       0xF0000000u
#define LOW_DIRECT_PDE    (DIRECT_BASE >> 22)
#define HIGH_BASE         0x40000000u
#define LOW_DIRECT_PT_COUNT 3u
#define HIGH_PSE_COUNT    (MAX_PHYS / 0x400000u)

static u32 page_directory[1024] __attribute__((aligned(4096)));
static u32 page_table0[1024] __attribute__((aligned(4096)));
static u32 page_table1[1024] __attribute__((aligned(4096)));
static u32 direct_pt[LOW_DIRECT_PT_COUNT][1024] __attribute__((aligned(4096)));

static void install_high_pse(u32 *pd) {
    for (u32 i = 0; i < HIGH_PSE_COUNT; i++)
        pd[(HIGH_BASE >> 22) + i] = i * 0x400000u | 0x083u;
}

static void enable_pse(void) {
    u32 cr4;
    __asm__ __volatile__("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= 0x00000010u;
    __asm__ __volatile__("mov %0, %%cr4" : : "r"(cr4));
}

static void paging_install(void) {
    for (int i = 0; i < 1024; i++) {
        page_directory[i] = 0;
        page_table0[i] = 0;
        page_table1[i] = 0;
    }

    page_directory[0] = (u32)page_table0 | 0x003;   /* supervisor 0-4 MB */
    page_directory[1] = (u32)page_table1 | 0x007;   /* user-capable 4-8 MB */
    for (u32 i = 0; i < LOW_DIRECT_PT_COUNT; i++)
        page_directory[LOW_DIRECT_PDE + i] = (u32)direct_pt[i] | 0x003;
    install_high_pse(page_directory);
    enable_pse();
    if (vbe_active)
        page_directory[VBE_PDE_INDEX] = vbe_pde_entry;

    for (int i = 0; i < 1024; i++) {
        page_table0[i] = (u32)(i * 0x1000) | 0x003;
        page_table1[i] = (u32)(0x400000 + i * 0x1000) | 0x003;
    }
    for (int i = 0; i < 4; i++)
        page_table1[i] |= 0x004;                    /* user code + stack */

    /* Switch to the new identity mapping before touching 0x400000: under a
       task page directory that virtual address points at the task's page. */
    __asm__ __volatile__("mov %0, %%cr3" : : "r"((u32)page_directory));

    u32 len = (u32)(user_blob_end - user_blob_start);
    if (len > 4096)
        len = 4096;
    u8 *dst = (u8 *)0x400000;
    for (u32 i = 0; i < len; i++)
        dst[i] = user_blob_start[i];

    u32 cr0;
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;
    __asm__ __volatile__("mov %0, %%cr0" : : "r"(cr0));
}

/* -----------------------------------------------------------------------------
 * Read-only program pack, loaded by the boot sector at 0x70000.
 * ----------------------------------------------------------------------------- */
#define PACK_BASE 0x00070000u
#define PACK_SIZE 32768u
#define PACK_MAX_ENTRIES 32

struct pack_header {
    u8 magic[4];
    u16 count;
    u16 reserved;
} __attribute__((packed));

struct pack_entry {
    char name[16];
    u32 offset;
    u32 size;
} __attribute__((packed));

static u8 pack_ready;
static u16 pack_count;

static struct pack_entry *pack_entry_at(int i) {
    return (struct pack_entry *)(PACK_BASE + sizeof(struct pack_header) +
                                 (u32)i * sizeof(struct pack_entry));
}

static u8 upper_char(u8 c) {
    if (c >= 'a' && c <= 'z')
        return (u8)(c - 32);
    return c;
}

static int pack_init(void) {
    if (pack_ready)
        return 1;
    struct pack_header *h = (struct pack_header *)PACK_BASE;
    if (h->magic[0] != 'M' || h->magic[1] != 'P' ||
        h->magic[2] != 'K' || h->magic[3] != '1')
        return 0;
    if (h->count == 0 || h->count > PACK_MAX_ENTRIES)
        return 0;
    for (int i = 0; i < h->count; i++) {
        struct pack_entry *e = pack_entry_at(i);
        if (e->offset > PACK_SIZE || e->size > PACK_SIZE - e->offset)
            return 0;
    }
    pack_count = h->count;
    pack_ready = 1;
    return 1;
}

static int pack_name_eq(const struct pack_entry *e, const char *name) {
    for (int i = 0; i < 16; i++) {
        u8 a = (u8)e->name[i];
        u8 b = (u8)name[i];
        if (a == 0 && b == 0)
            return 1;
        if (a == 0 || b == 0)
            return 0;
        if (upper_char(a) != upper_char(b))
            return 0;
    }
    return 1;
}

static int pack_find(const char *name) {
    if (!pack_init())
        return -1;
    for (int i = 0; i < pack_count; i++) {
        if (pack_name_eq(pack_entry_at(i), name))
            return i;
    }
    return -1;
}

void list_programs(void) {
    if (!pack_init()) {
        gui_sys_print("[pack] no valid program pack.\n");
        return;
    }
    gui_sys_print("[pack] programs:\n");
    for (int i = 0; i < pack_count; i++) {
        struct pack_entry *e = pack_entry_at(i);
        char name[17];
        for (int j = 0; j < 16; j++)
            name[j] = e->name[j];
        name[16] = 0;
        gui_sys_print("  \x1b[36m");
        gui_sys_print(name);
        gui_sys_print("\x1b[0m\n");
    }
}

/* -----------------------------------------------------------------------------
 * Runtime MiniFS at LBA 200: header, directory, file data.
 * ----------------------------------------------------------------------------- */
#define FS_LBA 328u
#define FS_MAX_ENTRIES 64
#define FS_MAX_FILE 4096

struct fs_header {
    u8 magic[4];
    u16 count;
    u16 reserved;
} __attribute__((packed));

struct fs_entry {
    char name[16];
    u32 start_lba;
    u32 size;
} __attribute__((packed));

static u8 fs_dir[2048];
static u8 fs_sector[512];

static int fs_name_eq(const struct fs_entry *e, const char *name) {
    for (int i = 0; i < 16; i++) {
        u8 a = (u8)e->name[i];
        u8 b = (u8)name[i];
        if (a == 0 && b == 0)
            return 1;
        if (a == 0 || b == 0)
            return 0;
        if (upper_char(a) != upper_char(b))
            return 0;
    }
    return 1;
}

static int fs_load_dir(void) {
    if (!fdc_read_sector(FS_LBA, fs_dir))
        return 0;
    struct fs_header *h = (struct fs_header *)fs_dir;
    if (h->magic[0] != 'M' || h->magic[1] != 'F' ||
        h->magic[2] != 'S' || h->magic[3] != '1')
        return 0;
    if (h->count == 0 || h->count > FS_MAX_ENTRIES)
        return 0;
    u32 dir_sectors = (sizeof(*h) + (u32)h->count * sizeof(struct fs_entry) + 511) / 512;
    if (dir_sectors > 4)
        return 0;
    for (u32 s = 1; s < dir_sectors; s++) {
        if (!fdc_read_sector(FS_LBA + s, fs_sector))
            return 0;
        for (int i = 0; i < 512; i++)
            fs_dir[s * 512 + i] = fs_sector[i];
    }
    return 1;
}

void list_disk_files(void) {
    if (!kernel_fdc_lock())
        return;
    if (!fs_load_dir()) {
        kernel_fdc_unlock();
        gui_sys_print("[disk] MiniFS read failed.\n");
        return;
    }
    struct fs_header *h = (struct fs_header *)fs_dir;
    struct fs_entry *entries = (struct fs_entry *)(fs_dir + sizeof(*h));
    gui_sys_print("\x1b[36m[disk] MiniFS files:\x1b[0m\n");
    for (int i = 0; i < h->count; i++) {
        char name[17];
        for (int j = 0; j < 16; j++)
            name[j] = entries[i].name[j];
        name[16] = 0;
        gui_sys_print("  \x1b[36m");
        gui_sys_print(name);
        gui_sys_print("\x1b[0m\n");
    }
    kernel_fdc_unlock();
}

/* -----------------------------------------------------------------------------
 * Kernel-module ABI for the MicroPython bring-up.
 * Module is linked at 0x100000; heap at 0x200000; size max 1 MB.
 * ----------------------------------------------------------------------------- */
#define MODULE_BASE 0x00100000u
#define MODULE_MAX  0x00100000u
#define MODULE_HEAP 0x00200000u
#define MODULE_HEAP_SIZE 0x00100000u

struct module_header {
    u8 magic[4];
    u32 entry_offset;
    u32 image_size;
    u32 bss_size;
    u32 heap_size;
    u32 ro_size;
} __attribute__((packed));

struct minios_api {
    u32 version;
    void (*puts)(const char *s);
    void (*putc)(char c);
    u32 (*ticks_ms)(void);
    int (*getchar)(void);
    u8 *heap;
    u32 heap_size;
    void (*write)(const char *s, u32 len);
};

static int fs_load_file_to_locked(const char *name, u8 *dst, u32 max, u32 *out_size) {
    if (!fs_load_dir())
        return 0;
    struct fs_header *h = (struct fs_header *)fs_dir;
    struct fs_entry *entries = (struct fs_entry *)(fs_dir + sizeof(*h));
    for (int i = 0; i < h->count; i++) {
        struct fs_entry *e = &entries[i];
        if (!fs_name_eq(e, name))
            continue;
        if (e->size > max)
            return 0;
        u32 sectors = (e->size + 511) / 512;
        u32 done = 0;
        for (u32 s = 0; s < sectors; s++) {
            if (!fdc_read_sector(e->start_lba + s, fs_sector))
                return 0;
            u32 chunk = e->size - done;
            if (chunk > 512)
                chunk = 512;
            for (u32 j = 0; j < chunk; j++)
                dst[done + j] = fs_sector[j];
            done += chunk;
        }
        *out_size = e->size;
        return 1;
    }
    return 0;
}

static int fs_load_file_to(const char *name, u8 *dst, u32 max, u32 *out_size) {
    if (!kernel_fdc_lock())
        return 0;
    int r = fs_load_file_to_locked(name, dst, max, out_size);
    kernel_fdc_unlock();
    return r;
}

static void module_puts(const char *s) {
    gui_sys_print(s);
}

static void module_write(const char *s, u32 len) {
    gui_sys_write(s, len);
}

static void module_putc(char c) {
    char b[2];
    b[0] = c;
    b[1] = 0;
    gui_sys_print(b);
}

static u32 module_ticks_ms(void) {
    return ticks * 10;
}

static int module_getchar(void) {
    static u8 seq[4];
    static u8 seq_pos, seq_len;
    for (;;) {
        if (seq_pos < seq_len)
            return (int)seq[seq_pos++];
        if (gui_canvas_keyboard()) {
            sched_yield();
            continue;
        }
        int cid = current_console_id();
        if (cid < 0 || cid >= kbd_console_count)
            cid = 0;
        while (kbd_tail[cid] != kbd_head[cid]) {
            u8 ch = kbd_buf[cid][kbd_tail[cid]];
            kbd_tail[cid] = (u8)((kbd_tail[cid] + 1) & 63);
            if (ch >= 0xF1 && ch <= 0xF6)
                continue;
            if (ch == KEY_PGUP || ch == KEY_PGDN)
                continue;

            seq_len = 0;
            seq_pos = 0;
            seq[0] = 27;
            seq[1] = '[';
            if (ch == KEY_UP) {
                seq[2] = 'A'; seq_len = 3;
            } else if (ch == KEY_DOWN) {
                seq[2] = 'B'; seq_len = 3;
            } else if (ch == KEY_RIGHT) {
                seq[2] = 'C'; seq_len = 3;
            } else if (ch == KEY_LEFT) {
                seq[2] = 'D'; seq_len = 3;
            } else if (ch == KEY_HOME) {
                seq[2] = 'H'; seq_len = 3;
            } else if (ch == KEY_END) {
                seq[2] = 'F'; seq_len = 3;
            } else if (ch == KEY_DEL) {
                seq[2] = '3'; seq[3] = '~'; seq_len = 4;
            } else {
                return (int)ch;
            }
            seq_pos = 1;
            return (int)seq[0];
        }
        module_wait_key(cid);
    }
}

int run_module(const char *name) {
    u32 size = 0;
    if (!fs_load_file_to(name, (u8 *)MODULE_BASE, MODULE_MAX, &size)) {
        gui_sys_print("[mod] load failed.\n");
        return 0;
    }

    struct module_header *h = (struct module_header *)MODULE_BASE;
    if (h->magic[0] != 'M' || h->magic[1] != 'P' ||
        h->magic[2] != 'M' || h->magic[3] != '1') {
        gui_sys_print("[mod] bad header.\n");
        return 0;
    }
    if (h->image_size > size || h->entry_offset >= h->image_size ||
        h->image_size > MODULE_MAX || h->bss_size > MODULE_MAX - h->image_size) {
        gui_sys_print("[mod] bad sizes.\n");
        return 0;
    }

    u8 *bss = (u8 *)(MODULE_BASE + h->image_size);
    for (u32 i = 0; i < h->bss_size; i++)
        bss[i] = 0;

    u32 heap_size = h->heap_size;
    if (heap_size == 0 || heap_size > MODULE_HEAP_SIZE)
        heap_size = MODULE_HEAP_SIZE;

    struct minios_api api;
    api.version = 1;
    api.puts = module_puts;
    api.putc = module_putc;
    api.ticks_ms = module_ticks_ms;
    api.getchar = module_getchar;
    api.heap = (u8 *)MODULE_HEAP;
    api.heap_size = heap_size;
    api.write = module_write;

    void (*entry)(struct minios_api *) =
        (void (*)(struct minios_api *))(MODULE_BASE + h->entry_offset);
    runtime_busy = 1;
    entry(&api);
    runtime_busy = 0;
    gui_sys_print("[mod] returned to kernel.\n");
    return 1;
}

/* -----------------------------------------------------------------------------
 * Tasks: up to four slots, each with its own page directory.
 * ----------------------------------------------------------------------------- */
#define TASK_INIT_CAP 16
#define TASK_FREE    0
#define TASK_READY   1
#define TASK_RUNNING 2
#define TASK_DONE    3
#define TASK_BLOCKED 4
#define TASK_STOPPED 5
#define WAIT_NONE    0xFFFFFFFFu
#define TIME_SLICE_TICKS 1

/* One large Ring 3 task for the MicroPython-class layout. */
#define BIG_CODE_PHYS    0x00100000u
#define BIG_CODE_MAX     0x00080000u
#define BIG_USER_CODE    0x00400000u
#define BIG_USER_STACK   0x00480000u
#define BIG_USER_HEAP    0x00500000u
#define HEAP_VIRT_BASE   0x00500000u
#define USER_PT_COUNT    20
#define USER_END         (BIG_USER_CODE + USER_PT_COUNT * 0x400000u)
#define HEAP_VIRT_MAX    USER_END
#define BIG_STACK_PAGES  16
#define BIG_HEAP_PAGES   256
#define BIG_STACK_SIZE   (BIG_STACK_PAGES * 0x1000u)
#define BIG_HEAP_SIZE    (BIG_HEAP_PAGES * 0x1000u)


struct task {
    u32 esp;
    u32 kesp;
    u32 *tf;
    u32 cr3;
    u32 *pd;
    u32 *upt[USER_PT_COUNT];
    u32 upt_phys[USER_PT_COUNT];
    u32 pd_phys;
    u8 *kstack;
    u16 kstack_pages;
    struct fd_entry *fds;
    u32 kstack_top;
    u32 code_phys;
    u32 stack_phys;
    u32 stack_va;
    u32 heap_phys;
    u16 code_pages;
    u16 stack_pages;
    u32 heap_pages;
    u8 shared_ro;
    u8  state;
    char name[16];
    u32 pid;
    u32 parent_pid;
    u32 waiting_pid;
    u32 wait_pipe;
    u32 wait_status;
    u32 sleep_until;
    u32 exit_code;
    u32 cpu_ticks;
    u8 console_id;
    u8 reap_pending;
    u8 stop_prev_state;
    u8 kill_pending;
    u8 sig_pending;
    u8 frames_freed;
    u32 sig_ignored;
    u8 wait_kbd;
    u8 wait_fdc;
    u8 wait_fdc_lock;
    u8 wait_pipe_write;
    u8 pipe_w_id;
    u8 pipe_r_id;
    u8 fpu[108];
};

static struct task *tasks;
static int last_created_slot = -1;
static int task_cap;
static int current_task;
static u32 quantum_ticks;
static u8 task0_kstack[8192] __attribute__((aligned(16)));
static u32 shared_ro_phys, shared_ro_pages, shared_ro_image, shared_ro_refs;

static void wake_fdc_tasks(void) {
    for (int t = 0; t < task_cap; t++) {
        if (tasks[t].state == TASK_BLOCKED && tasks[t].wait_fdc) {
            tasks[t].wait_fdc = 0;
            tasks[t].sleep_until = 0;
            tasks[t].state = TASK_READY;
        }
    }
}

void kernel_fdc_wake(void) {
    wake_fdc_tasks();
}

static int fdc_lock_owner = -1;
static int fdc_lock_depth;
static u8 fdc_trace;
static u32 fdc_lock_contends;
static u32 fdc_lock_acquires;

void kernel_fdc_trace(int on) {
    fdc_trace = on ? 1 : 0;
}

u32 kernel_fdc_owner(void) {
    return fdc_lock_owner > 0 ? tasks[fdc_lock_owner].pid : 0;
}

u32 kernel_fdc_contends(void) {
    return fdc_lock_contends;
}

static void wake_fdc_lock_waiter(void) {
    for (int t = 0; t < task_cap; t++) {
        if (tasks[t].state == TASK_BLOCKED && tasks[t].wait_fdc_lock) {
            tasks[t].wait_fdc_lock = 0;
            tasks[t].state = TASK_READY;
            break;
        }
    }
}

int kernel_fdc_lock(void) {
    if (current_task < 0 || current_task >= task_cap)
        return 0;
    for (;;) {
        u32 flags = irq_save_flags();
        if (fdc_lock_owner < 0 || fdc_lock_owner == current_task) {
            fdc_lock_owner = current_task;
            fdc_lock_depth++;
            fdc_lock_acquires++;
            if (fdc_trace) {
                serial_puts(current_task == 0 ? "T0 ACQUIRED\r\n" :
                            "LOCK ACQ pid ");
                if (current_task != 0)
                    serial_u32(tasks[current_task].pid);
                serial_puts("\r\n");
            }
            irq_restore_flags(flags);
            return 1;
        }
        if (current_task == 0) {
            fdc_lock_contends++;
            if (fdc_trace) {
                serial_puts("T0 CONTEND owner ");
                serial_u32(tasks[fdc_lock_owner].pid);
                serial_puts("\r\n");
            }
        }
        tasks[current_task].wait_fdc_lock = 1;
        tasks[current_task].state = TASK_BLOCKED;
        if (!sched_block_current()) {
            tasks[current_task].wait_fdc_lock = 0;
            tasks[current_task].state = TASK_READY;
            irq_restore_flags(flags);
            if (current_task == 0) {
                __asm__ __volatile__("sti");
                __asm__ __volatile__("pause");
                continue;
            }
            return 0;
        }
        irq_restore_flags(flags);
    }
}

void kernel_fdc_unlock(void) {
    u32 flags = irq_save_flags();
    if (fdc_lock_owner == current_task) {
        if (fdc_lock_depth > 0)
            fdc_lock_depth--;
        if (fdc_lock_depth == 0) {
            if (fdc_trace) {
                serial_puts("LOCK REL pid ");
                serial_u32(tasks[current_task].pid);
                serial_puts("\r\n");
            }
            fdc_lock_owner = -1;
            wake_fdc_lock_waiter();
        }
    }
    irq_restore_flags(flags);
}

void kernel_fdc_release_owner(int slot) {
    u32 flags = irq_save_flags();
    if (fdc_lock_owner == slot) {
        fdc_lock_owner = -1;
        fdc_lock_depth = 0;
        wake_fdc_lock_waiter();
    }
    irq_restore_flags(flags);
}

int kernel_fdc_wait(int ms) {
    if (!scheduler_active || current_task < 0 || current_task >= task_cap)
        return 0;
    u32 flags = irq_save_flags();
    if (!(flags & 0x200u)) {
        irq_restore_flags(flags);
        return 0;
    }
    tasks[current_task].wait_fdc = 1;
    tasks[current_task].sleep_until = ticks + (u32)(ms / 10 + 1);
    tasks[current_task].state = TASK_BLOCKED;
    if (!sched_block_current()) {
        tasks[current_task].wait_fdc = 0;
        tasks[current_task].sleep_until = 0;
        tasks[current_task].state = TASK_READY;
        irq_restore_flags(flags);
        return 0;
    }
    irq_restore_flags(flags);
    tasks[current_task].wait_fdc = 0;
    tasks[current_task].sleep_until = 0;
    return 1;
}

static void wake_kbd_tasks(int c) {
    for (int t = 1; t < task_cap; t++) {
        if (tasks[t].state == TASK_BLOCKED && tasks[t].wait_kbd == (u8)(c + 1)) {
            tasks[t].wait_kbd = 0;
            tasks[t].state = TASK_READY;
        }
    }
}
static u32 next_pid = 1;
static u8 fpu_default[108];
static int fpu_default_ready;

static void fpu_task_default(u8 *dst) {
    if (!fpu_default_ready) {
        __asm__ __volatile__("fninit");
        __asm__ __volatile__("fsave %0" : "=m"(fpu_default));
        fpu_default_ready = 1;
    }
    for (int i = 0; i < 108; i++)
        dst[i] = fpu_default[i];
}

u32 kernel_current_pid(void) {
    if (scheduler_active && current_task >= 0 && current_task < task_cap)
        return tasks[current_task].pid;
    return 0;
}

int kernel_task_console(void) {
    if (scheduler_active && current_task > 0 && current_task < task_cap)
        return tasks[current_task].console_id;
    return -1;
}

static int current_console_id(void) {
    if (scheduler_active && current_task >= 0 && current_task < task_cap)
        return tasks[current_task].console_id;
    return gui_focused_console();
}

int scheduler_active_now(void) {
    return scheduler_active;
}

int kernel_runtime_busy(void) {
    return runtime_busy;
}

static int console_has_live_task(int cid);

int kernel_console_has_live(int cid) {
    return console_has_live_task(cid);
}

static int console_has_live_task(int cid) {
    for (int t = 1; t < task_cap; t++) {
        if (tasks[t].state != TASK_FREE && tasks[t].state != TASK_DONE &&
            tasks[t].console_id == cid)
            return 1;
    }
    return 0;
}

#define MAX_FDS 32
struct fd_entry {
    u8 used;
    u8 kind;            /* 0 = MiniFS read-only, 1 = RAMFS */
    u8 writable;
    u8 append;
    u8 ram_index;
    u32 start_lba;
    u32 size;
    u32 offset;
    char name[16];
};


#define MAX_RAMFILES 32
#define NPIPES 4
struct kpipe {
    u32 phys;
    u16 head, tail, count;
    u8 open_w, open_r;
    u8 used;
};
static struct kpipe pipes[NPIPES];
#define RAMFILE_MAX_PAGES 64
struct ramfile {
    u8 used;
    char name[16];
    u32 size;
    u32 pages[RAMFILE_MAX_PAGES];
};
static struct ramfile ramfiles[MAX_RAMFILES];
static volatile u8 fs_snapshot_busy;
static int fs_sync_owner = -1;

static u32 task_kernel_pt[1024] __attribute__((aligned(4096)));

/* One bitmap for all physical pages up to 512 MiB. */
static u32 frame_bitmap[(MAX_FRAME_PAGES + 31) / 32];
static u32 frame_alloc_bitmap[(MAX_FRAME_PAGES + 31) / 32];
static u32 frame_total;
static u32 frame_used;
static u8 frames_ready;

/* Never modified while active: safe kernel mapping used during task setup. */
static u32 sched_safe_pd[1024] __attribute__((aligned(4096)));
static u32 sched_safe_pt[1024] __attribute__((aligned(4096)));
static u8 sched_safe_ready;

static void set_cr3(u32 value) {
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(value));
}

static void *direct_ptr(u32 phys) {
    if (phys < LOW_POOL_END)
        return (void *)(DIRECT_BASE + (phys - DIRECT_PHYS_START));
    return (void *)(HIGH_BASE + phys);
}

static void frame_init(void) {
    if (frames_ready)
        return;

    for (u32 t = 0; t < LOW_DIRECT_PT_COUNT; t++) {
        for (int i = 0; i < 1024; i++) {
            u32 phys = DIRECT_PHYS_START + (u32)t * 0x400000u + (u32)i * 0x1000u;
            direct_pt[t][i] = phys | 0x003;
        }
    }

    for (u32 i = 0; i < (MAX_FRAME_PAGES + 31) / 32; i++)
        frame_alloc_bitmap[i] = 0;
    frame_total = 0;
    frame_used = 0;

    volatile u32 *magic = (volatile u32 *)0x1800u;
    volatile u16 *version = (volatile u16 *)0x1804u;
    volatile u16 *status = (volatile u16 *)0x1806u;
    volatile u16 *count = (volatile u16 *)0x1808u;
    volatile u16 *stride = (volatile u16 *)0x180Au;
    int n = 0;
    if (*magic == 0x414D454Du && *version == 1 &&
        (*status == 1 || *status == 2) && *stride == 24) {
        n = *count;
        if (n > 32) n = 32;
    }
    memmap_entry entries[32];
    for (int i = 0; i < n; i++) {
        volatile u32 *e = (volatile u32 *)(0x2000u + (u32)i * 24u);
        entries[i].base_lo = e[0];
        entries[i].base_hi = e[1];
        entries[i].len_lo = e[2];
        entries[i].len_hi = e[3];
        entries[i].type = e[4];
    }
    frame_total = memmap_build(frame_bitmap, MAX_FRAME_PAGES,
                               FRAME_POOL_START / 0x1000u, entries, n);
    if (frame_total == 0)
        serial_puts("[mm] no usable E820 memory\n");
    frames_ready = 1;
}

static inline void frame_invlpg(u32 va) {
    __asm__ __volatile__("invlpg (%0)" : : "r"(va) : "memory");
}

static u32 frame_fail_after;
void kernel_mem_fail_after(u32 n) { frame_fail_after = n; }

static u32 frame_alloc_from(u32 pages, u32 first_page, u32 last_page) {
    if (frame_fail_after) {
        if (frame_fail_after == 1) {
            frame_fail_after = 0;
            return 0;
        }
        frame_fail_after--;
    }
    if (pages == 0 || pages > frame_total - frame_used)
        return 0;
    if (first_page >= last_page)
        return 0;
    u32 start = frame_find_run_from(frame_bitmap, MAX_FRAME_PAGES, pages,
                                    first_page);
    if (start == 0xFFFFFFFFu || start + pages > last_page)
        return 0;
    for (u32 j = 0; j < pages; j++) {
        u32 bit = start + j;
        frame_bitmap[bit / 32] |= (1u << (bit % 32));
        frame_alloc_bitmap[bit / 32] |= (1u << (bit % 32));
        u32 phys = bit * 0x1000u;
        if (phys >= DIRECT_PHYS_START && phys < LOW_POOL_END) {
            u32 idx = (phys - DIRECT_PHYS_START) >> 12;
            u32 *pte = &direct_pt[idx / 1024u][idx % 1024u];
            if (!(*pte & 1)) {
                *pte = phys | 0x003;
                frame_invlpg(DIRECT_BASE + (phys - DIRECT_PHYS_START));
            }
        }
    }
    frame_used += pages;
    return start * 0x1000u;
}

static u32 frame_alloc_pages(u32 pages) {
    return frame_alloc_from(pages, HIGH_POOL_START / 0x1000u,
                            MAX_FRAME_PAGES);
}

static u32 frame_alloc_low_pages(u32 pages) {
    return frame_alloc_from(pages, FRAME_POOL_START / 0x1000u,
                            LOW_POOL_END / 0x1000u);
}

static void frame_free_pages(u32 base, u32 pages) {
    if (pages == 0 || (base & 0xFFFu) || base >= MAX_PHYS)
        return;
    u32 start = base / 0x1000u;
    if (start + pages > MAX_FRAME_PAGES)
        return;
    for (u32 j = 0; j < pages; j++) {
        u32 bit = start + j;
        if (!(frame_bitmap[bit / 32] & (1u << (bit % 32))) ||
            !(frame_alloc_bitmap[bit / 32] & (1u << (bit % 32))))
            return;
    }
    for (u32 j = 0; j < pages; j++) {
        u32 bit = start + j;
        frame_bitmap[bit / 32] &= ~(1u << (bit % 32));
        frame_alloc_bitmap[bit / 32] &= ~(1u << (bit % 32));
    }
    frame_used -= pages;
}

static void frame_zero(u32 phys, u32 pages) {
    u8 *p = (u8 *)direct_ptr(phys);
    for (u32 i = 0; i < pages * 0x1000u; i++)
        p[i] = 0;
}

u32 kernel_free_frames(void) {
    return frame_total - frame_used;
}

u32 kernel_total_frames(void) {
    return frame_total;
}

static int task_guard_pte(u8 *base, int present);

int kernel_mem_selftest(void) {
    u32 initial = kernel_free_frames();
    u32 pages[64];
    u32 allocated = 0;
    int unique_ok = 1;
    for (u32 i = 0; i < 64; i++) {
        u32 p = frame_alloc_pages(1);
        if (!p)
            break;
        for (u32 j = 0; j < allocated; j++)
            if (pages[j] == p)
                unique_ok = 0;
        pages[allocated++] = p;
        u8 *v = (u8 *)direct_ptr(p);
        *(u32 *)v = 0xA5A50000u | i;
        *(u32 *)(v + 4092) = 0x5A5A0000u | i;
    }
    int data_ok = 1;
    for (u32 i = 0; i < allocated; i++) {
        u8 *v = (u8 *)direct_ptr(pages[i]);
        if (*(u32 *)v != (0xA5A50000u | i) ||
            *(u32 *)(v + 4092) != (0x5A5A0000u | i))
            data_ok = 0;
    }
    for (u32 i = 0; i < allocated; i++)
        frame_free_pages(pages[i], 1);
    u32 after_first_free = kernel_free_frames();
    if (allocated)
        frame_free_pages(pages[0], 1); /* must be rejected */
    if (kernel_free_frames() != after_first_free)
        data_ok = 0;

    u32 high_ok = 0;
    u32 high = frame_alloc_pages(2049);
    if (high) {
        u32 last = high + 2048u * 0x1000u;
        if (last >= 0x01000000u) {
            u8 *v = (u8 *)direct_ptr(last);
            *(u32 *)v = 0xC001D00Du;
            high_ok = *(u32 *)v == 0xC001D00Du;
        }
        frame_free_pages(high, 2049);
    }

    u32 high512_ok = 1;
    if (frame_total > 0x20000000u / 0x1000u) {
        high512_ok = 0;
        u32 hp = frame_alloc_from(1, 0x20000000u / 0x1000u, MAX_FRAME_PAGES);
        if (hp) {
            u8 *v = (u8 *)direct_ptr(hp);
            *(u32 *)v = 0x51200000u;
            high512_ok = *(u32 *)v == 0x51200000u;
            frame_free_pages(hp, 1);
        }
    }

    u32 guard_ok = 0, cr3_before = 0, cr3_after = 0;
    u32 gp = frame_alloc_low_pages(1);
    if (gp) {
        __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3_before));
        u8 *gv = (u8 *)direct_ptr(gp);
        if (task_guard_pte(gv, 0)) {
            u32 idx = (gp - DIRECT_PHYS_START) >> 12;
            u32 cleared = !(direct_pt[idx / 1024u][idx % 1024u] & 1);
            if (task_guard_pte(gv, 1)) {
                u32 restored_pte = direct_pt[idx / 1024u][idx % 1024u] & 1;
                guard_ok = cleared && restored_pte;
            }
        }
        __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3_after));
        cr3_before = cr3_before == cr3_after;
        frame_free_pages(gp, 1);
    }
    u32 restored = kernel_free_frames();
    char buf[96];
    char *p = buf;
    const char *s = "initial=";
    while (*s) *p++ = *s++;
    u32 vals[4] = { initial, allocated, restored,
                    (unique_ok && data_ok && high_ok && high512_ok &&
                     guard_ok && cr3_before) };
    for (int k = 0; k < 4; k++) {
        char tmp[12];
        int n = 0;
        u32 v = vals[k];
        if (!v) tmp[n++] = '0';
        while (v) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
        while (n) *p++ = tmp[--n];
        if (k == 0) { *p++=' '; *p++='a'; *p++='l'; *p++='l'; *p++='o'; *p++='c'; *p++='='; }
        if (k == 1) { *p++=' '; *p++='r'; *p++='e'; *p++='s'; *p++='t'; *p++='='; }
        if (k == 2) { *p++=' '; *p++='o'; *p++='k'; *p++='='; }
    }
    *p = 0;
    gui_sys_print("MEMTEST ");
    gui_sys_print(buf);
    gui_sys_print("\n");
    serial_puts("MEMTEST ");
    serial_puts(buf);
    serial_puts("\r\n");
    return allocated == 64 && unique_ok && data_ok && restored == initial;
}

/* Used by the GUI for large optional buffers after paging/direct-map init. */
void *kernel_alloc_pages_zero(u32 pages) {
    u32 phys = frame_alloc_pages(pages);
    if (!phys)
        return 0;
    frame_zero(phys, pages);
    return direct_ptr(phys);
}

void kernel_free_pages_ptr(void *p, u32 pages) {
    if (!p || (u32)p < HIGH_BASE)
        return;
    u32 v = (u32)p;
    u32 phys;
    if (v >= DIRECT_BASE)
        phys = DIRECT_PHYS_START + (v - DIRECT_BASE);
    else
        phys = v - HIGH_BASE;
    frame_free_pages(phys, pages);
}

static void *kernel_alloc_low_pages_zero(u32 pages) {
    u32 phys = frame_alloc_low_pages(pages);
    if (!phys)
        return 0;
    frame_zero(phys, pages);
    return direct_ptr(phys);
}

static void task_system_init(void) {
    frame_init();
    if (!sched_safe_ready) {
        for (int i = 0; i < 1024; i++) {
            sched_safe_pt[i] = (u32)(i * 0x1000) | 0x003;
            sched_safe_pd[i] = 0;
        }
        sched_safe_pd[0] = (u32)sched_safe_pt | 0x003;
        for (u32 i = 0; i < LOW_DIRECT_PT_COUNT; i++)
            sched_safe_pd[LOW_DIRECT_PDE + i] = (u32)direct_pt[i] | 0x003;
        install_high_pse(sched_safe_pd);
        enable_pse();
        sched_safe_ready = 1;
    }
    if (vbe_active)
        sched_safe_pd[VBE_PDE_INDEX] = vbe_pde_entry;
    set_cr3((u32)sched_safe_pd);

    for (int i = 0; i < 1024; i++)
        task_kernel_pt[i] = (u32)(i * 0x1000) | 0x003;

    u32 cr0;
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));
    if (!(cr0 & 0x80000000u)) {
        cr0 |= 0x80000000u;
        __asm__ __volatile__("mov %0, %%cr0" : : "r"(cr0));
    }
}

static u32 user_unmap_page(int slot, u32 va);
static void user_maybe_free_table(int slot, u32 va);

static void task_free_heap(int slot) {
    for (u32 i = 0; i < tasks[slot].heap_pages; i++) {
        u32 va = HEAP_VIRT_BASE + i * 0x1000u;
        u32 phys = user_unmap_page(slot, va);
        if (phys)
            frame_free_pages(phys, 1);
        user_maybe_free_table(slot, va);
    }
    tasks[slot].heap_pages = 0;
    tasks[slot].heap_phys = 0;
}

static void __attribute__((unused)) task_clear_all(void) {
    for (int t = 0; t < task_cap; t++) {
        if (tasks[t].state != TASK_FREE) {
            if (tasks[t].code_phys)
                frame_free_pages(tasks[t].code_phys, tasks[t].code_pages);
            if (tasks[t].stack_phys)
                frame_free_pages(tasks[t].stack_phys, tasks[t].stack_pages);
            task_free_heap(t);
        }
        tasks[t].state = TASK_FREE;
        tasks[t].reap_pending = 0;
        tasks[t].frames_freed = 0;
        tasks[t].stop_prev_state = 0;
        tasks[t].code_phys = 0;
        tasks[t].stack_phys = 0;
        tasks[t].heap_phys = 0;
        tasks[t].code_pages = 0;
        tasks[t].stack_pages = 0;
        tasks[t].heap_pages = 0;
        tasks[t].name[0] = 0;
        tasks[t].pid = 0;
        tasks[t].parent_pid = 0;
        tasks[t].waiting_pid = WAIT_NONE;
        tasks[t].wait_status = 0;
        tasks[t].sleep_until = 0;
        tasks[t].exit_code = 0;
        tasks[t].cpu_ticks = 0;
        tasks[t].console_id = 0;
        tasks[t].kill_pending = 0;
        tasks[t].sig_pending = 0;
        tasks[t].sig_ignored = 0;
        tasks[t].wait_kbd = 0;
        tasks[t].wait_fdc = 0;
        tasks[t].wait_fdc_lock = 0;
        tasks[t].kesp = 0;
        tasks[t].tf = 0;
        tasks[t].shared_ro = 0;
        if (tasks[t].fds)
            for (int f = 0; f < MAX_FDS; f++)
                tasks[t].fds[f].used = 0;
    }
}

static void task_tables_grow(void) {
    int ncap = task_cap ? task_cap * 2 : TASK_INIT_CAP;
    u32 bytes = (u32)(ncap * sizeof(struct task));
    u32 pages = (bytes + 4095u) / 4096u;
    struct task *nt = (struct task *)kernel_alloc_pages_zero(pages);
    if (!nt)
        return;
    u32 flags;
    __asm__ __volatile__("pushf; pop %0; cli" : "=r"(flags));
    int old_cap = task_cap;
    struct task *old = tasks;
    for (int i = 0; i < old_cap; i++)
        nt[i] = old[i];
    tasks = nt;
    task_cap = ncap;
    if (old)
        kernel_free_pages_ptr(old,
            ((u32)(old_cap * sizeof(struct task)) + 4095u) / 4096u);
    irq_restore_flags(flags);
}

static void task_tables_init(void) {
    if (task_cap)
        return;
    task_tables_grow();
}

static int task_alloc_slot(void) {
    task_tables_init();
    for (int t = 1; t < task_cap; t++)
        if (tasks[t].state == TASK_FREE ||
            (tasks[t].state == TASK_DONE && !tasks[t].reap_pending))
            return t;
    int old = task_cap;
    task_tables_grow();
    return task_cap > old ? old : -1;
}

static inline void brk_invlpg(u32 va);

static u32 *user_pte_ptr(int slot, u32 va) {
    u32 pde = va >> 22;
    if (slot < 0 || slot >= task_cap || pde < 1 || pde > USER_PT_COUNT)
        return 0;
    u32 idx = pde - 1;
    if (!tasks[slot].upt[idx])
        return 0;
    return &tasks[slot].upt[idx][(va >> 12) & 1023u];
}

static u32 user_pte_get(int slot, u32 va) {
    u32 *pte = user_pte_ptr(slot, va);
    return pte ? *pte : 0;
}

static u8 *user_va_ptr(int slot, u32 va) {
    u32 pte = user_pte_get(slot, va);
    if (!(pte & 1))
        return 0;
    return (u8 *)direct_ptr(pte & 0xFFFFF000u) + (va & 0xFFFu);
}

static int user_ensure_table(int slot, u32 va) {
    u32 pde = va >> 22;
    if (pde < 1 || pde > USER_PT_COUNT)
        return 0;
    u32 idx = pde - 1;
    if (tasks[slot].upt[idx])
        return 1;
    u32 phys = frame_alloc_low_pages(1);
    if (!phys)
        return 0;
    frame_zero(phys, 1);
    tasks[slot].upt_phys[idx] = phys;
    tasks[slot].upt[idx] = (u32 *)direct_ptr(phys);
    tasks[slot].pd[pde] = phys | 0x007;
    return 1;
}

static int user_map_page(int slot, u32 va, u32 phys, u32 flags) {
    if (va < BIG_USER_CODE || va >= USER_END ||
        !user_ensure_table(slot, va))
        return 0;
    u32 *pte = user_pte_ptr(slot, va);
    if (!pte)
        return 0;
    *pte = phys | flags;
    if (slot == current_task)
        brk_invlpg(va);
    return 1;
}

static u32 user_unmap_page(int slot, u32 va) {
    u32 *pte = user_pte_ptr(slot, va);
    if (!pte)
        return 0;
    u32 phys = *pte & 0xFFFFF000u;
    if (!(*pte & 1))
        return 0;
    *pte = 0;
    if (slot == current_task)
        brk_invlpg(va);
    return phys;
}

static void user_maybe_free_table(int slot, u32 va) {
    u32 pde = va >> 22;
    if (slot < 0 || slot >= task_cap || pde < 1 || pde > USER_PT_COUNT)
        return;
    u32 idx = pde - 1;
    if (!tasks[slot].upt[idx])
        return;
    for (int i = 0; i < 1024; i++)
        if (tasks[slot].upt[idx][i] & 1)
            return;
    u32 phys = tasks[slot].upt_phys[idx];
    tasks[slot].pd[pde] = 0;
    tasks[slot].upt[idx] = 0;
    tasks[slot].upt_phys[idx] = 0;
    if (slot == current_task)
        brk_invlpg(va & 0xFFC00000u);
    if (phys)
        frame_free_pages(phys, 1);
}

static void free_user_range(int slot, u32 va, u32 pages) {
    for (u32 i = 0; i < pages; i++) {
        u32 p = user_unmap_page(slot, va + i * 0x1000u);
        if (p)
            frame_free_pages(p, 1);
        user_maybe_free_table(slot, va + i * 0x1000u);
    }
}

static void user_free_tables(int slot) {
    for (u32 idx = 0; idx < USER_PT_COUNT; idx++) {
        if (tasks[slot].upt_phys[idx]) {
            if (slot == current_task)
                brk_invlpg(BIG_USER_CODE + (idx + 1) * 0x400000u);
            frame_free_pages(tasks[slot].upt_phys[idx], 1);
        }
        tasks[slot].upt[idx] = 0;
        tasks[slot].upt_phys[idx] = 0;
    }
}

static int task_fill_pd(int slot) {
    u32 pd_phys = frame_alloc_low_pages(1);
    if (!pd_phys)
        return 0;
    frame_zero(pd_phys, 1);
    tasks[slot].pd = (u32 *)direct_ptr(pd_phys);
    tasks[slot].pd_phys = pd_phys;
    for (u32 i = 0; i < USER_PT_COUNT; i++) {
        tasks[slot].upt[i] = 0;
        tasks[slot].upt_phys[i] = 0;
    }
    tasks[slot].pd[0] = (u32)task_kernel_pt | 0x003;
    for (u32 i = 0; i < LOW_DIRECT_PT_COUNT; i++)
        tasks[slot].pd[LOW_DIRECT_PDE + i] = (u32)direct_pt[i] | 0x003;
    install_high_pse(tasks[slot].pd);
    if (vbe_active)
        tasks[slot].pd[VBE_PDE_INDEX] = vbe_pde_entry;
    return 1;
}

static void vbe_init(void) {
    const volatile struct vbe_boot_info *vi =
        (const volatile struct vbe_boot_info *)0x00000500u;
    vbe_active = 0;
    vbe_lfb = 0;
    vbe_pitch = 320;
    vbe_phys_base = 0x000A0000u;
    vbe_pde_entry = 0;
    scr_w = 320;
    scr_h = 200;

    if (vi->magic[0] == 'V' && vi->magic[1] == 'B' &&
        vi->magic[2] == 'E' && vi->magic[3] == '1' &&
        vi->width == 640 && vi->height == 480 &&
        vi->bpp == 8 && vi->phys_base >= 0x00100000u) {
        u32 map_base = vi->phys_base & 0xFFC00000u;
        u32 map_off = vi->phys_base - map_base;
        u32 pitch = vi->pitch ? (u32)vi->pitch : (u32)vi->width;
        if (map_off + pitch * (u32)vi->height <= 0x00400000u) {
            u32 cr4;
            __asm__ __volatile__("mov %%cr4, %0" : "=r"(cr4));
            cr4 |= 0x00000010u; /* CR4.PSE for 4 MB supervisor LFB page */
            __asm__ __volatile__("mov %0, %%cr4" : : "r"(cr4));

            vbe_wc = mtrr_wc_4m(map_base);
            vbe_pde_entry = map_base | (vbe_wc ? 0x083u : 0x09Bu);
            sched_safe_pd[VBE_PDE_INDEX] = vbe_pde_entry;
            set_cr3((u32)sched_safe_pd);

            vbe_active = 1;
            vbe_lfb = (volatile u8 *)(VBE_VIRT_BASE + map_off);
            vbe_pitch = pitch;
            vbe_phys_base = vi->phys_base;
            scr_w = (int)vi->width;
            scr_h = (int)vi->height;
        }
    }
    mouse_x = scr_w / 2;
    mouse_y = scr_h / 2;
}

static void task_free_frames(int slot);
static void pipe_release_task(int slot);

static void task_init_kernel_context(int slot, u32 *frame) {
    u32 *ks = (u32 *)((u32)frame - 24);
    for (int i = 0; i < 5; i++)
        ks[i] = 0;
    ks[0] = 0x002;
    ks[5] = (u32)task_first_entry;
    tasks[slot].kesp = (u32)ks;
    tasks[slot].tf = frame;
    tasks[slot].esp = (u32)frame;
}

static void task_set_name(int slot, const char *name) {
    int i = 0;
    while (i < 15 && name[i]) {
        tasks[slot].name[i] = name[i];
        i++;
    }
    tasks[slot].name[i] = 0;
}

static int task_guard_pte(u8 *base, int present) {
    u32 v = (u32)base;
    if (v < DIRECT_BASE)
        return 0;
    u32 idx = (v - DIRECT_BASE) >> 12;
    if (idx >= (u32)LOW_DIRECT_PT_COUNT * 1024u)
        return 0;
    direct_pt[idx / 1024u][idx % 1024u] =
        present ? (DIRECT_PHYS_START + idx * 0x1000u) | 0x003 : 0;
    u32 va = DIRECT_BASE + idx * 0x1000u;
    __asm__ __volatile__("invlpg (%0)" : : "r"(va) : "memory");
    return 1;
}

static int task_alloc_runtime(int slot, int kpages) {
    tasks[slot].kstack = (u8 *)kernel_alloc_low_pages_zero((u32)(kpages + 1));
    tasks[slot].kstack_pages = (u16)(kpages + 1);
    tasks[slot].fds = (struct fd_entry *)kernel_alloc_low_pages_zero(1);
    if (!tasks[slot].kstack || !tasks[slot].fds ||
        !task_fill_pd(slot))
        return 0;
    /* Lowest page is a guard: present=0 in the shared direct map. */
    task_guard_pte(tasks[slot].kstack, 0);
    for (int f = 0; f < MAX_FDS; f++)
        tasks[slot].fds[f].used = 0;
    return 1;
}

static int task_create(const char *name, const u8 *src, const u8 *end) {
    int slot = task_alloc_slot();
    if (slot < 0)
        return -1;

    u32 len = (u32)(end - src);
    if (len == 0 || len > 4096)
        return -1;
    u32 code = frame_alloc_pages(1);
    u32 stack = frame_alloc_pages(1);
    if (!code || !stack || !task_alloc_runtime(slot, 1)) {
        if (code) frame_free_pages(code, 1);
        if (stack) frame_free_pages(stack, 1);
        task_free_frames(slot);
        tasks[slot].state = TASK_FREE;
        return -1;
    }

    u8 *codev = (u8 *)direct_ptr(code);
    u8 *stackv = (u8 *)direct_ptr(stack);
    for (u32 i = 0; i < 4096; i++) {
        codev[i] = 0;
        stackv[i] = 0;
    }
    for (u32 i = 0; i < len; i++)
        codev[i] = src[i];

    tasks[slot].stack_va = 0x00401000u;
    tasks[slot].stack_pages = 1;
    if (!user_map_page(slot, 0x00400000u, code, 0x007) ||
        !user_map_page(slot, 0x00401000u, stack, 0x007)) {
        frame_free_pages(code, 1);
        frame_free_pages(stack, 1);
        task_free_frames(slot);
        tasks[slot].state = TASK_FREE;
        return -1;
    }

    u32 top = (u32)(tasks[slot].kstack + tasks[slot].kstack_pages * 4096);
    u32 *frame = (u32 *)(top - 52);
    for (int i = 0; i < 13; i++)
        frame[i] = 0;
    frame[8] = 0x00400000;
    frame[9] = 0x1B;
    frame[10] = 0x202;
    frame[11] = 0x00402000;
    frame[12] = 0x23;

    task_init_kernel_context(slot, frame);
    tasks[slot].kstack_top = top;
    tasks[slot].cr3 = tasks[slot].pd_phys;
    tasks[slot].code_phys = code;
    tasks[slot].stack_phys = 0;
    tasks[slot].heap_phys = 0;
    tasks[slot].code_pages = 1;
    tasks[slot].stack_pages = 1;
    tasks[slot].heap_pages = 0;
    tasks[slot].shared_ro = 0;
    tasks[slot].state = TASK_READY;
    tasks[slot].reap_pending = 0;
    tasks[slot].frames_freed = 0;
    tasks[slot].stop_prev_state = 0;
    tasks[slot].pid = next_pid++;
    tasks[slot].parent_pid = 0;
    tasks[slot].waiting_pid = WAIT_NONE;
    tasks[slot].wait_status = 0;
    tasks[slot].sleep_until = 0;
    tasks[slot].exit_code = 0;
    tasks[slot].cpu_ticks = 0;
    tasks[slot].wait_pipe = 0;
    tasks[slot].wait_pipe_write = 0;
    tasks[slot].pipe_w_id = 0;
    tasks[slot].pipe_r_id = 0;
    tasks[slot].console_id = (u8)gui_focused_console();
    tasks[slot].kill_pending = 0;
    tasks[slot].sig_pending = 0;
    tasks[slot].sig_ignored = 0;
    tasks[slot].wait_kbd = 0;
    tasks[slot].wait_fdc = 0;
    tasks[slot].wait_fdc_lock = 0;
    fpu_task_default(tasks[slot].fpu);
    task_set_name(slot, name);
    return slot;
}

static int task_create_big(const char *name, u32 image, u32 bss, u32 entry) {
    int slot = task_alloc_slot();
    if (slot < 0)
        return -1;
    if (image > BIG_CODE_MAX || bss > BIG_CODE_MAX - image)
        return -1;

    u32 code_pages = (image + bss + 4095) / 4096;
    if (code_pages == 0 || code_pages > BIG_CODE_MAX / 4096)
        return -1;
    struct module_header *mh = (struct module_header *)BIG_CODE_PHYS;
    u32 ro_pages = 0;
    if (mh->magic[0] == 'U' && mh->magic[1] == 'M' &&
        mh->magic[2] == 'P' && mh->magic[3] == '1' &&
        mh->ro_size > 0 && mh->ro_size <= image)
        ro_pages = (mh->ro_size + 4095) / 4096;
    if (ro_pages > code_pages)
        return -1;
    u32 data_pages = code_pages - ro_pages;
    int use_shared = ro_pages > 0;
    if (use_shared && shared_ro_phys &&
        (shared_ro_pages != ro_pages || shared_ro_image != image)) {
        if (shared_ro_refs == 0) {
            frame_free_pages(shared_ro_phys, shared_ro_pages);
            shared_ro_phys = 0;
            shared_ro_pages = 0;
            shared_ro_image = 0;
        } else {
            /* Another MPY image is live: fall back to a private full copy. */
            use_shared = 0;
            ro_pages = 0;
            data_pages = code_pages;
        }
    }
    if (use_shared && !shared_ro_phys && shared_ro_refs) {
        use_shared = 0;
        ro_pages = 0;
        data_pages = code_pages;
    }

    u32 code = data_pages ? frame_alloc_pages(data_pages) : 0;
    if ((data_pages && !code) || !task_alloc_runtime(slot, 2)) {
        if (code) frame_free_pages(code, data_pages);
        task_free_frames(slot);
        tasks[slot].state = TASK_FREE;
        return -1;
    }
    tasks[slot].heap_pages = 0;
    tasks[slot].stack_va = BIG_USER_STACK;
    tasks[slot].stack_pages = 0;
    tasks[slot].stack_phys = 0;

    u8 *src = (u8 *)BIG_CODE_PHYS;
    if (use_shared) {
        if (!shared_ro_phys) {
            shared_ro_phys = frame_alloc_pages(ro_pages);
            if (!shared_ro_phys) {
                if (code) frame_free_pages(code, data_pages);
                task_free_frames(slot);
                tasks[slot].state = TASK_FREE;
                return -1;
            }
            for (u32 i = 0; i < ro_pages * 0x1000u; i++)
                ((u8 *)direct_ptr(shared_ro_phys))[i] = src[i];
            shared_ro_pages = ro_pages;
            shared_ro_image = image;
            shared_ro_refs = 0;
        }
    }
    if (data_pages) {
        u8 *codev = (u8 *)direct_ptr(code);
        for (u32 i = 0; i < data_pages * 0x1000u; i++)
            codev[i] = 0;
        u32 off = ro_pages * 0x1000u;
        if (image > off) {
            for (u32 i = 0; i < image - off; i++)
                codev[i] = src[off + i];
        }
    }
    for (u32 i = 0; i < ro_pages; i++) {
        if (!user_map_page(slot, BIG_USER_CODE + i * 0x1000u,
                           shared_ro_phys + i * 0x1000u, 0x005))
            goto map_fail;
    }
    for (u32 i = 0; i < data_pages; i++) {
        if (!user_map_page(slot, BIG_USER_CODE + (ro_pages + i) * 0x1000u,
                           code + i * 0x1000u, 0x007))
            goto map_fail;
    }
    for (u32 i = 0; i < BIG_STACK_PAGES; i++) {
        u32 sp = frame_alloc_pages(1);
        if (!sp)
            goto map_fail;
        frame_zero(sp, 1);
        tasks[slot].stack_pages = i + 1;
        if (!user_map_page(slot, BIG_USER_STACK + i * 0x1000u, sp, 0x007)) {
            frame_free_pages(sp, 1);
            tasks[slot].stack_pages = i;
            goto map_fail;
        }
    }
    for (u32 i = 0; i < BIG_HEAP_PAGES; i++) {
        u32 hp = frame_alloc_pages(1);
        if (!hp)
            goto map_fail;
        frame_zero(hp, 1);
        tasks[slot].heap_pages = i + 1;
        if (!user_map_page(slot, BIG_USER_HEAP + i * 0x1000u, hp, 0x007)) {
            frame_free_pages(hp, 1);
            tasks[slot].heap_pages = i;
            goto map_fail;
        }
    }

    u32 top = (u32)(tasks[slot].kstack + tasks[slot].kstack_pages * 4096);
    u32 *frame = (u32 *)(top - 52);
    for (int i = 0; i < 13; i++)
        frame[i] = 0;
    frame[8] = BIG_USER_CODE + entry;
    frame[9] = 0x1B;
    frame[10] = 0x202;
    frame[11] = BIG_USER_STACK + BIG_STACK_PAGES * 0x1000;
    frame[12] = 0x23;

    task_init_kernel_context(slot, frame);
    tasks[slot].kstack_top = top;
    tasks[slot].cr3 = tasks[slot].pd_phys;
    tasks[slot].code_phys = code;
    tasks[slot].stack_phys = 0;
    tasks[slot].heap_phys = 0;
    tasks[slot].code_pages = (u16)data_pages;
    tasks[slot].stack_pages = BIG_STACK_PAGES;
    tasks[slot].heap_pages = BIG_HEAP_PAGES;
    tasks[slot].shared_ro = ro_pages ? 1 : 0;
    if (ro_pages)
        shared_ro_refs++;
    tasks[slot].state = TASK_READY;
    tasks[slot].reap_pending = 0;
    tasks[slot].frames_freed = 0;
    tasks[slot].stop_prev_state = 0;
    tasks[slot].pid = next_pid++;
    tasks[slot].parent_pid = 0;
    tasks[slot].waiting_pid = WAIT_NONE;
    tasks[slot].wait_status = 0;
    tasks[slot].sleep_until = 0;
    tasks[slot].exit_code = 0;
    tasks[slot].cpu_ticks = 0;
    tasks[slot].wait_pipe = 0;
    tasks[slot].wait_pipe_write = 0;
    tasks[slot].pipe_w_id = 0;
    tasks[slot].pipe_r_id = 0;
    tasks[slot].console_id = (u8)gui_focused_console();
    tasks[slot].kill_pending = 0;
    tasks[slot].sig_pending = 0;
    tasks[slot].sig_ignored = 0;
    tasks[slot].wait_kbd = 0;
    tasks[slot].wait_fdc = 0;
    tasks[slot].wait_fdc_lock = 0;
    fpu_task_default(tasks[slot].fpu);
    for (int f = 0; f < MAX_FDS; f++)
        tasks[slot].fds[f].used = 0;
    task_set_name(slot, name);
    last_created_slot = slot;
    return slot;

map_fail:
    if (code) frame_free_pages(code, data_pages);
    task_free_frames(slot);
    tasks[slot].state = TASK_FREE;
    return -1;
}

static void start_tasks(int first) {
    (void)first;
    __asm__ __volatile__("sti");
}

static void sched_init_task0(void) {
    task_tables_init();
    tasks[0].state = TASK_RUNNING;
    task_set_name(0, "kernel");
    tasks[0].cr3 = (u32)sched_safe_pd;
    tasks[0].pd = sched_safe_pd;
    tasks[0].kstack = task0_kstack;
    tasks[0].kstack_pages = 2;
    tasks[0].kstack_top = (u32)task0_kstack + sizeof(task0_kstack);
    tasks[0].console_id = 0;
    tasks[0].pid = 0;
    tasks[0].kesp = 0;
    tasks[0].tf = 0;
    fpu_task_default(tasks[0].fpu);
    current_task = 0;
    scheduler_active = 1;
    tss.esp0 = 0x00080000u;
    set_cr3((u32)sched_safe_pd);
}

static int next_ready(void) {
    static int rr_cursor = 1;
    if (current_task == 0) {
        for (int k = 0; k < task_cap - 1; k++) {
            int t = 1 + (rr_cursor - 1 + k) % (task_cap - 1);
            if (tasks[t].state == TASK_READY) {
                rr_cursor = (t % (task_cap - 1)) + 1;
                return t;
            }
        }
        return -1;
    }
    for (int k = 1; k < task_cap; k++) {
        int t = (current_task + k) % task_cap;
        if (t == 0)
            continue;
        if (tasks[t].state == TASK_READY)
            return t;
    }
    if (tasks[0].state == TASK_READY)
        return 0;
    return -1;
}

static void sched_switch(int next) {
    int prev = current_task;
    if (prev >= 0 && prev < task_cap && tasks[prev].state != TASK_DONE)
        __asm__ __volatile__("fsave %0" : "=m"(tasks[prev].fpu));
    if (next == 0) {
        tss.esp0 = 0x00080000u;
        set_cr3((u32)sched_safe_pd);
    } else {
        __asm__ __volatile__("frstor %0" : : "m"(tasks[next].fpu));
        tss.esp0 = tasks[next].kstack_top;
        set_cr3(tasks[next].cr3);
    }
    current_task = next;
    tasks[next].state = TASK_RUNNING;
    quantum_ticks = 0;
    swtch(&tasks[prev].kesp, tasks[next].kesp);
}

static void module_wait_key(int cid) {
    __asm__ __volatile__("cli");
    if (kbd_tail[cid] != kbd_head[cid]) {
        __asm__ __volatile__("sti");
        return;
    }
    tasks[current_task].wait_kbd = (u8)(cid + 1);
    tasks[current_task].state = TASK_BLOCKED;
    sched_yield();
    __asm__ __volatile__("sti");
}

static u32 task0_fdc_blocks;

static int sched_block_current(void) {
    int me = current_task;
    if (me < 0 || me >= task_cap)
        return 0;
    tasks[me].state = TASK_BLOCKED;
    int next = next_ready();
    if (next < 0 || next == me) {
        tasks[me].state = TASK_READY;
        return 0;
    }
    if (me == 0)
        task0_fdc_blocks++;
    sched_switch(next);
    return 1;
}

u32 kernel_task0_fdc_blocks(void) {
    return task0_fdc_blocks;
}

static void sched_yield(void) {
    if (current_task == 0) {
        tasks[0].state = TASK_READY;
    } else if (tasks[current_task].state == TASK_RUNNING) {
        tasks[current_task].state = TASK_READY;
    }
    int next = next_ready();
    if (next < 0) {
        if (current_task == 0)
            tasks[0].state = TASK_RUNNING;
        return;
    }
    sched_switch(next);
}

static void __attribute__((unused)) switch_task(int next) {
    if (current_task >= 0 && current_task < task_cap)
        __asm__ __volatile__("fsave %0" : "=m"(tasks[current_task].fpu));
    __asm__ __volatile__("frstor %0" : : "m"(tasks[next].fpu));
    current_task = next;
    tasks[next].state = TASK_RUNNING;
    quantum_ticks = 0;
    tss.esp0 = tasks[next].kstack_top;
    set_cr3(tasks[next].cr3);
    switch_to(tasks[next].esp);
    for (;;) {}
}

static void task_free_frames(int slot) {
    if (slot == current_task)
        return;
    if (fs_sync_owner == slot) {
        fs_sync_owner = -1;
        fs_snapshot_busy = 0;
    }
    kernel_fdc_release_owner(slot);
    pipe_release_task(slot);
    if (tasks[slot].shared_ro) {
        if (shared_ro_refs)
            shared_ro_refs--;
        if (!shared_ro_refs && shared_ro_phys) {
            frame_free_pages(shared_ro_phys, shared_ro_pages);
            shared_ro_phys = 0;
            shared_ro_pages = 0;
            shared_ro_image = 0;
        }
        tasks[slot].shared_ro = 0;
    }
    if (tasks[slot].code_phys)
        frame_free_pages(tasks[slot].code_phys, tasks[slot].code_pages);
    if (tasks[slot].stack_va)
        free_user_range(slot, tasks[slot].stack_va, tasks[slot].stack_pages);
    task_free_heap(slot);
    if (slot > 0) {
        user_free_tables(slot);
        if (tasks[slot].pd)
            kernel_free_pages_ptr(tasks[slot].pd, 1);
        if (tasks[slot].kstack) {
            task_guard_pte(tasks[slot].kstack, 1);
            kernel_free_pages_ptr(tasks[slot].kstack, tasks[slot].kstack_pages);
        }
        if (tasks[slot].fds)
            kernel_free_pages_ptr(tasks[slot].fds, 1);
        tasks[slot].pd = 0;
        tasks[slot].kstack = 0;
        tasks[slot].fds = 0;
        tasks[slot].kstack_pages = 0;
        tasks[slot].pd_phys = 0;
    }
    tasks[slot].code_phys = 0;
    tasks[slot].stack_phys = 0;
    tasks[slot].heap_phys = 0;
    tasks[slot].code_pages = 0;
    tasks[slot].stack_pages = 0;
}

static void task_reap_done(void) {
    for (int t = 1; t < task_cap; t++) {
        if (tasks[t].state != TASK_DONE || !tasks[t].reap_pending)
            continue;
        if (!tasks[t].frames_freed) {
            task_free_frames(t);
            tasks[t].frames_freed = 1;
        }
        if (tasks[t].parent_pid == 0)
            tasks[t].reap_pending = 0;
    }
}

static int find_pid(u32 pid) {
    for (int t = 0; t < task_cap; t++) {
        if (tasks[t].state != TASK_FREE && tasks[t].pid == pid)
            return t;
    }
    return -1;
}

static void wake_parent(int child_slot) {
    u32 parent_pid = tasks[child_slot].parent_pid;
    if (parent_pid == 0)
        return;
    int parent = find_pid(parent_pid);
    if (parent < 0)
        return;
    if (!(tasks[parent].sig_ignored & SIGCHLD))
        tasks[parent].sig_pending |= SIGCHLD;
    u32 wait_pid = tasks[parent].waiting_pid;
    if ((tasks[parent].state == TASK_BLOCKED ||
         tasks[parent].state == TASK_STOPPED) &&
        wait_pid != WAIT_NONE &&
        (wait_pid == 0 || wait_pid == tasks[child_slot].pid)) {
        tasks[parent].wait_status = tasks[child_slot].exit_code;
        tasks[parent].waiting_pid = WAIT_NONE;
        if (tasks[parent].state == TASK_STOPPED)
            tasks[parent].stop_prev_state = TASK_READY;
        else
            tasks[parent].state = TASK_READY;
    }
}

static void task_exit(u32 status);
static void adopt_children(int slot);

static u32 irq_save_flags(void) {
    u32 flags;
    __asm__ __volatile__("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void irq_restore_flags(u32 flags) {
    __asm__ __volatile__("pushl %0; popfl" : : "r"(flags) : "memory");
}

static int task0_wanted(void) {
    extern int gui_dirty_pending(void);
    if (mouse_event || gui_special_pending || gui_dirty_pending())
        return 1;
    for (int t = 1; t < task_cap; t++)
        if (tasks[t].state == TASK_DONE && tasks[t].reap_pending)
            return 1;
    return (ticks & 3) == 0;
}

static void sched_timer_tick(u32 *frame) {
    if (!scheduler_active)
        return;

    if (current_task >= 0 && current_task < task_cap)
        tasks[current_task].cpu_ticks++;

    for (int t = 0; t < task_cap; t++) {
        if (tasks[t].state == TASK_BLOCKED && tasks[t].sleep_until != 0 &&
            (int)(ticks - tasks[t].sleep_until) >= 0) {
            tasks[t].sleep_until = 0;
            tasks[t].wait_fdc = 0;
            tasks[t].state = TASK_READY;
        }
    }

    if ((frame[9] & 3) != 3 || current_task == 0)
        return;

    if (tasks[current_task].kill_pending) {
        tasks[current_task].kill_pending = 0;
        task_exit(0xFFFFu);
    }
    if (tasks[current_task].sig_pending & SIG_TERM_MASK) {
        tasks[current_task].sig_pending &= (u8)~SIG_TERM_MASK;
        tasks[current_task].kill_pending = 0;
        task_exit(0xFFFFu);
    }

    int slice = TIME_SLICE_TICKS;
    if (tasks[current_task].console_id == (u8)gui_focused_console())
        slice *= 2;
    quantum_ticks++;
    if (quantum_ticks < (u32)slice)
        return;
    quantum_ticks = 0;

    tasks[current_task].state = TASK_READY;
    int next = -1;
    if (task0_wanted() && tasks[0].state == TASK_READY)
        next = 0;
    if (next < 0) {
        for (int k = 1; k < task_cap; k++) {
            int t = (current_task + k) % task_cap;
            if (t == 0)
                continue;
            if (tasks[t].state == TASK_READY) {
                next = t;
                break;
            }
        }
    }
    if (next < 0 && tasks[0].state == TASK_READY)
        next = 0;
    if (next >= 0 && next != current_task)
        sched_switch(next);
    else
        tasks[current_task].state = TASK_RUNNING;
}

static void task_yield(u32 frame) {
    (void)frame;
    if (scheduler_active)
        sched_yield();
}

static void task_exit(u32 status) {
    int me = current_task;
    int cid = tasks[me].console_id;
    adopt_children(me);
    pipe_release_task(me);
    tasks[me].exit_code = status;
    tasks[me].state = TASK_DONE;
    tasks[me].reap_pending = 1;
    tasks[me].frames_freed = 0;
    wake_parent(me);
    /* The current kernel stack must stay mapped until we switch away. */
    set_cr3((u32)sched_safe_pd);
    extern void gui_prompt_console(int cid);
    extern void gui_canvas_release_owner(u32 pid);
    gui_canvas_release_owner(tasks[me].pid);
    if (cid >= 0 && !console_has_live_task(cid))
        gui_prompt_console(cid);
    if (me != 0)
        sched_switch(0);
    for (;;) {
        __asm__ __volatile__("sti; hlt");
    }
}

#define CRASH_HEADER_LBA 2820u
#define CRASH_DATA_LBA   2821u
static char crash_log[512];
static u32 crash_log_len;

static void bsod_putc(int *x, int *y, char ch) {
    if (crash_log_len + 1 < sizeof(crash_log))
        crash_log[crash_log_len++] = ch;
    volatile u8 *base;
    u32 pitch;
    int width, height;
    if (vbe_active && vbe_lfb) {
        base = vbe_lfb;
        pitch = vbe_pitch;
        width = scr_w;
        height = scr_h;
    } else {
        base = (volatile u8 *)0x000A0000;
        pitch = 320;
        width = 320;
        height = 200;
    }
    if (ch == '\n' || *x + 8 > width) {
        *x = 8;
        *y += 10;
    }
    if (ch == '\n' || *y + 8 > height)
        return;
    if (ch < 32 || ch > 126)
        ch = '?';
    const u8 *glyph = (const u8 *)0x000FFA6E + (u8)ch * 8;
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            if (glyph[r] & (0x80u >> c))
                base[(*y + r) * pitch + *x + c] = 15;
    *x += 8;
}

static void bsod_puts(int *x, int *y, const char *s) {
    while (*s)
        bsod_putc(x, y, *s++);
}

static void bsod_hex(int *x, int *y, const char *label, u32 v) {
    static const char h[] = "0123456789ABCDEF";
    bsod_puts(x, y, label);
    bsod_puts(x, y, "0x");
    for (int i = 28; i >= 0; i -= 4)
        bsod_putc(x, y, h[(v >> i) & 15]);
    bsod_putc(x, y, '\n');
}

static void crash_disk_dump(u32 vector) {
    u8 sector[512];
    for (int i = 0; i < 512; i++)
        sector[i] = 0;
    sector[0] = 'M'; sector[1] = 'C'; sector[2] = 'R'; sector[3] = '1';
    for (int i = 0; i < 4; i++)
        sector[4 + i] = (u8)(vector >> (i * 8));
    u32 n = crash_log_len;
    u32 first = n > 496 ? 496 : n;
    for (int i = 0; i < 4; i++)
        sector[8 + i] = (u8)(n >> (i * 8));
    for (u32 i = 0; i < first; i++)
        sector[16 + i] = (u8)crash_log[i];
    scheduler_active = 0;
    runtime_busy = 0;
    outb(0x21, 0xB9);           /* IRQ0, cascade, IRQ6 only */
    outb(0xA1, 0xFF);
    __asm__ __volatile__("sti");
    serial_puts("BSOD disk dump LBA=2820\r\n");
    int ok = fdc_write_sector(CRASH_HEADER_LBA, sector);
    if (ok && n > 496) {
        for (int i = 0; i < 512; i++)
            sector[i] = 0;
        for (u32 i = 496; i < n; i++)
            sector[i - 496] = (u8)crash_log[i];
        ok = fdc_write_sector(CRASH_DATA_LBA, sector);
    }
    __asm__ __volatile__("cli");
    serial_puts(ok ? "BSOD disk dump OK\r\n" : "BSOD disk dump FAILED\r\n");
}

static void bsod_show(u32 vector, u32 *frame) {
    __asm__ __volatile__("cli");
    crash_log_len = 0;
    __asm__ __volatile__(
        "movw $0x10, %%ax; movw %%ax, %%ds; movw %%ax, %%es;"
        "movw %%ax, %%fs; movw %%ax, %%gs; movw %%ax, %%ss"
        : : : "eax");
    volatile u8 *base;
    u32 pitch;
    int width, height;
    if (vbe_active && vbe_lfb) {
        base = vbe_lfb; pitch = vbe_pitch;
        width = scr_w; height = scr_h;
    } else {
        base = (volatile u8 *)0x000A0000; pitch = 320;
        width = 320; height = 200;
    }
    for (int yy = 0; yy < height; yy++)
        for (int xx = 0; xx < width; xx++)
            base[(u32)yy * pitch + (u32)xx] = 1;
    int x = 8, y = 8;
    bsod_puts(&x, &y, "MiniOS KERNEL PANIC\n\n");
    bsod_hex(&x, &y, "vector: ", vector);
    u32 cr2 = 0;
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
    bsod_hex(&x, &y, "cr2:    ", cr2);
    if (frame) {
        bsod_hex(&x, &y, "error:  ", frame[9]);
        bsod_hex(&x, &y, "eip:    ", frame[10]);
        bsod_hex(&x, &y, "cs:     ", frame[11]);
        bsod_hex(&x, &y, "esp:    ", (u32)&frame[12]);
        bsod_hex(&x, &y, "edi:    ", frame[0]);
        bsod_hex(&x, &y, "esi:    ", frame[1]);
        bsod_hex(&x, &y, "ebp:    ", frame[2]);
        bsod_hex(&x, &y, "ebx:    ", frame[4]);
        bsod_hex(&x, &y, "edx:    ", frame[5]);
        bsod_hex(&x, &y, "ecx:    ", frame[6]);
        bsod_hex(&x, &y, "eax:    ", frame[7]);
    } else {
        bsod_puts(&x, &y, "double fault task gate\n");
        bsod_hex(&x, &y, "old eip: ", tss.eip);
        bsod_hex(&x, &y, "old esp: ", tss.esp);
        bsod_hex(&x, &y, "old cr3: ", tss.cr3);
    }
    bsod_puts(&x, &y, "\nSystem halted. Reboot required.\n");
    serial_puts("BSOD vector=");
    serial_puts("0x");
    const char *h = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4)
        serial_putc((char)h[(vector >> i) & 15]);
    serial_puts("\r\n");
    if (crash_log_len < sizeof(crash_log))
        crash_log[crash_log_len] = 0;
    serial_puts(crash_log);
    serial_puts("\r\n");
    crash_disk_dump(vector);
    for (;;) {
        __asm__ __volatile__("cli");
        __asm__ __volatile__("hlt");
    }
}

void bsod_entry(void) {
    bsod_show(8, 0);
}

void exception_handler(u32 *frame) {
    u32 vector = frame[8];
    u32 cs = frame[11];

    if (scheduler_active && (cs & 3) == 3) {
        gui_sys_print("[fault] Ring3 task killed; returning to shell.\n");
        __asm__ __volatile__("movw $0x23, %%ax; movw %%ax, %%ds; movw %%ax, %%es"
                             : : : "eax");
        task_exit(0xDEADu);
    }

    bsod_show(vector, frame);
}

static char *ps_str(char *p, const char *s) {
    while (*s)
        *p++ = *s++;
    return p;
}

static char *ps_u32w(char *p, u32 v, int width) {
    char b[12];
    int n = 0;
    if (v == 0)
        b[n++] = '0';
    while (v) {
        b[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    int digits = n;
    while (n)
        *p++ = b[--n];
    while (digits < width) {
        *p++ = ' ';
        digits++;
    }
    return p;
}

static char *ps_strw(char *p, const char *s, int width) {
    int n = 0;
    while (s[n]) {
        *p++ = s[n++];
        width--;
    }
    while (width-- > 0)
        *p++ = ' ';
    return p;
}

void task_ps(void) {
    gui_sys_print("PID  PPID  CON  STATE  PAGES  CPU    NAME\n");
    serial_puts("PS\r\nPID  PPID  CON  STATE  PAGES  CPU    NAME\r\n");
    for (int t = 0; t < task_cap; t++) {
        if (tasks[t].state == TASK_FREE || tasks[t].state == TASK_DONE)
            continue;
        char line[96];
        char *p = line;
        p = ps_u32w(p, tasks[t].pid, 3);
        *p++ = ' '; *p++ = ' ';
        p = ps_u32w(p, tasks[t].parent_pid, 4);
        *p++ = ' '; *p++ = ' ';
        p = ps_u32w(p, (u32)tasks[t].console_id + 1, 3);
        *p++ = ' '; *p++ = ' ';
        const char *state = "?";
        if (tasks[t].state == TASK_READY) state = "READY";
        else if (tasks[t].state == TASK_RUNNING) state = "RUN";
        else if (tasks[t].state == TASK_BLOCKED) state = "BLOCK";
        else if (tasks[t].state == TASK_STOPPED) state = "STOP";
        p = ps_strw(p, state, 5);
        *p++ = ' '; *p++ = ' ';
        p = ps_u32w(p, (u32)tasks[t].code_pages + tasks[t].stack_pages +
                         tasks[t].heap_pages, 5);
        *p++ = ' '; *p++ = ' ';
        p = ps_u32w(p, tasks[t].cpu_ticks, 5);
        *p++ = ' '; *p++ = ' ';
        p = ps_str(p, tasks[t].name);
        *p = 0;
        gui_sys_print(line);
        gui_sys_print("\n");
        serial_puts(line);
        serial_puts("\r\n");
    }
}

int kernel_task_snapshot(int index, u32 *pid, int *console, u32 *state,
                         u32 *pages, char *name) {
    int n = 0;
    for (int t = 0; t < task_cap; t++) {
        if (tasks[t].state == TASK_FREE || tasks[t].state == TASK_DONE)
            continue;
        if (n++ != index)
            continue;
        *pid = tasks[t].pid;
        *console = tasks[t].console_id + 1;
        *state = tasks[t].state;
        *pages = tasks[t].code_pages + tasks[t].stack_pages + tasks[t].heap_pages;
        for (int j = 0; j < 16; j++)
            name[j] = tasks[t].name[j];
        return 1;
    }
    return 0;
}

u32 kernel_task_cpu(u32 pid) {
    int t = find_pid(pid);
    return t >= 0 ? tasks[t].cpu_ticks : 0;
}

int task_signal_pid(u32 pid, u8 sig) {
    int t = find_pid(pid);
    if (t <= 0 || t >= task_cap)
        return 0;
    if (tasks[t].state == TASK_FREE || tasks[t].state == TASK_DONE)
        return 0;
    if (sig & SIGKILL)
        return task_kill_pid(pid) ? 1 : 0;
    if (sig & SIGTERM) {
        if (tasks[t].sig_ignored & SIGTERM)
            return 1;
        return task_kill_pid(pid) ? 1 : 0;
    }
    if (sig & SIGSTOP)
        return task_stop_pid(pid) ? 1 : 0;
    if (sig & SIGCONT) {
        int r = task_bg_pid(pid);
        return r == 0 ? 0 : 1;
    }
    u32 flags = irq_save_flags();
    if (tasks[t].state == TASK_FREE || tasks[t].state == TASK_DONE) {
        irq_restore_flags(flags);
        return 0;
    }
    if ((sig & (SIGINT | SIGCHLD)) &&
        (tasks[t].sig_ignored & sig)) {
        irq_restore_flags(flags);
        return 1;
    }
    tasks[t].sig_pending |= sig;
    if (sig & SIGINT) {
        tasks[t].sleep_until = 0;
        tasks[t].wait_kbd = 0;
        tasks[t].wait_fdc = 0;
        tasks[t].wait_fdc_lock = 0;
        tasks[t].waiting_pid = WAIT_NONE;
        tasks[t].wait_pipe = 0;
        tasks[t].wait_pipe_write = 0;
        tasks[t].state = TASK_READY;
    }
    irq_restore_flags(flags);
    return 1;
}

void task_signal_console(int cid, u8 sig) {
    for (int t = 1; t < task_cap; t++) {
        if (tasks[t].state == TASK_FREE || tasks[t].state == TASK_DONE)
            continue;
        if ((int)tasks[t].console_id == cid) {
            task_signal_pid(tasks[t].pid, sig);
            return;
        }
    }
}

int task_kill_pid(u32 pid) {
    int target = -1;
    if (pid == 0)
        return 0;
    for (int t = 1; t < task_cap; t++) {
        if (tasks[t].state != TASK_FREE && tasks[t].state != TASK_DONE &&
            tasks[t].pid == pid) {
            target = t;
            break;
        }
    }
    if (target < 0)
        return 0;

    if (target == current_task) {
        tasks[target].kill_pending = 1;
        return 2;
    }

    tasks[target].kill_pending = 0;
    tasks[target].exit_code = 0xFFFFu;
    int cid = tasks[target].console_id;
    u32 tpid = tasks[target].pid;
    adopt_children(target);
    tasks[target].state = TASK_DONE;
    tasks[target].reap_pending = tasks[target].parent_pid != 0;
    tasks[target].frames_freed = 0;
    wake_parent(target);
    task_free_frames(target);
    tasks[target].frames_freed = 1;
    extern void gui_canvas_release_owner(u32 pid);
    extern void gui_prompt_console(int cid);
    gui_canvas_release_owner(tpid);
    if (!console_has_live_task(cid))
        gui_prompt_console(cid);
    return 1;
}

int kernel_kill_console_task(int console) {
    for (int t = 1; t < task_cap; t++) {
        if (tasks[t].state == TASK_FREE || tasks[t].state == TASK_DONE)
            continue;
        if ((int)tasks[t].console_id == console)
            return task_kill_pid(tasks[t].pid);
    }
    return 0;
}

int task_stop_pid(u32 pid) {
    int t = find_pid(pid);
    if (t <= 0 || pid == 0 || tasks[t].state == TASK_FREE ||
        tasks[t].state == TASK_DONE)
        return 0;
    u32 flags = irq_save_flags();
    if (tasks[t].state == TASK_FREE || tasks[t].state == TASK_DONE) {
        irq_restore_flags(flags);
        return 0;
    }
    if (tasks[t].state == TASK_STOPPED) {
        irq_restore_flags(flags);
        return 2;
    }
    tasks[t].stop_prev_state = tasks[t].state;
    tasks[t].sig_pending |= SIGSTOP;
    tasks[t].state = TASK_STOPPED;
    irq_restore_flags(flags);
    return 1;
}

static int pipe_wait_ready(int id, int write);

int task_bg_pid(u32 pid) {
    int t = find_pid(pid);
    if (t <= 0 || pid == 0 || tasks[t].state == TASK_FREE ||
        tasks[t].state == TASK_DONE)
        return 0;
    u32 flags = irq_save_flags();
    if (tasks[t].state == TASK_FREE || tasks[t].state == TASK_DONE) {
        irq_restore_flags(flags);
        return 0;
    }
    if (tasks[t].state != TASK_STOPPED) {
        irq_restore_flags(flags);
        return 2;
    }
    u8 prev = tasks[t].stop_prev_state;
    tasks[t].stop_prev_state = 0;
    tasks[t].sig_pending = (u8)((tasks[t].sig_pending & ~SIGSTOP) | SIGCONT);
    if (prev == TASK_BLOCKED) {
        if (tasks[t].waiting_pid != WAIT_NONE) {
            if (tasks[t].waiting_pid == 0) {
                int live = 0;
                int exited = -1;
                for (int c = 1; c < task_cap; c++) {
                    if (tasks[c].parent_pid != tasks[t].pid)
                        continue;
                    if (tasks[c].state != TASK_FREE &&
                        tasks[c].state != TASK_DONE) {
                        live = 1;
                        break;
                    }
                    if (tasks[c].state == TASK_DONE && exited < 0)
                        exited = c;
                }
                if (live) {
                    tasks[t].state = TASK_BLOCKED;
                } else {
                    if (exited >= 0)
                        tasks[t].wait_status = tasks[exited].exit_code;
                    tasks[t].waiting_pid = WAIT_NONE;
                    tasks[t].state = TASK_READY;
                }
            } else {
                int child = find_pid(tasks[t].waiting_pid);
                if (child >= 0 && tasks[child].state != TASK_DONE &&
                    tasks[child].state != TASK_FREE) {
                    tasks[t].state = TASK_BLOCKED;
                } else {
                    if (child >= 0)
                        tasks[t].wait_status = tasks[child].exit_code;
                    tasks[t].waiting_pid = WAIT_NONE;
                    tasks[t].state = TASK_READY;
                }
            }
        } else if (tasks[t].wait_fdc_lock) {
            tasks[t].wait_fdc_lock = 0;
            tasks[t].state = TASK_READY;
        } else if (tasks[t].wait_fdc) {
            tasks[t].wait_fdc = 0;
            tasks[t].sleep_until = 0;
            tasks[t].state = TASK_READY;
        } else if (tasks[t].wait_kbd != 0) {
            int cid = (int)tasks[t].wait_kbd - 1;
            if (cid >= 0 && cid < kbd_console_count &&
                kbd_tail[cid] != kbd_head[cid]) {
                tasks[t].wait_kbd = 0;
                tasks[t].state = TASK_READY;
            } else {
                tasks[t].state = TASK_BLOCKED;
            }
        } else if (tasks[t].wait_pipe != 0) {
            int pid_ = (int)tasks[t].wait_pipe - 1;
            if (pipe_wait_ready(pid_, tasks[t].wait_pipe_write)) {
                tasks[t].wait_pipe = 0;
                tasks[t].wait_pipe_write = 0;
                tasks[t].state = TASK_READY;
            } else {
                tasks[t].state = TASK_BLOCKED;
            }
        } else if (tasks[t].sleep_until != 0) {
            if ((int)(ticks - tasks[t].sleep_until) < 0) {
                tasks[t].state = TASK_BLOCKED;
            } else {
                tasks[t].sleep_until = 0;
                tasks[t].state = TASK_READY;
            }
        } else {
            tasks[t].state = TASK_READY;
        }
    } else {
        tasks[t].state = TASK_READY;
    }
    irq_restore_flags(flags);
    return 1;
}

static int run_user_module_ex(const char *name, int argc,
                              const char *const *argv);

/* Forward declarations: the implementations live next to the RAMFS helpers. */
static int ramfile_find(const char *name);
static int ramfile_rw(struct ramfile *r, u32 off, u8 *buf, u32 len, int wr);
static int ramfs_load_file_to(const char *name, u8 *dst, u32 max,
                              u32 *out_size);
static int vfs_load_file_to(const char *name, u8 *dst, u32 max,
                            u32 *out_size, int *out_from_ramfs);

static int image_is_ump1(const u8 *p, u32 size) {
    return size >= 4 && p[0] == 'U' && p[1] == 'M' && p[2] == 'P' && p[3] == '1';
}

static int ump1_header_ok(const struct module_header *h, u32 size) {
    return h->image_size <= size && h->entry_offset < h->image_size &&
           h->image_size <= BIG_CODE_MAX &&
           h->bss_size <= BIG_CODE_MAX - h->image_size;
}

static int name_is_py(const char *name) {
    int n = 0;
    while (name[n]) n++;
    return n >= 3 && name[n - 3] == '.' &&
           (name[n - 2] == 'P' || name[n - 2] == 'p') &&
           (name[n - 1] == 'Y' || name[n - 1] == 'y');
}

static int task_spawn_user(const char *name) {
    if (!scheduler_active || current_task < 0 || current_task >= task_cap)
        return 0;
    if (name_is_py(name)) {
        const char *argv[2] = { "py", name };
        int ok = run_user_module_ex("MPY.BIN", 2, argv);
        __asm__ __volatile__("cli");
        if (!ok)
            return 0;
        int child = last_created_slot;
        if (child <= 0 || child >= task_cap)
            return 0;
        tasks[child].parent_pid = tasks[current_task].pid;
        tasks[child].console_id = tasks[current_task].console_id;
        return (int)tasks[child].pid;
    }
    u32 size = 0;
    int child = -1;
    u8 *base = (u8 *)BIG_CODE_PHYS;
    __asm__ __volatile__("sti");
    int ok = vfs_load_file_to(name, base, BIG_CODE_MAX, &size, 0);
    __asm__ __volatile__("cli");
    if (!ok || size == 0)
        return 0;
    if (image_is_ump1(base, size)) {
        struct module_header *h = (struct module_header *)base;
        if (!ump1_header_ok(h, size))
            return 0;
        child = task_create_big(name, h->image_size, h->bss_size,
                                h->entry_offset);
    } else if (size <= FS_MAX_FILE) {
        child = task_create(name, base, base + size);
    }
    if (child < 0)
        return 0;
    tasks[child].parent_pid = tasks[current_task].pid;
    tasks[child].console_id = tasks[current_task].console_id;
    return (int)tasks[child].pid;
}

static int user_str_ok(u32 ptr, u32 max);

/* Copy a user-space argv array onto a child's user stack. */
static int build_user_args_on_stack(int slot, u32 argc, u32 uargv,
                                    u32 stack_base, u32 stack_top) {
    if (argc > 32 || tasks[slot].stack_va == 0)
        return 0;
    u32 sp = stack_top;
    u32 ptrs[32];

    for (u32 i = 0; i < argc; i++) {
        u32 up = ((const u32 *)uargv)[i];
        if (!user_str_ok(up, 64))
            return 0;
        int len = 0;
        while (len < 64 && *(volatile u8 *)(up + (u32)len) != 0)
            len++;
        if (len == 64)
            return 0;
        sp -= (u32)len + 1;
        if (sp < stack_base + 64)
            return 0;
        for (int j = 0; j <= len; j++) {
            u8 *dst = user_va_ptr(slot, sp + (u32)j);
            if (!dst)
                return 0;
            *dst = *(volatile u8 *)(up + (u32)j);
        }
        ptrs[i] = sp;
    }
    sp &= ~3u;
    sp -= 4;
    u32 *zero = (u32 *)user_va_ptr(slot, sp);
    if (!zero)
        return 0;
    *zero = 0;
    for (int i = (int)argc - 1; i >= 0; i--) {
        sp -= 4;
        u32 *slotp = (u32 *)user_va_ptr(slot, sp);
        if (!slotp)
            return 0;
        *slotp = ptrs[i];
    }
    sp -= 4;
    if (sp < stack_base + 64)
        return 0;
    u32 *argcp = (u32 *)user_va_ptr(slot, sp);
    if (!argcp)
        return 0;
    *argcp = argc;
    ((u32 *)tasks[slot].esp)[11] = sp;
    return 1;
}

static int task_spawn_args(const char *name, u32 argv, u32 argc) {
    if (!scheduler_active || current_task < 0 || current_task >= task_cap)
        return 0;
    if (argc > 32 || argv < 0x400000 || argv >= 0x800000)
        return 0;
    u32 size = 0;
    u8 *base = (u8 *)BIG_CODE_PHYS;
    __asm__ __volatile__("sti");
    int ok = vfs_load_file_to(name, base, BIG_CODE_MAX, &size, 0);
    __asm__ __volatile__("cli");
    if (!ok || size == 0)
        return 0;

    int child = -1;
    u32 stack_base = 0x00401000u;
    u32 stack_top = 0x00402000u;
    if (image_is_ump1(base, size)) {
        struct module_header *h = (struct module_header *)base;
        if (!ump1_header_ok(h, size))
            return 0;
        child = task_create_big(name, h->image_size, h->bss_size,
                                h->entry_offset);
        stack_base = BIG_USER_STACK;
        stack_top = BIG_USER_STACK + BIG_STACK_SIZE;
    } else if (size <= FS_MAX_FILE) {
        child = task_create(name, base, base + size);
    }
    if (child < 0)
        return 0;
    tasks[child].parent_pid = tasks[current_task].pid;
    tasks[child].console_id = tasks[current_task].console_id;

    if (!build_user_args_on_stack(child, argc, argv, stack_base, stack_top)) {
        tasks[child].state = TASK_FREE;
        task_free_frames(child);
        return 0;
    }
    return (int)tasks[child].pid;
}

static int task_wait_block(u32 want_pid, int nohang, u32 frame) {
    u32 my_pid = tasks[current_task].pid;
    int child = -1;
    for (int t = 0; t < task_cap; t++) {
        if (tasks[t].state == TASK_FREE || tasks[t].parent_pid != my_pid)
            continue;
        if (want_pid != 0 && tasks[t].pid != want_pid)
            continue;
        child = t;
        if (tasks[t].state == TASK_DONE)
            break;
    }
    if (child < 0)
        return -1;
    if (tasks[child].state == TASK_DONE) {
        u32 status = tasks[child].exit_code;
        if (!tasks[child].frames_freed) {
            task_free_frames(child);
            tasks[child].frames_freed = 1;
        }
        tasks[child].state = TASK_FREE;
        tasks[child].reap_pending = 0;
        tasks[child].frames_freed = 0;
        SYS_RET(frame) = status;
        return 0;
    }
    if (nohang) {
        SYS_RET(frame) = 0xFFFFFFFEu;
        return 0;
    }

    tasks[current_task].waiting_pid = want_pid;
    tasks[current_task].state = TASK_BLOCKED;
    sched_yield();
    SYS_RET(frame) = tasks[current_task].wait_status;
    tasks[current_task].wait_status = 0;
    return 0;
}

static void adopt_children(int slot) {
    u32 pid = tasks[slot].pid;
    for (int t = 1; t < task_cap; t++) {
        if (tasks[t].state != TASK_FREE && tasks[t].parent_pid == pid)
            tasks[t].parent_pid = 0;
    }
}

static void task_sleep_block(u32 ms, u32 frame) {
    if (ms == 0) {
        SYS_RET(frame) = 0;
        return;
    }
    tasks[current_task].state = TASK_BLOCKED;
    tasks[current_task].sleep_until = ticks + ms / 10 + 1;
    SYS_RET(frame) = 0;
    sched_yield();
}

int run_pack_program(const char *name) {
    int idx = pack_find(name);
    if (idx < 0) {
        gui_sys_print("[pack] program not found.\n");
        return 0;
    }
    struct pack_entry *e = pack_entry_at(idx);
    const u8 *src = (const u8 *)(PACK_BASE + e->offset);
    const u8 *end = src + e->size;

    __asm__ __volatile__("cli");
    gdt_install();
    idt_set_flags(0x80, (u32)syscall_stub, 0xEE);
    /* Task 0 already owns scheduler state; do not clear peers. */

    int slot = task_create(name, src, end);
    if (slot < 0) {
        gui_sys_print("[task] no free task slot.\n");
        __asm__ __volatile__("sti");
        return 0;
    }
    start_tasks(slot);
    return 1;
}

void run_task_demo(void) {
    int ai = pack_find("A.BIN");
    int bi = pack_find("B.BIN");
    int ci = pack_find("C.BIN");
    if (ai < 0 || bi < 0 || ci < 0) {
        gui_sys_print("[pack] A.BIN/B.BIN/C.BIN missing.\n");
        return;
    }
    struct pack_entry *ae = pack_entry_at(ai);
    struct pack_entry *be = pack_entry_at(bi);
    struct pack_entry *ce = pack_entry_at(ci);
    const u8 *as = (const u8 *)(PACK_BASE + ae->offset);
    const u8 *bs = (const u8 *)(PACK_BASE + be->offset);
    const u8 *cs = (const u8 *)(PACK_BASE + ce->offset);

    __asm__ __volatile__("cli");
    gdt_install();
    idt_set_flags(0x80, (u32)syscall_stub, 0xEE);
    /* Task 0 already owns scheduler state; do not clear peers. */

    int a = task_create("A.BIN", as, as + ae->size);
    int b = task_create("B.BIN", bs, bs + be->size);
    int c = task_create("C.BIN", cs, cs + ce->size);
    if (a < 0 || b < 0 || c < 0) {
        gui_sys_print("[task] cannot create tasks.\n");
        __asm__ __volatile__("sti");
        return;
    }

    start_tasks(a);
}

static int build_args_on_stack(int slot, u32 argc, const char *const *argv,
                               u32 stack_base, u32 stack_top) {
    if (argc > 32 || tasks[slot].stack_va == 0)
        return 0;
    u32 sp = stack_top;
    u32 ptrs[32];

    for (u32 i = 0; i < argc; i++) {
        const char *s = argv[i];
        if (!s)
            return 0;
        int len = 0;
        while (len < 128 && s[len])
            len++;
        if (len == 128)
            return 0;
        sp -= (u32)len + 1;
        if (sp < stack_base + 64)
            return 0;
        for (int j = 0; j <= len; j++) {
            u8 *dst = user_va_ptr(slot, sp + (u32)j);
            if (!dst)
                return 0;
            *dst = (u8)s[j];
        }
        ptrs[i] = sp;
    }
    sp &= ~3u;
    sp -= 4;
    u32 *zero = (u32 *)user_va_ptr(slot, sp);
    if (!zero)
        return 0;
    *zero = 0;
    for (int i = (int)argc - 1; i >= 0; i--) {
        sp -= 4;
        u32 *slotp = (u32 *)user_va_ptr(slot, sp);
        if (!slotp)
            return 0;
        *slotp = ptrs[i];
    }
    sp -= 4;
    if (sp < stack_base + 64)
        return 0;
    u32 *argcp = (u32 *)user_va_ptr(slot, sp);
    if (!argcp)
        return 0;
    *argcp = argc;
    ((u32 *)tasks[slot].esp)[11] = sp;
    return 1;
}

#define WARM_CACHE_SLOTS 2
struct warm_module {
    u8 used;
    char name[16];
    u32 phys;
    u32 pages;
    u32 size;
};
static struct warm_module warm_modules[WARM_CACHE_SLOTS];
volatile u32 user_module_last_ticks;
volatile u32 user_module_cold_ticks;
volatile u32 user_module_warm_ticks;
volatile u32 user_module_last_size;
volatile u8 user_module_cache_hit;

static int warm_name_eq(const char *a, const char *b) {
    for (int i = 0; i < 16; i++) {
        u8 ca = (u8)a[i];
        u8 cb = (u8)b[i];
        if (ca == 0 && cb == 0)
            return 1;
        if (ca == 0 || cb == 0)
            return 0;
        if (upper_char(ca) != upper_char(cb))
            return 0;
    }
    return 1;
}

static int warm_find(const char *name) {
    for (int i = 0; i < WARM_CACHE_SLOTS; i++) {
        if (warm_modules[i].used && warm_name_eq(warm_modules[i].name, name))
            return i;
    }
    return -1;
}

static void warm_store(const char *name, u32 size) {
    int slot = -1;
    for (int i = 0; i < WARM_CACHE_SLOTS; i++) {
        if (!warm_modules[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0 || size == 0 || size > BIG_CODE_MAX)
        return;
    u32 pages = (size + 0xFFFu) / 0x1000u;
    u32 phys = frame_alloc_pages(pages);
    if (!phys)
        return;
    u8 *dst = (u8 *)direct_ptr(phys);
    const u8 *src = (const u8 *)BIG_CODE_PHYS;
    for (u32 i = 0; i < size; i++)
        dst[i] = src[i];
    warm_modules[slot].used = 1;
    warm_modules[slot].phys = phys;
    warm_modules[slot].pages = pages;
    warm_modules[slot].size = size;
    for (int i = 0; i < 16; i++)
        warm_modules[slot].name[i] = name[i];
}

static void warm_copy(int slot, u32 size) {
    const u8 *src = (const u8 *)direct_ptr(warm_modules[slot].phys);
    u8 *dst = (u8 *)BIG_CODE_PHYS;
    for (u32 i = 0; i < size; i++)
        dst[i] = src[i];
}

static int run_user_module_ex(const char *name, int argc,
                              const char *const *argv) {
    u32 start_ticks = ticks;
    u32 size = 0;
    int cache = warm_find(name);
    int cache_hit = 0;
    int from_ramfs = 0;

    if (ramfile_find(name) >= 0) {
        /* RAMFS wins over the disk copy and the warm cache: never run a
         * stale image after a recompile. */
        if (!ramfs_load_file_to(name, (u8 *)BIG_CODE_PHYS, BIG_CODE_MAX,
                                &size)) {
            gui_sys_print("[user-mod] load failed.\n");
            return 0;
        }
        from_ramfs = 1;
    } else if (cache >= 0) {
        size = warm_modules[cache].size;
    } else if (!fs_load_file_to(name, (u8 *)BIG_CODE_PHYS, BIG_CODE_MAX, &size)) {
        gui_sys_print("[user-mod] load failed.\n");
        return 0;
    }

    __asm__ __volatile__("cli");
    gdt_install();
    idt_set_flags(0x80, (u32)syscall_stub, 0xEE);
    /* Task 0 already owns scheduler state; do not clear peers. */

    if (cache >= 0) {
        warm_copy(cache, size);
        cache_hit = 1;
    }

    struct module_header *h = (struct module_header *)BIG_CODE_PHYS;
    if (h->magic[0] != 'U' || h->magic[1] != 'M' ||
        h->magic[2] != 'P' || h->magic[3] != '1') {
        gui_sys_print("[user-mod] bad UMP1 header.\n");
        __asm__ __volatile__("sti");
        return 0;
    }
    if (h->image_size > size || h->entry_offset >= h->image_size ||
        h->image_size > BIG_CODE_MAX || h->bss_size > BIG_CODE_MAX - h->image_size) {
        gui_sys_print("[user-mod] bad sizes.\n");
        __asm__ __volatile__("sti");
        return 0;
    }

    if (!cache_hit && !from_ramfs)
        warm_store(name, size);

    int slot = task_create_big(name, h->image_size, h->bss_size, h->entry_offset);
    if (slot < 0) {
        gui_sys_print("[user-mod] cannot create big task.\n");
        __asm__ __volatile__("sti");
        return 0;
    }
    if (argc > 0 &&
        !build_args_on_stack(slot, (u32)argc, argv, BIG_USER_STACK,
                             BIG_USER_STACK + BIG_STACK_SIZE)) {
        tasks[slot].state = TASK_FREE;
        task_free_frames(slot);
        gui_sys_print("[user-mod] argv does not fit.\n");
        __asm__ __volatile__("sti");
        return 0;
    }
    u32 elapsed = ticks - start_ticks;
    user_module_last_ticks = elapsed;
    if (cache_hit)
        user_module_warm_ticks = elapsed;
    else
        user_module_cold_ticks = elapsed;
    user_module_last_size = size;
    user_module_cache_hit = (u8)cache_hit;
    start_tasks(slot);
    return 1;
}

int run_two_python(const char *a, const char *b) {
    u32 size = 0;
    if (!fs_load_file_to("MPY.BIN", (u8 *)BIG_CODE_PHYS, BIG_CODE_MAX, &size)) {
        gui_sys_print("[py2] MPY.BIN load failed.\n");
        return 0;
    }
    struct module_header *h = (struct module_header *)BIG_CODE_PHYS;
    if (h->magic[0] != 'U' || h->magic[1] != 'M' ||
        h->magic[2] != 'P' || h->magic[3] != '1' ||
        h->image_size > size || h->bss_size > BIG_CODE_MAX - h->image_size) {
        gui_sys_print("[py2] bad MPY.BIN header.\n");
        return 0;
    }
    int s0 = task_create_big("MPY.BIN", h->image_size, h->bss_size, h->entry_offset);
    int s1 = task_create_big("MPY.BIN", h->image_size, h->bss_size, h->entry_offset);
    if (s0 < 0 || s1 < 0) {
        if (s0 >= 0) { tasks[s0].state = TASK_FREE; task_free_frames(s0); }
        if (s1 >= 0) { tasks[s1].state = TASK_FREE; task_free_frames(s1); }
        gui_sys_print("[py2] no dual big slots.\n");
        return 0;
    }
    const char *a0[2] = { "py", a };
    const char *a1[2] = { "py", b };
    if (!build_args_on_stack(s0, 2, a0, BIG_USER_STACK,
                             BIG_USER_STACK + BIG_STACK_SIZE) ||
        !build_args_on_stack(s1, 2, a1, BIG_USER_STACK,
                             BIG_USER_STACK + BIG_STACK_SIZE)) {
        tasks[s0].state = TASK_FREE;
        tasks[s1].state = TASK_FREE;
        task_free_frames(s0);
        task_free_frames(s1);
        gui_sys_print("[py2] args failed.\n");
        return 0;
    }
    tasks[s0].console_id = 0;
    tasks[s1].console_id = 1;
    __asm__ __volatile__("cli");
    start_tasks(s0);
    __asm__ __volatile__("sti");
    return 1;
}

int run_three_python(const char *a, const char *b, const char *c) {
    u32 size = 0;
    if (!fs_load_file_to("MPY.BIN", (u8 *)BIG_CODE_PHYS, BIG_CODE_MAX, &size)) {
        gui_sys_print("[py3] MPY.BIN load failed.\n");
        return 0;
    }
    struct module_header *h = (struct module_header *)BIG_CODE_PHYS;
    if (h->magic[0] != 'U' || h->magic[1] != 'M' ||
        h->magic[2] != 'P' || h->magic[3] != '1' ||
        h->image_size > size || h->bss_size > BIG_CODE_MAX - h->image_size) {
        gui_sys_print("[py3] bad MPY.BIN header.\n");
        return 0;
    }
    int slots[3];
    for (int i = 0; i < 3; i++)
        slots[i] = task_create_big("MPY.BIN", h->image_size, h->bss_size, h->entry_offset);
    for (int i = 0; i < 3; i++)
        if (slots[i] < 0) {
            for (int j = 0; j < 3; j++)
                if (slots[j] >= 0) {
                    tasks[slots[j]].state = TASK_FREE;
                    task_free_frames(slots[j]);
                }
            gui_sys_print("[py3] no three big slots.\n");
            return 0;
        }
    const char *av[3][2] = { { "py", a }, { "py", b }, { "py", c } };
    for (int i = 0; i < 3; i++) {
        if (!build_args_on_stack(slots[i], 2, av[i], BIG_USER_STACK,
                                 BIG_USER_STACK + BIG_STACK_SIZE)) {
            for (int j = 0; j < 3; j++) {
                tasks[slots[j]].state = TASK_FREE;
                task_free_frames(slots[j]);
            }
            gui_sys_print("[py3] args failed.\n");
            return 0;
        }
        tasks[slots[i]].console_id = (u8)i;
    }
    __asm__ __volatile__("cli");
    start_tasks(slots[0]);
    __asm__ __volatile__("sti");
    return 1;
}

int run_four_python(const char *a, const char *b, const char *c, const char *d) {
    u32 size = 0;
    if (!fs_load_file_to("MPY.BIN", (u8 *)BIG_CODE_PHYS, BIG_CODE_MAX, &size))
        return 0;
    struct module_header *h = (struct module_header *)BIG_CODE_PHYS;
    if (h->magic[0] != 'U' || h->magic[1] != 'M' ||
        h->magic[2] != 'P' || h->magic[3] != '1' ||
        h->image_size > size || h->bss_size > BIG_CODE_MAX - h->image_size)
        return 0;
    int slots[4];
    for (int i = 0; i < 4; i++)
        slots[i] = task_create_big("MPY.BIN", h->image_size, h->bss_size,
                                   h->entry_offset);
    for (int i = 0; i < 4; i++)
        if (slots[i] < 0) {
            for (int j = 0; j < 4; j++)
                if (slots[j] >= 0) {
                    tasks[slots[j]].state = TASK_FREE;
                    task_free_frames(slots[j]);
                }
            gui_sys_print("[py4] no four big slots.\n");
            return 0;
        }
    const char *av[4][2] = { { "py", a }, { "py", b }, { "py", c }, { "py", d } };
    for (int i = 0; i < 4; i++) {
        if (!build_args_on_stack(slots[i], 2, av[i], BIG_USER_STACK,
                                 BIG_USER_STACK + BIG_STACK_SIZE)) {
            for (int j = 0; j < 4; j++) {
                tasks[slots[j]].state = TASK_FREE;
                task_free_frames(slots[j]);
            }
            gui_sys_print("[py4] args failed.\n");
            return 0;
        }
        tasks[slots[i]].console_id = (u8)i;
    }
    __asm__ __volatile__("cli");
    start_tasks(slots[0]);
    __asm__ __volatile__("sti");
    return 1;
}

int run_n_python(const char *const *names, int n) {
    if (n < 1 || n > 8)
        return 0;
    u32 size = 0;
    if (!fs_load_file_to("MPY.BIN", (u8 *)BIG_CODE_PHYS, BIG_CODE_MAX, &size))
        return 0;
    struct module_header *h = (struct module_header *)BIG_CODE_PHYS;
    if (h->magic[0] != 'U' || h->magic[1] != 'M' ||
        h->magic[2] != 'P' || h->magic[3] != '1' ||
        h->image_size > size || h->bss_size > BIG_CODE_MAX - h->image_size)
        return 0;
    int slots[8];
    for (int i = 0; i < n; i++) {
        slots[i] = task_create_big("MPY.BIN", h->image_size, h->bss_size,
                                   h->entry_offset);
        if (slots[i] >= 0)
            tasks[slots[i]].console_id = (u8)i;
    }
    for (int i = 0; i < n; i++)
        if (slots[i] < 0) {
            for (int j = 0; j < n; j++)
                if (slots[j] >= 0) {
                    tasks[slots[j]].state = TASK_FREE;
                    task_free_frames(slots[j]);
                }
            return 0;
        }
    const char *av[8][2];
    for (int i = 0; i < n; i++) {
        av[i][0] = "py";
        av[i][1] = names[i];
    }
    for (int i = 0; i < n; i++) {
        if (!build_args_on_stack(slots[i], 2, av[i], BIG_USER_STACK,
                                 BIG_USER_STACK + BIG_STACK_SIZE)) {
            for (int j = 0; j < n; j++) {
                tasks[slots[j]].state = TASK_FREE;
                task_free_frames(slots[j]);
            }
            return 0;
        }
        tasks[slots[i]].console_id = (u8)i;
    }
    __asm__ __volatile__("cli");
    start_tasks(slots[0]);
    __asm__ __volatile__("sti");
    return 1;
}

int run_user_module(const char *name) {
    return run_user_module_ex(name, 0, 0);
}

int run_user_module_args(const char *name, int argc, const char *const *argv) {
    return run_user_module_ex(name, argc, argv);
}

int exec_file_args(const char *name, int argc, const char *const *argv) {
    u32 size = 0;
    u8 *base = (u8 *)BIG_CODE_PHYS;

    if (!vfs_load_file_to(name, base, BIG_CODE_MAX, &size, 0)) {
        gui_sys_print("[disk] file not found or read failed.\n");
        return 0;
    }
    if (size == 0) {
        gui_sys_print("[disk] empty file.\n");
        return 0;
    }

    __asm__ __volatile__("cli");
    gdt_install();
    idt_set_flags(0x80, (u32)syscall_stub, 0xEE);
    /* Task 0 already owns scheduler state; do not clear peers. */

    int slot = -1;
    if (image_is_ump1(base, size)) {
        struct module_header *h = (struct module_header *)base;
        if (!ump1_header_ok(h, size)) {
            gui_sys_print("[disk] bad UMP1 header.\n");
            __asm__ __volatile__("sti");
            return 0;
        }
        slot = task_create_big(name, h->image_size, h->bss_size,
                               h->entry_offset);
        if (slot >= 0 && argc > 0 &&
            !build_args_on_stack(slot, (u32)argc, argv, BIG_USER_STACK,
                                 BIG_USER_STACK + BIG_STACK_SIZE)) {
            tasks[slot].state = TASK_FREE;
            task_free_frames(slot);
            gui_sys_print("[disk] argv does not fit.\n");
            __asm__ __volatile__("sti");
            return 0;
        }
    } else if (size <= FS_MAX_FILE) {
        slot = task_create(name, base, base + size);
    } else {
        gui_sys_print("[disk] binary too large (need UMP1).\n");
        __asm__ __volatile__("sti");
        return 0;
    }
    if (slot < 0) {
        gui_sys_print("[task] no free task slot.\n");
        __asm__ __volatile__("sti");
        return 0;
    }
    start_tasks(slot);
    return 1;
}

int exec_disk(const char *name) {
    return exec_file_args(name, 0, 0);
}

static int name16_eq(const char *a, const char *b) {
    for (int i = 0; i < 16; i++) {
        u8 ca = (u8)a[i];
        u8 cb = (u8)b[i];
        if (ca == 0 && cb == 0)
            return 1;
        if (ca == 0 || cb == 0)
            return 0;
        if (upper_char(ca) != upper_char(cb))
            return 0;
    }
    return 1;
}

static void wake_pipe_tasks(int id) {
    for (int t = 1; t < task_cap; t++) {
        if (tasks[t].state == TASK_BLOCKED && tasks[t].wait_pipe == (u32)(id + 1)) {
            tasks[t].wait_pipe = 0;
            tasks[t].wait_pipe_write = 0;
            tasks[t].state = TASK_READY;
        }
    }
}

static int pipe_wait_ready(int id, int write);

static void pipe_free(int id) {
    if (id < 0 || id >= NPIPES)
        return;
    if (pipes[id].phys) {
        frame_free_pages(pipes[id].phys, 1);
        pipes[id].phys = 0;
    }
    pipes[id].head = pipes[id].tail = pipes[id].count = 0;
    pipes[id].open_w = pipes[id].open_r = 0;
    pipes[id].used = 0;
    wake_pipe_tasks(id);
}

static void pipe_close_write(int id) {
    if (id < 0 || id >= NPIPES)
        return;
    pipes[id].open_w = 0;
    if (!pipes[id].open_r)
        pipe_free(id);
    else
        wake_pipe_tasks(id);
}

static void pipe_close_read(int id) {
    if (id < 0 || id >= NPIPES)
        return;
    pipes[id].open_r = 0;
    if (!pipes[id].open_w)
        pipe_free(id);
    else
        wake_pipe_tasks(id);
}

static int pipe_reset(int id) {
    if (id < 0 || id >= NPIPES)
        return -1;
    pipe_free(id);
    pipes[id].used = 1;
    pipes[id].open_w = 1;
    pipes[id].open_r = 1;
    return 0;
}

int kernel_pipe_alloc(void) {
    for (int id = 0; id < NPIPES; id++)
        if (!pipes[id].used)
            return pipe_reset(id) == 0 ? id : -1;
    return -1;
}

static int pipe_write(int id, const u8 *buf, u32 len) {
    if (id < 0 || id >= NPIPES || !pipes[id].used ||
        !pipes[id].open_w || !pipes[id].open_r)
        return -1;
    if (!pipes[id].phys) {
        pipes[id].phys = frame_alloc_pages(1);
        if (!pipes[id].phys)
            return -1;
        frame_zero(pipes[id].phys, 1);
    }
    u8 *p = (u8 *)direct_ptr(pipes[id].phys);
    u32 n = 0;
    while (n < len && pipes[id].count < 0x1000u) {
        p[pipes[id].head] = buf[n++];
        pipes[id].head = (u16)((pipes[id].head + 1) & 0xFFF);
        pipes[id].count++;
    }
    if (n)
        wake_pipe_tasks(id);
    return (int)n;
}

static int pipe_read(int id, u8 *buf, u32 max) {
    if (id < 0 || id >= NPIPES || !pipes[id].used || !pipes[id].open_r)
        return -1;
    if (!pipes[id].phys) {
        if (!pipes[id].open_w && pipes[id].count == 0)
            return -1;
        return 0;
    }
    u8 *p = (u8 *)direct_ptr(pipes[id].phys);
    u32 n = 0;
    while (n < max && pipes[id].count) {
        buf[n++] = p[pipes[id].tail];
        pipes[id].tail = (u16)((pipes[id].tail + 1) & 0xFFF);
        pipes[id].count--;
    }
    if (n)
        wake_pipe_tasks(id);
    if (n == 0 && !pipes[id].open_w)
        return -1;
    return (int)n;
}

static int pipe_wait_ready(int id, int write) {
    if (id < 0 || id >= NPIPES || !pipes[id].used)
        return 1;
    if (write)
        return !pipes[id].open_r || pipes[id].count < 0x1000u;
    return pipes[id].count > 0 || !pipes[id].open_w;
}

static void pipe_release_task(int slot) {
    if (slot < 0 || slot >= task_cap)
        return;
    u32 w = tasks[slot].pipe_w_id;
    u32 r = tasks[slot].pipe_r_id;
    if (w && w <= NPIPES)
        pipe_close_write((int)w - 1);
    if (r && r <= NPIPES)
        pipe_close_read((int)r - 1);
    tasks[slot].pipe_w_id = 0;
    tasks[slot].pipe_r_id = 0;
}

static int pipe_block_op(int id, int write, u8 *buf, u32 len) {
    if (id < 0 || id >= NPIPES)
        return -1;
    if (len == 0)
        return 0;
    for (;;) {
        u32 flags = irq_save_flags();
        int n = write ? pipe_write(id, buf, len)
                      : pipe_read(id, buf, len);
        if (n != 0 || pipe_wait_ready(id, write)) {
            if (write && n == 0 && !pipes[id].open_r)
                n = -1;
            irq_restore_flags(flags);
            return n;
        }
        if (current_task < 0 || current_task >= task_cap) {
            irq_restore_flags(flags);
            return -1;
        }
        tasks[current_task].wait_pipe = (u32)(id + 1);
        tasks[current_task].wait_pipe_write = (u8)write;
        tasks[current_task].state = TASK_BLOCKED;
        irq_restore_flags(flags);
        sched_yield();
        tasks[current_task].wait_pipe = 0;
        tasks[current_task].wait_pipe_write = 0;
    }
}

int kernel_pipe_abort(int id) {
    if (id < 0 || id >= NPIPES)
        return 0;
    pipe_close_write(id);
    pipe_close_read(id);
    return 1;
}

int kernel_pipe_bind_console(int console, int id, int write) {
    if (id < 0 || id >= NPIPES || console < 0)
        return 0;
    for (int t = 1; t < task_cap; t++) {
        if (tasks[t].state == TASK_FREE || tasks[t].state == TASK_DONE)
            continue;
        if ((int)tasks[t].console_id != console)
            continue;
        if (write)
            tasks[t].pipe_w_id = (u8)(id + 1);
        else
            tasks[t].pipe_r_id = (u8)(id + 1);
        return 1;
    }
    if (write)
        pipe_close_write(id);
    else
        pipe_close_read(id);
    return 0;
}

static int ramfile_find(const char *name) {
    for (int i = 0; i < MAX_RAMFILES; i++) {
        if (ramfiles[i].used && name16_eq(ramfiles[i].name, name))
            return i;
    }
    return -1;
}

static void ramfile_free_from(struct ramfile *r, int first) {
    for (int p = first; p < RAMFILE_MAX_PAGES; p++) {
        if (r->pages[p]) {
            frame_free_pages(r->pages[p], 1);
            r->pages[p] = 0;
        }
    }
}

static int ramfile_rw(struct ramfile *r, u32 off, u8 *buf, u32 len, int wr) {
    if (!r || off + len < off || off + len > RAMFILE_MAX_PAGES * 0x1000u)
        return -1;
    if (wr && fs_snapshot_busy)
        return -1;
    u32 done = 0;
    while (done < len) {
        u32 pos = off + done;
        u32 page = pos >> 12;
        u32 in = pos & 0xFFFu;
        u32 chunk = 0x1000u - in;
        if (chunk > len - done)
            chunk = len - done;
        if (wr && !r->pages[page]) {
            u32 p = frame_alloc_pages(1);
            if (!p)
                return (int)done;
            frame_zero(p, 1);
            r->pages[page] = p;
        }
        if (r->pages[page]) {
            u8 *dst = (u8 *)direct_ptr(r->pages[page]) + in;
            if (wr)
                for (u32 i = 0; i < chunk; i++)
                    dst[i] = buf[done + i];
            else
                for (u32 i = 0; i < chunk; i++)
                    buf[done + i] = dst[i];
        } else if (!wr) {
            for (u32 i = 0; i < chunk; i++)
                buf[done + i] = 0;
        }
        done += chunk;
    }
    return (int)done;
}

/* RAMFS-first load helper: a matching RAMFS name always shadows MiniFS. */
static int ramfs_load_file_to(const char *name, u8 *dst, u32 max,
                              u32 *out_size) {
    int idx = ramfile_find(name);
    if (idx < 0)
        return 0;
    struct ramfile *r = &ramfiles[idx];
    if (r->size > max)
        return 0;
    u32 n = r->size;
    if (n && ramfile_rw(r, 0, dst, n, 0) != (int)n)
        return 0;
    *out_size = n;
    return 1;
}

static int vfs_load_file_to(const char *name, u8 *dst, u32 max, u32 *out_size,
                            int *out_from_ramfs) {
    if (out_from_ramfs)
        *out_from_ramfs = 0;
    if (ramfile_find(name) >= 0) {
        if (!ramfs_load_file_to(name, dst, max, out_size))
            return 0;
        if (out_from_ramfs)
            *out_from_ramfs = 1;
        return 1;
    }
    return fs_load_file_to(name, dst, max, out_size);
}

static int ramfile_create(const char *name) {
    if (fs_snapshot_busy)
        return -1;
    int idx = -1;
    for (int i = 0; i < MAX_RAMFILES; i++) {
        if (!ramfiles[i].used) {
            idx = i;
            break;
        }
    }
    if (idx < 0)
        return -1;
    ramfiles[idx].used = 1;
    ramfiles[idx].size = 0;
    for (int p = 0; p < RAMFILE_MAX_PAGES; p++)
        ramfiles[idx].pages[p] = 0;
    for (int i = 0; i < 16; i++)
        ramfiles[idx].name[i] = 0;
    for (int i = 0; i < 15 && name[i]; i++)
        ramfiles[idx].name[i] = name[i];
    return idx;
}

static void ramfile_delete(int idx) {
    if (fs_snapshot_busy)
        return;
    if (idx < 0 || idx >= MAX_RAMFILES || !ramfiles[idx].used)
        return;
    ramfile_free_from(&ramfiles[idx], 0);
    ramfiles[idx].used = 0;
    for (int t = 0; t < task_cap; t++) {
        if (!tasks[t].fds)
            continue;
        for (int f = 0; f < MAX_FDS; f++) {
            if (tasks[t].fds[f].used && tasks[t].fds[f].kind == 1 &&
                tasks[t].fds[f].ram_index == idx)
                tasks[t].fds[f].used = 0;
        }
    }
}

static int make_demo_script(const char *name, char letter) {
    int old = ramfile_find(name);
    if (old >= 0)
        ramfile_delete(old);
    int idx = ramfile_create(name);
    if (idx < 0)
        return 0;
    char body[128];
    int n = 0;
    const char *p = "import time\nfor i in range(8):\n    print('";
    while (*p) body[n++] = *p++;
    body[n++] = letter;
    body[n++] = '\''; body[n++] = ','; body[n++] = ' '; body[n++] = 'i';
    body[n++] = ')'; body[n++] = '\n';
    p = "    time.sleep_ms(150)\n";
    while (*p) body[n++] = *p++;
    struct ramfile *r = &ramfiles[idx];
    if (ramfile_rw(r, 0, (u8 *)body, (u32)n, 1) != n) {
        ramfile_delete(idx);
        return 0;
    }
    r->size = (u32)n;
    return 1;
}

int prepare_long_script(void) {
    int old = ramfile_find("LONG.PY");
    if (old >= 0)
        ramfile_delete(old);
    int idx = ramfile_create("LONG.PY");
    if (idx < 0)
        return 0;
    char body[128];
    int n = 0;
    const char *p = "import time\nfor i in range(1000):\n    print('L', i)\n";
    while (*p) body[n++] = *p++;
    p = "    time.sleep_ms(100)\n";
    while (*p) body[n++] = *p++;
    struct ramfile *r = &ramfiles[idx];
    if (ramfile_rw(r, 0, (u8 *)body, (u32)n, 1) != n) {
        ramfile_delete(idx);
        return 0;
    }
    r->size = (u32)n;
    return 1;
}

int run_demo_n(int n) {
    static const char *files[8] = {
        "PYA.PY", "PYB.PY", "PYC.PY", "PYD.PY",
        "PYE.PY", "PYF.PY", "PYG.PY", "PYH.PY"
    };
    if (n < 1 || n > 8)
        return 0;
    for (int i = 0; i < n; i++)
        if (!make_demo_script(files[i], (char)('A' + i)))
            return 0;
    return run_n_python(files, n);
}

static int make_cpu_script(const char *name) {
    int old = ramfile_find(name);
    if (old >= 0)
        ramfile_delete(old);
    int idx = ramfile_create(name);
    if (idx < 0)
        return 0;
    const char *body = "i = 0\nwhile True:\n    i += 1\n";
    int n = 0;
    while (body[n]) n++;
    struct ramfile *r = &ramfiles[idx];
    if (ramfile_rw(r, 0, (u8 *)body, (u32)n, 1) != n) {
        ramfile_delete(idx);
        return 0;
    }
    r->size = (u32)n;
    return 1;
}

int prepare_cpu_scripts(void) {
    return make_cpu_script("CPUA.PY") && make_cpu_script("CPUB.PY");
}

static int make_pipe_script(const char *name, const char *body) {
    int old = ramfile_find(name);
    if (old >= 0)
        return 1;               /* Reuse live scripts; a running task may hold them. */
    int idx = ramfile_create(name);
    if (idx < 0)
        return 0;
    int n = 0;
    while (body[n]) n++;
    struct ramfile *r = &ramfiles[idx];
    if (ramfile_rw(r, 0, (u8 *)body, (u32)n, 1) != n) {
        ramfile_delete(idx);
        return 0;
    }
    r->size = (u32)n;
    return 1;
}

int prepare_pipe_scripts(void) {
    const char *prod =
        "import time, sys\n"
        "from minios import pipe_write, pipe_close\n"
        "pid = int(sys.argv[1])\n"
        "for i in range(100):\n"
        "    if pipe_write(pid, 'line %d\\n' % i) < 0:\n"
        "        break\n"
        "    time.sleep_ms(100)\n"
        "pipe_close(pid)\n";
    const char *cons =
        "import sys\n"
        "from minios import pipe_read\n"
        "pid = int(sys.argv[1])\n"
        "while True:\n"
        "    d = pipe_read(pid, 64)\n"
        "    if not d:\n"
        "        break\n"
        "    print(d)\n";
    return make_pipe_script("PPROD.PY", prod) &&
           make_pipe_script("PCONS.PY", cons);
}

int prepare_gc_script(void) {
    return make_pipe_script("GCTEST.PY",
        "print('gc start')\n"
        "a = bytearray(8 * 1024 * 1024)\n"
        "a[0] = 65\n"
        "a[-1] = 66\n"
        "print('gc ok', len(a), a[0], a[-1])\n");
}

int prepare_regress_scripts(void) {
    const char *child =
        "from minios import exit_status\n"
        "exit_status(5)\n";
    const char *reg =
        "import os\n"
        "from minios import spawn, wait, serial_write\n"
        "def R(*a):\n"
        "    s = 'REGRESS ' + ' '.join(str(x) for x in a)\n"
        "    print(s)\n"
        "    serial_write((s + '\\n').encode())\n"
        "try:\n"
        "    a = open('BIGA.TXT', 'w')\n"
        "    a.write('A' * 33000)\n"
        "    a.close()\n"
        "    b = open('BIGB.TXT', 'w')\n"
        "    b.write('B' * 33000)\n"
        "    b.close()\n"
        "    try:\n"
        "        os.sync()\n"
        "        R('SYNC_UNEXPECTED_OK')\n"
        "    except Exception:\n"
        "        R('SYNC_FAIL')\n"
        "finally:\n"
        "    try: os.unlink('BIGA.TXT')\n"
        "    except Exception: pass\n"
        "    try: os.unlink('BIGB.TXT')\n"
        "    except Exception: pass\n"
        "f = open('APP.TXT', 'w')\n"
        "f.write('0')\n"
        "f.close()\n"
        "a = open('APP.TXT', 'a')\n"
        "b = open('APP.TXT', 'a')\n"
        "a.write('A')\n"
        "b.write('B')\n"
        "a.close()\n"
        "b.close()\n"
        "f = open('APP.TXT')\n"
        "R('APPEND', f.read())\n"
        "f.close()\n"
        "f = open('SEEK.TXT', 'w')\n"
        "f.write('12')\n"
        "f.close()\n"
        "a = open('SEEK.TXT', 'r+')\n"
        "b = open('SEEK.TXT', 'a')\n"
        "b.write('3')\n"
        "b.close()\n"
        "R('SEEKEND', a.seek(0, 2))\n"
        "a.close()\n"
        "p1 = spawn('CHILD5.PY')\n"
        "p2 = spawn('CHILD5.PY')\n"
        "R('CHILDREN', p1, p2)\n"
        "s1 = wait(p1, 0)\n"
        "s2 = wait(p2, 0)\n"
        "R('STATUSES', s1, s2)\n";
    return make_pipe_script("CHILD5.PY", child) &&
           make_pipe_script("REGRESS.PY", reg);
}

int prepare_lockhold_script(void) {
    return make_pipe_script("LOCKHOLD.PY",
        "from minios import fdc_lock_test\n"
        "print('holder start')\n"
        "r = fdc_lock_test(3000)\n"
        "print('holder done', r)\n");
}

int prepare_locktest_script(void) {
    return make_pipe_script("LOCKTEST.PY",
        "from minios import fdc_lock_test\n"
        "print('lock test start')\n"
        "r = fdc_lock_test(10000)\n"
        "print('lock test done', r)\n");
}

int prepare_fdctest_script(void) {
    return make_pipe_script("FDCTEST.PY",
        "from minios import fdc_test\n"
        "print('fdc test start')\n"
        "r = fdc_test(10000)\n"
        "print('fdc test woke', r)\n");
}

int prepare_disk_script(void) {
    return make_pipe_script("DISKREAD.PY",
        "import time\n"
        "from minios import read_file\n"
        "print('disk start')\n"
        "time.sleep_ms(5000)\n"
        "d = read_file('HELLO.BIN')\n"
        "print('disk len', len(d))\n");
}

int prepare_proc_scripts(void) {
    const char *child =
        "import time\n"
        "time.sleep_ms(600)\n"
        "print('child done')\n";
    const char *parent =
        "import time\n"
        "from minios import spawn, wait\n"
        "p = spawn('CHILD.PY')\n"
        "print('spawn', p)\n"
        "print('nohang', wait(p, 1))\n"
        "time.sleep_ms(200)\n"
        "print('wait', wait(p, 0))\n";
    const char *orphan =
        "import time\n"
        "print('orphan start')\n"
        "time.sleep_ms(800)\n"
        "print('orphan done')\n";
    const char *parento =
        "from minios import spawn\n"
        "p = spawn('ORPHAN.PY')\n"
        "print('parent spawned', p)\n";
    return make_pipe_script("CHILD.PY", child) &&
           make_pipe_script("PARENT.PY", parent) &&
           make_pipe_script("ORPHAN.PY", orphan) &&
           make_pipe_script("PARENTO.PY", parento);
}

int prepare_full_scripts(void) {
    const char *prod =
        "import sys\n"
        "from minios import pipe_write, pipe_close\n"
        "pid = int(sys.argv[1])\n"
        "data = b'X' * 512\n"
        "for i in range(16):\n"
        "    if pipe_write(pid, data) < 0:\n"
        "        break\n"
        "pipe_close(pid)\n";
    const char *cons =
        "import sys, time\n"
        "from minios import pipe_read, pipe_close_read\n"
        "pid = int(sys.argv[1])\n"
        "time.sleep_ms(5000)\n"
        "while True:\n"
        "    d = pipe_read(pid, 512)\n"
        "    if not d:\n"
        "        break\n"
        "pipe_close_read(pid)\n";
    return make_pipe_script("PFULLP.PY", prod) &&
           make_pipe_script("PFULLC.PY", cons);
}

int run_four_demo(void) {
    return run_demo_n(4);
}

int serial_upload_file(const char *name) {
    int idx = ramfile_find(name);
    if (idx < 0)
        idx = ramfile_create(name);
    if (idx < 0)
        return 0;
    struct ramfile *r = &ramfiles[idx];
    ramfile_free_from(r, 0);
    r->size = 0;
    u8 tmp[256];
    u32 n = 0, tn = 0;
    u8 old_busy = runtime_busy;
    runtime_busy = 1;
    gui_sys_print("[serial] send bytes, end with Ctrl-Z...\n");
    while (n + tn < RAMFILE_MAX_PAGES * 0x1000u) {
        int ch = serial_getc();
        if (ch < 0 || ch == 0x1A)
            break;
        tmp[tn++] = (u8)ch;
        if (tn == sizeof(tmp)) {
            if (ramfile_rw(r, n, tmp, tn, 1) != (int)tn)
                break;
            n += tn;
            tn = 0;
        }
    }
    if (tn && ramfile_rw(r, n, tmp, tn, 1) == (int)tn)
        n += tn;
    runtime_busy = old_busy;
    r->size = n;
    gui_sys_print("[serial] upload done: ");
    if (n >= 100000) gui_sys_print("?");
    else {
        char b[12];
        int i = 0;
        u32 v = n;
        if (!v) b[i++] = '0';
        while (v) { b[i++] = (char)('0' + v % 10); v /= 10; }
        while (i) gui_sys_print((char[]){ b[--i], 0 });
    }
    gui_sys_print(" bytes.\n");
    return 1;
}

int serial_download_file(const char *name) {
    int idx = ramfile_find(name);
    if (idx < 0)
        return 0;
    struct ramfile *r = &ramfiles[idx];
    serial_puts("<<<FILE ");
    for (int i = 0; i < 16 && r->name[i]; i++)
        serial_putc(r->name[i]);
    serial_puts(">>>\n");
    u8 tmp[256];
    for (u32 i = 0; i < r->size; ) {
        u32 n = r->size - i;
        if (n > sizeof(tmp)) n = sizeof(tmp);
        if (ramfile_rw(r, i, tmp, n, 0) != (int)n)
            break;
        for (u32 j = 0; j < n; j++)
            serial_putc((char)tmp[j]);
        i += n;
    }
    serial_puts("\n<<<END>>>\n");
    return 1;
}

void serial_info(void) {
    serial_puts("MiniOS COM1 115200 8N1 ready\r\n");
}

void serial_u32(u32 v) {
    char b[12];
    int n = 0;
    do { b[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n)
        serial_putc(b[--n]);
}

void serial_selftest_result(int ok) {
    serial_puts(ok ? "[selftest] PASS\r\n" : "[selftest] FAIL\r\n");
}

void serial_metrics(void) {
    extern u32 kernel_free_frames(void);
    extern u32 kernel_total_frames(void);
    serial_puts("[metrics] free=");
    serial_u32(kernel_free_frames());
    serial_puts(" total=");
    serial_u32(kernel_total_frames());
    serial_puts(" heap=");
    serial_u32(heap_used);
    serial_puts("\r\n");
}

/* -----------------------------------------------------------------------------
 * SaveFS: snapshot RAMFS into the tail of the floppy.
 * ----------------------------------------------------------------------------- */
#define SAVE_HEADER_LBA  2560u
#define SAVE_DATA_LBA    2561u
#define SAVE_DATA_SECTORS 259u
#define SAVE_MAGIC "MSV1"

struct savefs_entry {
    char name[16];
    u32 size;
    u32 start_lba;
    u32 crc;
} __attribute__((packed));

struct savefs_header {
    char magic[4];
    u32 generation;
    u32 count;
    u32 header_crc;
    struct savefs_entry entries[16];
} __attribute__((packed));

static u32 savefs_generation;

static u32 crc32_bytes(u32 crc, const u8 *p, u32 len) {
    for (u32 i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}

static u32 crc32_finish(u32 crc) {
    return ~crc;
}

#if 0
static int savefs_sync(void) {
    struct savefs_header h;
    u8 *hp = (u8 *)&h;
    for (u32 i = 0; i < sizeof(h); i++)
        hp[i] = 0;
    h.magic[0] = 'M'; h.magic[1] = 'S'; h.magic[2] = 'V'; h.magic[3] = '2';
    h.generation = ++savefs_generation;

    u32 total = 0, count = 0;
    for (int i = 0; i < MAX_RAMFILES; i++) {
        if (!ramfiles[i].used)
            continue;
        if (count >= 16 || ramfiles[i].size > RAMFILE_MAX_PAGES * 0x1000u)
            return 0;
        total += (ramfiles[i].size + 511u) / 512u;
        count++;
    }
    if (total > SAVE_DATA_SECTORS)
        return 0;

    u32 lba = SAVE_DATA_LBA;
    count = 0;
    for (int i = 0; i < MAX_RAMFILES; i++) {
        if (!ramfiles[i].used)
            continue;
        struct ramfile *r = &ramfiles[i];
        u32 sectors = (r->size + 511u) / 512u;
        u32 crc = 0xFFFFFFFFu;
        for (u32 s = 0; s < sectors; s++) {
            for (int j = 0; j < 512; j++)
                fs_sector[j] = 0;
            u32 chunk = r->size - s * 512u;
            if (chunk > 512) chunk = 512;
            if (ramfile_rw(r, s * 512u, fs_sector, chunk, 0) != (int)chunk)
                return 0;
            crc = crc32_bytes(crc, fs_sector, chunk);
            if (!fdc_write_sector(lba + s, fs_sector))
                return 0;
        }
        for (int j = 0; j < 16; j++)
            h.entries[count].name[j] = r->name[j];
        h.entries[count].size = r->size;
        h.entries[count].start_lba = lba;
        h.entries[count].crc = crc32_finish(crc);
        count++;
        lba += sectors;
    }
    h.count = count;
    h.header_crc = crc32_finish(crc32_bytes(0xFFFFFFFFu, hp, sizeof(h)));
    for (int j = 0; j < 512; j++)
        fs_sector[j] = 0;
    for (u32 j = 0; j < sizeof(h); j++)
        fs_sector[j] = hp[j];
    if (!fdc_write_sector(SAVE_HEADER_LBA, fs_sector))
        return 0;
    return 1;
}

static int savefs_restore(void) {
    if (!fdc_read_sector(SAVE_HEADER_LBA, fs_sector))
        return 0;
    struct savefs_header h;
    u8 *hp = (u8 *)&h;
    for (u32 i = 0; i < sizeof(h); i++)
        hp[i] = fs_sector[i];
    if (h.magic[0] != 'M' || h.magic[1] != 'S' ||
        h.magic[2] != 'V' || (h.magic[3] != '1' && h.magic[3] != '2'))
        return 0;
    u32 stored = h.header_crc;
    h.header_crc = 0;
    u32 hdr_len = (h.magic[3] == '1')
        ? (u32)(16 + 8 * sizeof(struct savefs_entry))
        : (u32)sizeof(h);
    if (crc32_finish(crc32_bytes(0xFFFFFFFFu, hp, hdr_len)) != stored)
        return 0;
    u32 max_entries = (h.magic[3] == '1') ? 8u : 16u;
    if (h.count > max_entries)
        return 0;

    for (u32 i = 0; i < h.count; i++) {
        if (h.entries[i].size > RAMFILE_MAX_PAGES * 0x1000u)
            return 0;
        u32 sectors = (h.entries[i].size + 511u) / 512u;
        if (h.entries[i].start_lba < SAVE_DATA_LBA ||
            h.entries[i].start_lba + sectors > SAVE_DATA_LBA + SAVE_DATA_SECTORS)
            return 0;
    }

    int made[16];
    int made_count = 0;
    for (u32 i = 0; i < h.count; i++) {
        int idx = ramfile_create(h.entries[i].name);
        if (idx < 0)
            goto fail;
        made[made_count++] = idx;
        struct ramfile *r = &ramfiles[idx];
        u32 sectors = (h.entries[i].size + 511u) / 512u;
        u32 crc = 0xFFFFFFFFu;
        u32 done = 0;
        for (u32 s = 0; s < sectors; s++) {
            if (!fdc_read_sector(h.entries[i].start_lba + s, fs_sector))
                goto fail;
            u32 chunk = h.entries[i].size - done;
            if (chunk > 512) chunk = 512;
            if (ramfile_rw(r, done, fs_sector, chunk, 1) != (int)chunk)
                goto fail;
            crc = crc32_bytes(crc, fs_sector, chunk);
            done += chunk;
        }
        if (crc32_finish(crc) != h.entries[i].crc)
            goto fail;
        r->size = h.entries[i].size;
    }
    return 1;

fail:
    for (int i = 0; i < made_count; i++)
        ramfile_delete(made[i]);
    return 0;
}

#endif

#define SAVE3_HEADER_A 2560u
#define SAVE3_DATA_A   2561u
#define SAVE3_HEADER_B 2690u
#define SAVE3_DATA_B   2691u
#define SAVE3_SECTORS  129u
static int savefs_active = -1;

static int save3_read_header(u32 lba, struct savefs_header *h) {
    if (!fdc_read_sector(lba, fs_sector))
        return 0;
    u8 *hp = (u8 *)h;
    for (u32 i = 0; i < sizeof(*h); i++)
        hp[i] = fs_sector[i];
    return 1;
}

static int save3_check(struct savefs_header *h, u32 data_lba) {
    if (h->magic[0] != 'M' || h->magic[1] != 'S' ||
        h->magic[2] != 'V' || h->magic[3] != '3')
        return 0;
    u32 crc = h->header_crc;
    h->header_crc = 0;
    if (crc32_finish(crc32_bytes(0xFFFFFFFFu, (u8 *)h, sizeof(*h))) != crc)
        return 0;
    if (h->count > 16)
        return 0;
    for (u32 i = 0; i < h->count; i++) {
        u32 sec = (h->entries[i].size + 511u) / 512u;
        if (h->entries[i].size > RAMFILE_MAX_PAGES * 0x1000u)
            return 0;
        if (h->entries[i].start_lba < data_lba ||
            h->entries[i].start_lba + sec > data_lba + SAVE3_SECTORS)
            return 0;
    }
    return 1;
}

static int save3_load(struct savefs_header *h) {
    int made[16], n = 0;
    for (u32 i = 0; i < h->count; i++) {
        int idx = ramfile_create(h->entries[i].name);
        if (idx < 0)
            goto fail;
        made[n++] = idx;
        struct ramfile *r = &ramfiles[idx];
        u32 done = 0, crc = 0xFFFFFFFFu;
        u32 sec = (h->entries[i].size + 511u) / 512u;
        for (u32 s = 0; s < sec; s++) {
            if (!fdc_read_sector(h->entries[i].start_lba + s, fs_sector))
                goto fail;
            u32 chunk = h->entries[i].size - done;
            if (chunk > 512) chunk = 512;
            if (ramfile_rw(r, done, fs_sector, chunk, 1) != (int)chunk)
                goto fail;
            crc = crc32_bytes(crc, fs_sector, chunk);
            done += chunk;
        }
        if (crc32_finish(crc) != h->entries[i].crc)
            goto fail;
        r->size = h->entries[i].size;
    }
    return 1;
fail:
    for (int i = 0; i < n; i++)
        ramfile_delete(made[i]);
    return 0;
}

static int savefs_sync(void) {
    if (fs_snapshot_busy)
        return 0;
    fs_snapshot_busy = 1;
    fs_sync_owner = current_task;

    struct savefs_header h;
    u8 *hp = (u8 *)&h;
    for (u32 i = 0; i < sizeof(h); i++)
        hp[i] = 0;
    h.magic[0] = 'M'; h.magic[1] = 'S'; h.magic[2] = 'V'; h.magic[3] = '3';

    u32 total = 0, count = 0;
    for (int i = 0; i < MAX_RAMFILES; i++) {
        if (!ramfiles[i].used)
            continue;
        if (count >= 16 || ramfiles[i].size > RAMFILE_MAX_PAGES * 0x1000u)
            goto fail;
        total += (ramfiles[i].size + 511u) / 512u;
        count++;
    }
    if (total > SAVE3_SECTORS)
        goto fail;

    int target = (savefs_active == 0) ? 1 : 0;
    u32 hdr_lba = target ? SAVE3_HEADER_B : SAVE3_HEADER_A;
    u32 data_lba = target ? SAVE3_DATA_B : SAVE3_DATA_A;
    u32 lba = data_lba;
    count = 0;
    for (int i = 0; i < MAX_RAMFILES; i++) {
        if (!ramfiles[i].used)
            continue;
        struct ramfile *r = &ramfiles[i];
        u32 sec = (r->size + 511u) / 512u;
        if (lba + sec > data_lba + SAVE3_SECTORS)
            goto fail;
        u32 crc = 0xFFFFFFFFu;
        for (u32 s = 0; s < sec; s++) {
            for (int j = 0; j < 512; j++)
                fs_sector[j] = 0;
            u32 chunk = r->size - s * 512u;
            if (chunk > 512) chunk = 512;
            if (ramfile_rw(r, s * 512u, fs_sector, chunk, 0) != (int)chunk)
                goto fail;
            crc = crc32_bytes(crc, fs_sector, chunk);
            if (!fdc_write_sector(lba + s, fs_sector))
                goto fail;
        }
        for (int j = 0; j < 16; j++)
            h.entries[count].name[j] = r->name[j];
        h.entries[count].size = r->size;
        h.entries[count].start_lba = lba;
        h.entries[count].crc = crc32_finish(crc);
        count++;
        lba += sec;
    }
    h.count = count;
    h.generation = ++savefs_generation;
    h.header_crc = crc32_finish(crc32_bytes(0xFFFFFFFFu, hp, sizeof(h)));
    for (int j = 0; j < 512; j++)
        fs_sector[j] = 0;
    for (u32 j = 0; j < sizeof(h); j++)
        fs_sector[j] = hp[j];
    if (!fdc_write_sector(hdr_lba, fs_sector))
        goto fail;
    savefs_active = target;
    fs_snapshot_busy = 0;
    fs_sync_owner = -1;
    return 1;

fail:
    fs_snapshot_busy = 0;
    fs_sync_owner = -1;
    return 0;
}

static int savefs_restore(void) {
    struct savefs_header a, b;
    int va = save3_read_header(SAVE3_HEADER_A, &a) && save3_check(&a, SAVE3_DATA_A);
    int vb = save3_read_header(SAVE3_HEADER_B, &b) && save3_check(&b, SAVE3_DATA_B);
    if (va || vb) {
        struct savefs_header *pick = 0;
        if (va && (!vb || a.generation >= b.generation))
            pick = &a;
        else
            pick = &b;
        u32 gen = pick->generation;
        if (save3_load(pick)) {
            savefs_generation = gen;
            savefs_active = (pick == &a) ? 0 : 1;
            return 1;
        }
        if (pick == &a && vb && save3_load(&b)) {
            savefs_generation = b.generation;
            savefs_active = 1;
            return 1;
        }
        if (pick == &b && va && save3_load(&a)) {
            savefs_generation = a.generation;
            savefs_active = 0;
            return 1;
        }
        return 0;
    }

    /* Legacy single-slot MSV1/MSV2 import. */
    struct savefs_header h;
    if (!save3_read_header(SAVE_HEADER_LBA, &h))
        return 0;
    if (h.magic[0] != 'M' || h.magic[1] != 'S' ||
        h.magic[2] != 'V' || (h.magic[3] != '1' && h.magic[3] != '2'))
        return 0;
    u32 stored = h.header_crc;
    h.header_crc = 0;
    u32 hdr_len = (h.magic[3] == '1')
        ? (u32)(16 + 8 * sizeof(struct savefs_entry))
        : (u32)sizeof(h);
    if (crc32_finish(crc32_bytes(0xFFFFFFFFu, (u8 *)&h, hdr_len)) != stored)
        return 0;
    u32 max_entries = h.magic[3] == '1' ? 8u : 16u;
    if (h.count > max_entries)
        return 0;
    for (u32 i = 0; i < h.count; i++) {
        if (h.entries[i].size > RAMFILE_MAX_PAGES * 0x1000u)
            return 0;
        u32 sec = (h.entries[i].size + 511u) / 512u;
        if (h.entries[i].start_lba < SAVE_DATA_LBA ||
            h.entries[i].start_lba + sec > SAVE_DATA_LBA + SAVE_DATA_SECTORS)
            return 0;
    }
    savefs_generation = h.generation;
    savefs_active = -1;
    return save3_load(&h);
}

void sync_savefs(void) {
    if (savefs_sync())
        gui_sys_print("[savefs] snapshot written.\n");
    else
        gui_sys_print("[savefs] snapshot FAILED.\n");
}

int kernel_pipe_reset(int id) {
    return pipe_reset(id);
}

int kernel_pipe_ls(int id) {
    int any = 0;
    for (int i = 0; i < MAX_RAMFILES; i++) {
        if (!ramfiles[i].used)
            continue;
        for (int j = 0; j < 16 && ramfiles[i].name[j]; j++)
            pipe_write(id, (const u8 *)&ramfiles[i].name[j], 1);
        pipe_write(id, (const u8 *)"\n", 1);
        any = 1;
    }
    char names[FS_MAX_ENTRIES][17];
    int count = 0;
    if (kernel_fdc_lock()) {
        if (fs_load_dir()) {
            struct fs_header *h = (struct fs_header *)fs_dir;
            struct fs_entry *entries = (struct fs_entry *)(fs_dir + sizeof(*h));
            for (u32 i = 0; i < h->count && count < FS_MAX_ENTRIES; i++) {
                for (int j = 0; j < 16; j++)
                    names[count][j] = entries[i].name[j];
                names[count][16] = 0;
                count++;
            }
        }
        kernel_fdc_unlock();
    }
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < 16 && names[i][j]; j++)
            pipe_write(id, (const u8 *)&names[i][j], 1);
        pipe_write(id, (const u8 *)"\n", 1);
        any = 1;
    }
    __asm__ __volatile__("sti");
    return any;
}

void list_ram_files(void) {
    int any = 0;
    for (int i = 0; i < MAX_RAMFILES; i++) {
        if (!ramfiles[i].used)
            continue;
        if (!any) {
            gui_sys_print("\x1b[32m[ramfs] files:\x1b[0m\n");
            any = 1;
        }
        char name[17];
        for (int j = 0; j < 16; j++)
            name[j] = ramfiles[i].name[j];
        name[16] = 0;
        gui_sys_print("  \x1b[36m");
        gui_sys_print(name);
        gui_sys_print("\x1b[0m\n");
    }
    if (!any)
        gui_sys_print("\x1b[90m[ramfs] empty.\x1b[0m\n");
}

static int fd_open(const char *name) {
    if (current_task < 0 || current_task >= task_cap || !tasks[current_task].fds)
        return -1;
    __asm__ __volatile__("sti");
    if (!kernel_fdc_lock()) {
        __asm__ __volatile__("cli");
        return -1;
    }
    if (!fs_load_dir()) {
        kernel_fdc_unlock();
        __asm__ __volatile__("cli");
        return -1;
    }
    struct fs_header *h = (struct fs_header *)fs_dir;
    struct fs_entry *entries = (struct fs_entry *)(fs_dir + sizeof(*h));
    for (int i = 0; i < h->count; i++) {
        if (!fs_name_eq(&entries[i], name))
            continue;
        for (int f = 0; f < MAX_FDS; f++) {
            if (tasks[current_task].fds[f].used)
                continue;
            tasks[current_task].fds[f].used = 1;
            tasks[current_task].fds[f].kind = 0;
            tasks[current_task].fds[f].writable = 0;
            tasks[current_task].fds[f].ram_index = 0;
            tasks[current_task].fds[f].start_lba = entries[i].start_lba;
            tasks[current_task].fds[f].size = entries[i].size;
            tasks[current_task].fds[f].offset = 0;
            for (int j = 0; j < 16; j++)
                tasks[current_task].fds[f].name[j] = entries[i].name[j];
            kernel_fdc_unlock();
            __asm__ __volatile__("cli");
            return f + 1;
        }
        kernel_fdc_unlock();
        __asm__ __volatile__("cli");
        return -1;
    }
    kernel_fdc_unlock();
    __asm__ __volatile__("cli");
    return -1;
}

static int fd_read(int fd, u8 *buf, u32 len) {
    if (fd < 1 || fd > MAX_FDS || current_task < 0 || current_task >= task_cap ||
        !tasks[current_task].fds)
        return -1;
    struct fd_entry *f = &tasks[current_task].fds[fd - 1];
    if (!f->used)
        return -1;
    if (f->kind == 1) {
        if (f->ram_index >= MAX_RAMFILES || !ramfiles[f->ram_index].used)
            return -1;
        struct ramfile *r = &ramfiles[f->ram_index];
        if (f->offset >= r->size)
            return 0;
        u32 n = r->size - f->offset;
        if (n > len)
            n = len;
        int got = ramfile_rw(r, f->offset, buf, n, 0);
        if (got < 0)
            return -1;
        f->offset += (u32)got;
        n = (u32)got;
        f->size = r->size;
        return (int)n;
    }
    u32 done = 0;
    __asm__ __volatile__("sti");
    if (!kernel_fdc_lock()) {
        __asm__ __volatile__("cli");
        return -1;
    }
    while (done < len && f->offset < f->size) {
        u32 lba = f->start_lba + f->offset / 512;
        if (!fdc_read_sector(lba, fs_sector)) {
            kernel_fdc_unlock();
            __asm__ __volatile__("cli");
            return -1;
        }
        u32 in_sector = f->offset % 512;
        u32 chunk = 512 - in_sector;
        if (chunk > f->size - f->offset)
            chunk = f->size - f->offset;
        if (chunk > len - done)
            chunk = len - done;
        for (u32 i = 0; i < chunk; i++)
            buf[done + i] = fs_sector[in_sector + i];
        f->offset += chunk;
        done += chunk;
    }
    kernel_fdc_unlock();
    __asm__ __volatile__("cli");
    return (int)done;
}

#define O_CREAT 1
#define O_TRUNC 2
#define O_WRITE 4
#define O_APPEND 8

static int fd_alloc_slot(void) {
    if (current_task < 0 || current_task >= task_cap || !tasks[current_task].fds)
        return -1;
    for (int f = 0; f < MAX_FDS; f++) {
        if (!tasks[current_task].fds[f].used)
            return f;
    }
    return -1;
}

static int fd_open_ex(const char *name, u32 flags) {
    if (current_task < 0 || current_task >= task_cap || !tasks[current_task].fds)
        return -1;

    if (fs_snapshot_busy && (flags & (O_CREAT | O_TRUNC)))
        return -1;
    int ri = ramfile_find(name);
    if (ri >= 0) {
        int f = fd_alloc_slot();
        if (f < 0)
            return -1;
        struct fd_entry *e = &tasks[current_task].fds[f];
        e->used = 1;
        e->kind = 1;
        e->writable = (flags & O_WRITE) ? 1 : 0;
        e->append = (flags & O_APPEND) ? 1 : 0;
        e->ram_index = (u8)ri;
        e->start_lba = 0;
        e->size = ramfiles[ri].size;
        e->offset = 0;
        if ((flags & O_TRUNC) && e->writable) {
            ramfile_free_from(&ramfiles[ri], 0);
            ramfiles[ri].size = 0;
            e->size = 0;
        }
        for (int j = 0; j < 16; j++)
            e->name[j] = ramfiles[ri].name[j];
        return f + 1;
    }

    if (flags & O_CREAT) {
        ri = ramfile_create(name);
        if (ri < 0)
            return -1;
        int f = fd_alloc_slot();
        if (f < 0) {
            ramfile_delete(ri);
            return -1;
        }
        struct fd_entry *e = &tasks[current_task].fds[f];
        e->used = 1;
        e->kind = 1;
        e->writable = 1;
        e->append = (flags & O_APPEND) ? 1 : 0;
        e->ram_index = (u8)ri;
        e->start_lba = 0;
        e->size = 0;
        e->offset = 0;
        for (int j = 0; j < 16; j++)
            e->name[j] = ramfiles[ri].name[j];
        return f + 1;
    }

    if (flags & O_WRITE)
        return -1;
    return fd_open(name);
}

static int fd_write(int fd, const u8 *buf, u32 len) {
    if (fd < 1 || fd > MAX_FDS || current_task < 0 || current_task >= task_cap ||
        !tasks[current_task].fds)
        return -1;
    struct fd_entry *f = &tasks[current_task].fds[fd - 1];
    if (!f->used || f->kind != 1 || !f->writable)
        return -1;
    if (f->ram_index >= MAX_RAMFILES || !ramfiles[f->ram_index].used)
        return -1;
    struct ramfile *r = &ramfiles[f->ram_index];
    if (fs_snapshot_busy)
        return -1;
    if (f->append)
        f->offset = r->size;
    if (f->offset + len > RAMFILE_MAX_PAGES * 0x1000u)
        return -1;
    int wrote = ramfile_rw(r, f->offset, (u8 *)buf, len, 1);
    if (wrote <= 0)
        return -1;
    f->offset += (u32)wrote;
    len = (u32)wrote;
    if (f->offset > r->size)
        r->size = f->offset;
    f->size = r->size;
    return (int)len;
}

struct user_sysinfo {
    u32 ticks;
    u32 uptime_s;
    u32 free_frames;
    u32 total_frames;
    u32 heap_used;
    u32 tasks;
    u32 last_module_ticks;
};

struct user_canvas_event {
    u32 type;
    int x, y;
    u32 buttons;
    u32 key;
    u32 closed;
    u32 width, height;
};

struct user_stat {
    u32 size;
    u32 kind;
};

struct user_dirent {
    char name[16];
    u32 size;
    u32 kind;
};

struct user_taskinfo {
    u32 pid;
    u32 state;
    u32 pages;
    u32 console;
    char name[16];
};

static int sys_stat(const char *name, struct user_stat *out) {
    int ri = ramfile_find(name);
    if (ri >= 0) {
        out->size = ramfiles[ri].size;
        out->kind = 2;
        return 0;
    }

    __asm__ __volatile__("sti");
    int r = -1;
    if (kernel_fdc_lock()) {
        if (fs_load_dir()) {
            struct fs_header *h = (struct fs_header *)fs_dir;
            struct fs_entry *entries = (struct fs_entry *)(fs_dir + sizeof(*h));
            for (int i = 0; i < h->count; i++) {
                if (fs_name_eq(&entries[i], name)) {
                    out->size = entries[i].size;
                    out->kind = 1;
                    r = 0;
                    break;
                }
            }
        }
        kernel_fdc_unlock();
    }
    __asm__ __volatile__("cli");
    return r;
}

static int sys_readdir(u32 index, struct user_dirent *out) {
    u32 pos = 0;
    for (int i = 0; i < MAX_RAMFILES; i++) {
        if (!ramfiles[i].used)
            continue;
        if (pos == index) {
            for (int j = 0; j < 16; j++)
                out->name[j] = ramfiles[i].name[j];
            out->size = ramfiles[i].size;
            out->kind = 2;
            return 1;
        }
        pos++;
    }

    __asm__ __volatile__("sti");
    int found = 0;
    if (kernel_fdc_lock()) {
        if (fs_load_dir()) {
            struct fs_header *h = (struct fs_header *)fs_dir;
            struct fs_entry *entries = (struct fs_entry *)(fs_dir + sizeof(*h));
            for (int i = 0; i < h->count; i++) {
                if (ramfile_find(entries[i].name) >= 0)
                    continue;
                if (pos == index) {
                    for (int j = 0; j < 16; j++)
                        out->name[j] = entries[i].name[j];
                    out->size = entries[i].size;
                    out->kind = 1;
                    found = 1;
                    break;
                }
                pos++;
            }
        }
        kernel_fdc_unlock();
    }
    __asm__ __volatile__("cli");
    return found;
}

static int fd_unlink(const char *name);

int kernel_unlink_gui(const char *name) {
    return fd_unlink(name);
}

int kernel_readdir_gui(u32 index, char *name, u32 *size, u32 *kind) {
    struct user_dirent d;
    int ok = sys_readdir(index, &d);
    __asm__ __volatile__("sti"); /* sys_readdir leaves IF=0 for syscall context */
    if (!ok)
        return 0;
    for (int j = 0; j < 16; j++)
        name[j] = d.name[j];
    *size = d.size;
    *kind = d.kind;
    return 1;
}

static void pc_beep(u32 freq, u32 ms) {
    if (freq < 20)
        return;
    u32 div = 1193180u / freq;
    outb(0x43, 0xB6);
    outb(0x42, (u8)(div & 0xFF));
    outb(0x42, (u8)((div >> 8) & 0xFF));
    outb(0x61, (u8)(inb(0x61) | 3));
    u32 until = ticks + ms / 10 + 1;
    __asm__ __volatile__("sti");
    while ((int)(ticks - until) < 0)
        __asm__ __volatile__("hlt");
    __asm__ __volatile__("cli");
    outb(0x61, (u8)(inb(0x61) & ~3));
}

static inline void brk_invlpg(u32 va) {
    __asm__ __volatile__("invlpg (%0)" : : "r"(va) : "memory");
}

static u32 task_brk(u32 addr) {
    if (current_task < 0 || current_task >= task_cap)
        return 0xFFFFFFFFu;
    u32 old_pages = tasks[current_task].heap_pages;
    u32 cur = HEAP_VIRT_BASE + old_pages * 0x1000u;
    if (addr == 0)
        return cur;
    if (addr < HEAP_VIRT_BASE || addr > HEAP_VIRT_MAX)
        return 0xFFFFFFFFu;
    u32 pages = (addr - HEAP_VIRT_BASE + 0xFFFu) / 0x1000u;

    if (pages < old_pages) {
        for (u32 i = pages; i < old_pages; i++) {
            u32 va = HEAP_VIRT_BASE + i * 0x1000u;
            u32 phys = user_unmap_page(current_task, va);
            if (phys)
                frame_free_pages(phys, 1);
            user_maybe_free_table(current_task, va);
        }
        tasks[current_task].heap_pages = pages;
        tasks[current_task].heap_phys = 0;
        return HEAP_VIRT_BASE + pages * 0x1000u;
    }

    for (u32 i = old_pages; i < pages; i++) {
        u32 phys = frame_alloc_pages(1);
        if (!phys || !user_map_page(current_task, HEAP_VIRT_BASE + i * 0x1000u,
                                    phys, 0x007)) {
            if (phys)
                frame_free_pages(phys, 1);
            for (u32 j = old_pages; j < i; j++) {
                u32 va = HEAP_VIRT_BASE + j * 0x1000u;
                u32 p = user_unmap_page(current_task, va);
                if (p)
                    frame_free_pages(p, 1);
                user_maybe_free_table(current_task, va);
            }
            tasks[current_task].heap_pages = old_pages;
            return 0xFFFFFFFFu;
        }
        frame_zero(phys, 1);
    }
    tasks[current_task].heap_pages = pages;
    tasks[current_task].heap_phys = 0;
    return HEAP_VIRT_BASE + pages * 0x1000u;
}

static int fd_unlink(const char *name) {
    if (fs_snapshot_busy)
        return -1;
    int ri = ramfile_find(name);
    if (ri < 0)
        return -1;
    ramfile_delete(ri);
    return 0;
}

static int fd_seek(int fd, int offset, int whence) {
    if (fd < 1 || fd > MAX_FDS || current_task < 0 || current_task >= task_cap ||
        !tasks[current_task].fds)
        return -1;
    struct fd_entry *f = &tasks[current_task].fds[fd - 1];
    if (!f->used)
        return -1;
    int base = 0;
    if (whence == 0) base = 0;
    else if (whence == 1) base = (int)f->offset;
    else if (whence == 2) {
        if (f->kind == 1 && f->ram_index < MAX_RAMFILES &&
            ramfiles[f->ram_index].used) {
            f->size = ramfiles[f->ram_index].size;
            base = (int)f->size;
        } else {
            base = (int)f->size;
        }
    } else return -1;
    int next = base + offset;
    if (next < 0 || (u32)next > f->size)
        return -1;
    f->offset = (u32)next;
    return next;
}

static int fd_close(int fd) {
    if (fd < 1 || fd > MAX_FDS || current_task < 0 || current_task >= task_cap ||
        !tasks[current_task].fds)
        return -1;
    tasks[current_task].fds[fd - 1].used = 0;
    return 0;
}

#define USER_BASE 0x400000u

static int user_page_present(u32 addr) {
    if (addr < USER_BASE || addr >= USER_END)
        return 0;
    if (!scheduler_active) {
        if (addr >= 0x800000u)
            return 0;
        u32 idx = (addr - USER_BASE) >> 12;
        return (page_table1[idx] & 5) == 5;
    }
    if (current_task < 0 || current_task >= task_cap)
        return 0;
    return (user_pte_get(current_task, addr) & 5) == 5;
}

static int user_page_writable(u32 addr) {
    if (addr < USER_BASE || addr >= USER_END)
        return 0;
    if (!scheduler_active) {
        if (addr >= 0x800000u)
            return 0;
        u32 idx = (addr - USER_BASE) >> 12;
        return (page_table1[idx] & 7) == 7;
    }
    if (current_task < 0 || current_task >= task_cap)
        return 0;
    return (user_pte_get(current_task, addr) & 7) == 7;
}

static int user_ptr_ok(u32 ptr, u32 size) {
    if (ptr < USER_BASE || ptr >= USER_END || size > USER_END - ptr)
        return 0;
    if (size == 0)
        return 1;
    u32 last = ptr + size - 1;
    for (u32 p = ptr & ~0xFFFu; p <= last; p += 0x1000)
        if (!user_page_present(p))
            return 0;
    return 1;
}

static int user_ptr_writable(u32 ptr, u32 size) {
    if (ptr < USER_BASE || ptr >= USER_END || size > USER_END - ptr)
        return 0;
    if (size == 0)
        return 1;
    u32 last = ptr + size - 1;
    for (u32 p = ptr & ~0xFFFu; p <= last; p += 0x1000)
        if (!user_page_writable(p))
            return 0;
    return 1;
}

static int user_str_ok(u32 ptr, u32 max) {
    if (ptr < USER_BASE || ptr >= USER_END || !user_page_present(ptr))
        return 0;
    for (u32 i = 0; i < max && ptr + i < USER_END; i++) {
        u32 a = ptr + i;
        if ((a & 0xFFF) == 0 && !user_page_present(a))
            return 0;
        if (*(const char *)a == 0)
            return 1;
    }
    return 0;
}

void syscall_handler(u32 number, u32 arg, u32 cs, u32 frame) {
    static u8 cpl_reported;
    if (!cpl_reported) {
        cpl_reported = 1;
        if ((cs & 3) == 3)
            gui_sys_print("[kernel] syscall from CPL=3 (CS=0x1B)\n");
        else
            gui_sys_print("[kernel] syscall from non-user CS!\n");
    }

    if (number == 1) {
        int cid = current_console_id();
        if (user_str_ok(arg, 4096))
            gui_sys_print_console(cid, (const char *)arg);
    } else if (number == 2) {
        if (scheduler_active) {
            gui_sys_print("[task] exit; scheduler.\n");
            task_exit(0);
        }
        gui_sys_print("[ring3] exit syscall; returning to GUI.\n");
        user_exit_to_kernel();
    } else if (number == 3) {
        if (scheduler_active)
            task_yield(frame);
        else
            gui_sys_print("[ring3] yield ignored.\n");
    } else if (number == 4) {
        u32 ptr = SYS_ARG1(frame);
        u32 len = SYS_ARG2(frame);
        int ok = user_ptr_ok(ptr, len) && len <= BIG_HEAP_SIZE;
        if (ok)
            gui_sys_write_console(current_console_id(), (const char *)ptr, len);
        SYS_RET(frame) = ok ? len : 0xFFFFFFFFu;
    } else if (number == 5) {
        SYS_RET(frame) = (u32)module_getchar();
    } else if (number == 6) {
        SYS_RET(frame) = ticks * 10;
    } else if (number == 7) {
        u32 name = SYS_ARG1(frame);
        u32 dst = SYS_ARG2(frame);
        u32 max = SYS_ARG3(frame);
        u32 size = 0;
        if (user_str_ok(name, 256) && max <= BIG_HEAP_SIZE &&
            user_ptr_writable(dst, max)) {
            __asm__ __volatile__("sti");
            int ok = fs_load_file_to((const char *)name, (u8 *)dst, max, &size);
            __asm__ __volatile__("cli");
            SYS_RET(frame) = ok ? size : 0;
        } else {
            SYS_RET(frame) = 0;
        }
    } else if (number == 8) {
        u32 name = SYS_ARG1(frame);
        int pid = 0;
        if (user_str_ok(name, 256))
            pid = task_spawn_user((const char *)name);
        SYS_RET(frame) = (u32)pid;
    } else if (number == 9) {
        int r = task_wait_block(SYS_ARG1(frame), SYS_ARG2(frame) != 0, frame);
        if (r < 0)
            SYS_RET(frame) = 0xFFFFFFFFu;
    } else if (number == 10) {
        task_sleep_block(SYS_ARG1(frame), frame);
    } else if (number == 11) {
        if (scheduler_active)
            task_exit(SYS_ARG1(frame));
        user_exit_to_kernel();
    } else if (number == 12) {
        u32 name = SYS_ARG1(frame);
        int fd = -1;
        if (user_str_ok(name, 256))
            fd = fd_open((const char *)name);
        SYS_RET(frame) = (u32)fd;
    } else if (number == 13) {
        u32 fd = SYS_ARG1(frame);
        u32 buf = SYS_ARG2(frame);
        u32 len = SYS_ARG3(frame);
        int r = -1;
        if (len <= 0x10000 && user_ptr_writable(buf, len))
            r = fd_read((int)fd, (u8 *)buf, len);
        SYS_RET(frame) = (u32)r;
    } else if (number == 14) {
        SYS_RET(frame) = (u32)fd_close((int)SYS_ARG1(frame));
    } else if (number == 15) {
        SYS_RET(frame) = (u32)fd_seek((int)SYS_ARG1(frame),
                                        (int)SYS_ARG2(frame),
                                        (int)SYS_ARG3(frame));
    } else if (number == 16) {
        u32 name = SYS_ARG1(frame);
        u32 flags = SYS_ARG2(frame);
        int fd = -1;
        if (user_str_ok(name, 256))
            fd = fd_open_ex((const char *)name, flags);
        SYS_RET(frame) = (u32)fd;
    } else if (number == 17) {
        u32 fd = SYS_ARG1(frame);
        u32 buf = SYS_ARG2(frame);
        u32 len = SYS_ARG3(frame);
        int r = -1;
        if (len <= 0x10000 && user_ptr_ok(buf, len))
            r = fd_write((int)fd, (const u8 *)buf, len);
        SYS_RET(frame) = (u32)r;
    } else if (number == 18) {
        u32 name = SYS_ARG1(frame);
        int r = -1;
        if (user_str_ok(name, 256))
            r = fd_unlink((const char *)name);
        SYS_RET(frame) = (u32)r;
    } else if (number == 19) {
        u32 name = SYS_ARG1(frame);
        u32 argv = SYS_ARG2(frame);
        u32 argc = SYS_ARG3(frame);
        int pid = 0;
        if (user_str_ok(name, 256) && argc <= 32 &&
            user_ptr_ok(argv, argc * 4))
            pid = task_spawn_args((const char *)name, argv, argc);
        SYS_RET(frame) = (u32)pid;
    } else if (number == 20) {
        SYS_RET(frame) = task_brk(SYS_ARG1(frame));
    } else if (number == 21) {
        u32 name = SYS_ARG1(frame);
        u32 out = SYS_ARG2(frame);
        int r = -1;
        if (user_str_ok(name, 256) &&
            user_ptr_writable(out, sizeof(struct user_stat)))
            r = sys_stat((const char *)name, (struct user_stat *)out);
        SYS_RET(frame) = (u32)r;
    } else if (number == 22) {
        u32 index = SYS_ARG1(frame);
        u32 out = SYS_ARG2(frame);
        int r = 0;
        if (user_ptr_writable(out, sizeof(struct user_dirent)))
            r = sys_readdir(index, (struct user_dirent *)out);
        SYS_RET(frame) = (u32)r;
    } else if (number == 23) {
        __asm__ __volatile__("sti");
        int r = savefs_sync();
        __asm__ __volatile__("cli");
        SYS_RET(frame) = r ? 0 : 0xFFFFFFFFu;
    } else if (number == 24) {
        u32 out = SYS_ARG1(frame);
        if (user_ptr_writable(out, sizeof(struct user_sysinfo))) {
            struct user_sysinfo *si = (struct user_sysinfo *)out;
            u32 free_frames = kernel_free_frames();
            u32 ntasks = 0;
            for (int i = 0; i < task_cap; i++)
                if (tasks[i].state != TASK_FREE && tasks[i].state != TASK_DONE)
                    ntasks++;
            si->ticks = ticks;
            si->uptime_s = ticks / 100;
            si->free_frames = free_frames;
            si->total_frames = kernel_total_frames();
            si->heap_used = heap_used;
            si->tasks = ntasks;
            si->last_module_ticks = user_module_last_ticks;
            SYS_RET(frame) = 0;
        } else {
            SYS_RET(frame) = 0xFFFFFFFFu;
        }
    } else if (number == 25) {
        u32 op = SYS_ARG1(frame);
        u32 title = SYS_ARG2(frame);
        if (op == 0) {
            gui_canvas_close();
            SYS_RET(frame) = 0;
        } else if (op == 1) {
            if (user_str_ok(title, 64)) {
                gui_canvas_open((const char *)title);
                SYS_RET(frame) = 0;
            } else {
                SYS_RET(frame) = 0xFFFFFFFFu;
            }
        } else if (op == 2) {
            SYS_RET(frame) = ((u32)gui_canvas_w() << 16) | (u32)gui_canvas_h();
        } else if (op == 3) {
            SYS_RET(frame) = ((u32)scr_w << 16) | (u32)scr_h;
        } else if (op == 4) {
            SYS_RET(frame) = vbe_phys_base;
        } else if (op == 5) {
            SYS_RET(frame) = (user_module_cold_ticks << 16) |
                                (user_module_warm_ticks & 0xFFFFu);
        } else if (op == 6) {
            gui_canvas_batch((int)title);
            SYS_RET(frame) = 0;
        } else if (op == 7) {
            gui_canvas_present();
            SYS_RET(frame) = 0;
        } else if (op == 8) {
            gui_canvas_capture((int)title);
            SYS_RET(frame) = 0;
        } else {
            SYS_RET(frame) = 0xFFFFFFFFu;
        }
    } else if (number == 26) {
        u32 ptr = SYS_ARG1(frame);
        if (user_ptr_ok(ptr, sizeof(struct gui_canvas_cmd))) {
            struct gui_canvas_cmd cmd;
            u8 *dst = (u8 *)&cmd;
            const u8 *src = (const u8 *)ptr;
            for (u32 i = 0; i < sizeof(cmd); i++)
                dst[i] = src[i];
            int r = -1;
            if (cmd.text && user_ptr_ok((u32)cmd.text, cmd.len))
                r = gui_canvas_cmd(&cmd);
            else if (!cmd.text && cmd.op != 4)
                r = gui_canvas_cmd(&cmd);
            SYS_RET(frame) = (u32)r;
        } else {
            SYS_RET(frame) = 0xFFFFFFFFu;
        }
    } else if (number == 27) {
        u32 out = SYS_ARG1(frame);
        int r = 0;
        if (user_ptr_writable(out, sizeof(struct user_canvas_event))) {
            struct gui_canvas_event ev;
            if (gui_canvas_event(&ev)) {
                struct user_canvas_event *ue = (struct user_canvas_event *)out;
                ue->type = ev.type;
                ue->x = ev.x;
                ue->y = ev.y;
                ue->buttons = ev.buttons;
                ue->key = ev.key;
                ue->closed = ev.closed;
                ue->width = ev.width;
                ue->height = ev.height;
                r = 1;
            }
        }
        SYS_RET(frame) = (u32)r;
    } else if (number == 28) {
        u32 op = SYS_ARG1(frame);
        u32 a = SYS_ARG2(frame);
        u32 b = SYS_ARG3(frame);
        if (op == 0) {
            if (a < 20000 && b < 5000)
                pc_beep(a, b);
            SYS_RET(frame) = 0;
        } else if (op == 1) {
            if (b <= 65536 && user_ptr_ok(a, b)) {
                for (u32 i = 0; i < b; i++)
                    serial_putc((char)((const u8 *)a)[i]);
                SYS_RET(frame) = b;
            } else {
                SYS_RET(frame) = 0;
            }
        } else if (op == 2) {
            if (b <= 65536 && user_ptr_writable(a, b)) {
                u32 n = 0;
                while (n < b && (inb(COM1 + 5) & 1))
                    ((u8 *)a)[n++] = inb(COM1);
                SYS_RET(frame) = n;
            } else {
                SYS_RET(frame) = 0;
            }
        } else {
            SYS_RET(frame) = 0;
        }
    } else if (number == 29) {
        int found = 0;
        if (current_task > 0 && current_task < task_cap &&
            (tasks[current_task].sig_pending & SIGINT)) {
            tasks[current_task].sig_pending &= (u8)~SIGINT;
            found = 1;
        }
        int cid = current_console_id();
        if (cid < 0 || cid >= kbd_console_count)
            cid = 0;
        u8 t = kbd_tail[cid];
        while (t != kbd_head[cid]) {
            if (kbd_buf[cid][t] == 3) {
                kbd_tail[cid] = (u8)((t + 1) & 63);
                found = 1;
                break;
            }
            t = (u8)((t + 1) & 63);
        }
        SYS_RET(frame) = (u32)found;
    } else if (number == 30) {
        u32 name = SYS_ARG1(frame);
        if (user_str_ok(name, 64)) {
            gui_request_py((const char *)name);
            SYS_RET(frame) = 0;
        } else {
            SYS_RET(frame) = 0xFFFFFFFFu;
        }
    } else if (number == 31) {
        int cid = current_console_id();
        if (cid < 0 || cid >= kbd_console_count)
            cid = 0;
        if (kbd_tail[cid] == kbd_head[cid]) {
            SYS_RET(frame) = 0xFFFFFFFFu;
        } else {
            u8 ch = kbd_buf[cid][kbd_tail[cid]];
            kbd_tail[cid] = (u8)((kbd_tail[cid] + 1) & 63);
            SYS_RET(frame) = ch;
        }
    } else if (number == 32) {
        u32 out = SYS_ARG1(frame);
        u32 max = SYS_ARG2(frame);
        if (max > 16)
            max = 16;
        if (user_ptr_writable(out, max * sizeof(struct user_taskinfo))) {
            int n = 0;
            for (u32 i = 0; i < max; i++) {
                u32 pid, state, pages;
                int con;
                char name[16];
                if (!kernel_task_snapshot((int)i, &pid, &con, &state, &pages, name))
                    break;
                struct user_taskinfo *ti =
                    (struct user_taskinfo *)(out + i * sizeof(*ti));
                ti->pid = pid;
                ti->state = state;
                ti->pages = pages;
                ti->console = (u32)con;
                for (int j = 0; j < 16; j++)
                    ti->name[j] = name[j];
                n++;
            }
            SYS_RET(frame) = (u32)n;
        } else {
            SYS_RET(frame) = 0;
        }
    } else if (number == 36) {
        __asm__ __volatile__("sti");
        int locked = kernel_fdc_lock();
        int waited = 0;
        if (locked) {
            waited = kernel_fdc_wait((int)SYS_ARG1(frame));
            kernel_fdc_unlock();
        }
        __asm__ __volatile__("cli");
        SYS_RET(frame) = (u32)(locked ? waited : 0);
    } else if (number == 35) {
        __asm__ __volatile__("sti");
        int r = kernel_fdc_wait((int)SYS_ARG1(frame));
        __asm__ __volatile__("cli");
        SYS_RET(frame) = (u32)r;
    } else if (number == 34) {
        u32 op = SYS_ARG1(frame);
        u32 mask = SYS_ARG2(frame);
        if (current_task > 0 && current_task < task_cap) {
            if (op == 0) {
                tasks[current_task].sig_ignored |= mask;
                SYS_RET(frame) = 0;
            } else if (op == 1) {
                u32 p = tasks[current_task].sig_pending &
                        ~tasks[current_task].sig_ignored;
                tasks[current_task].sig_pending &= (u8)~p;
                SYS_RET(frame) = p;
            } else {
                SYS_RET(frame) = 0;
            }
        } else {
            SYS_RET(frame) = 0;
        }
    } else if (number == 33) {
        u32 a1 = SYS_ARG1(frame);
        u32 a2 = SYS_ARG2(frame);
        u32 a3 = SYS_ARG3(frame);
        int id = (int)(a1 >> 8);
        int op = (int)(a1 & 0xFF);
        int r = -1;
        if (op == 0) {
            r = pipe_reset(id);
        } else if (op == 1) {
            if (a3 <= 4096 && user_ptr_ok(a2, a3)) {
                if (current_task > 0 && current_task < task_cap &&
                    id >= 0 && id < NPIPES)
                    tasks[current_task].pipe_w_id = (u8)(id + 1);
                r = pipe_block_op(id, 1, (u8 *)a2, a3);
            }
        } else if (op == 2) {
            if (a3 <= 4096 && user_ptr_writable(a2, a3)) {
                if (current_task > 0 && current_task < task_cap &&
                    id >= 0 && id < NPIPES)
                    tasks[current_task].pipe_r_id = (u8)(id + 1);
                r = pipe_block_op(id, 0, (u8 *)a2, a3);
            }
        } else if (op == 3) {
            if (id >= 0 && id < NPIPES) {
                if (current_task > 0 && current_task < task_cap &&
                    tasks[current_task].pipe_w_id == (u8)(id + 1))
                    tasks[current_task].pipe_w_id = 0;
                pipe_close_write(id);
            }
            r = 0;
        } else if (op == 4) {
            if (id >= 0 && id < NPIPES) {
                if (current_task > 0 && current_task < task_cap &&
                    tasks[current_task].pipe_r_id == (u8)(id + 1))
                    tasks[current_task].pipe_r_id = 0;
                pipe_close_read(id);
            }
            r = 0;
        }
        SYS_RET(frame) = (u32)r;
    } else {
        gui_sys_print("[ring3] unknown syscall.\n");
    }
}

void disk_test(void) {
    u8 sector[512];
    if (!fdc_read_sector(0, sector)) {
        gui_sys_print("[fdc] read failed.\n");
        return;
    }
    if (sector[510] == 0x55 && sector[511] == 0xAA)
        gui_sys_print("[fdc] boot sector OK.\n");
    else
        gui_sys_print("[fdc] bad boot signature.\n");
}

void enter_ring3_demo(void) {
    __asm__ __volatile__("cli");
    gdt_install();
    idt_set_flags(0x80, (u32)syscall_stub, 0xEE);   /* DPL3 interrupt gate */
    paging_install();
    runtime_busy = 1;
    run_user(0x400000, 0x402000);
    runtime_busy = 0;
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
void bsod_entry(void);

static void kernel_protections_on(void) {
    page_table0[0] = 0;
    sched_safe_pt[0] = 0;
    task_kernel_pt[0] = 0;
    set_cr3((u32)sched_safe_pd);
    u32 cr0;
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x00010000u;
    __asm__ __volatile__("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

static void df_tss_init(void) {
    u8 *p = (u8 *)&df_tss;
    for (unsigned i = 0; i < sizeof(df_tss); i++)
        p[i] = 0;
    df_tss.cr3 = (u32)sched_safe_pd;
    df_tss.eip = (u32)bsod_entry;
    df_tss.eflags = 0x2;
    df_tss.esp = (u32)df_stack + sizeof(df_stack);
    df_tss.esp0 = df_tss.esp;
    df_tss.ss0 = 0x10;
    df_tss.cs = 0x08;
    df_tss.ss = 0x10;
    df_tss.ds = 0x10;
    df_tss.es = 0x10;
    df_tss.fs = 0x10;
    df_tss.gs = 0x10;
    df_tss.iomap_base = (u16)sizeof(df_tss);
}

void kmain(void) {
    gdt_install();
    cpu_vendor();
    serial_init();
    task_system_init();
    kernel_kbd_resize(4);
    vbe_init();
    gui_alloc_fb();
    gui_init();
    df_tss_init();
    interrupts_setup();
    idt_set_flags(0x80, (u32)syscall_stub, 0xEE);
    mouse_init();

    interrupts_on();
    sched_init_task0();
    kernel_protections_on();
    savefs_restore();
    int autoexec_pending = ramfile_find("AUTOEXEC.PY") >= 0;
    u32 autoexec_at = ticks + 300;
    gui_redraw();

    u32 last_blink = ticks / 50;

    for (;;) {
        int event = 0;
        task_reap_done();
        if (autoexec_pending && (int)(ticks - autoexec_at) >= 0) {
            autoexec_pending = 0;
            if (ctrl_seen || ctrl_down)
                gui_sys_print("[boot] AUTOEXEC.PY skipped (Safe Mode)\n");
            else
                gui_request_py("AUTOEXEC.PY");
        }
        gui_run_pending_py();

        if (gui_special_pending) {
            u8 special = gui_special_pending;
            gui_special_pending = 0;
            gui_special(special);
            event = 1;
        }

        int cid = gui_focused_console();
        if (cid < 0 || cid >= kbd_console_count)
            cid = 0;
        if (gui_runtime_keyboard_wanted() || !console_has_live_task(cid)) {
            while (kbd_tail[cid] != kbd_head[cid]) {
                u8 ch = kbd_buf[cid][kbd_tail[cid]];
                kbd_tail[cid] = (u8)((kbd_tail[cid] + 1) & 63);
                gui_key((char)ch);
                event = 1;
            }
        }

        if (mouse_event) {
            mouse_event = 0;
            gui_mouse_event(mouse_x, mouse_y, mouse_buttons);
            event = 1;
        }

        u32 blink = ticks / 50;
        int periodic = blink != last_blink;
        if (periodic)
            last_blink = blink;
        if (event || periodic || gui_dirty_pending())
            gui_redraw();
        if (next_ready() >= 0)
            sched_yield();

        __asm__ __volatile__("hlt");
    }
}



















































