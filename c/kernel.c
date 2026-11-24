/* =============================================================================
 *  MiniOS 2.0 - 32-bit protected-mode C kernel (graphical shell)
 *  Assembly: boot.asm, entry.asm, interrupts.asm.
 * ============================================================================= */

#include "gui.h"
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
static volatile u8 scheduler_active;
u32 heap_used;

static u8 heap[64 * 1024];

#define KBD_CONSOLES 3
static volatile u8 kbd_buf[KBD_CONSOLES][64];
static volatile u8 kbd_head[KBD_CONSOLES], kbd_tail[KBD_CONSOLES];
static volatile u8 gui_special_pending;
static int current_console_id(void);
static int console_has_live_task(int cid);
static u8 shift_down, ctrl_down, extended;

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

static void serial_puts(const char *s) {
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

static void kbd_push(u8 ch) {
    int c = gui_focused_console();
    if (c < 0 || c >= KBD_CONSOLES)
        c = 0;
    u8 next = (u8)((kbd_head[c] + 1) & 63);
    if (next != kbd_tail[c]) {
        kbd_buf[c][kbd_head[c]] = ch;
        kbd_head[c] = next;
    }
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
        else if (make == 0x1D)
            ctrl_down = 0;
        return;
    }
    if (sc == 0x2A || sc == 0x36) {
        shift_down = 1;
        return;
    }
    if (sc == 0x1D) {
        ctrl_down = 1;
        return;
    }
    if (sc == 0x3B) { gui_special_pending = 1; return; }
    if (sc == 0x3C) { gui_special_pending = 2; return; }
    if (sc == 0x3D) { gui_special_pending = 3; return; }
    if (sc == 0x3E) { gui_special_pending = 4; return; }
    if (sc == 0x3F) { gui_special_pending = 5; return; }
    if (sc == 0x40) { gui_special_pending = 6; return; }

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
extern void exc0_stub(void);
extern void exc6_stub(void);
extern void exc13_stub(void);
extern void exc14_stub(void);
extern void run_user(u32 entry, u32 user_stack);
extern void user_exit_to_kernel(void);
extern void scheduler_start(u32 first_task_esp);
extern void scheduler_exit_to_kernel(void);
extern void switch_to(u32 task_esp);
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

static void sched_timer_tick(u32 *frame);

static volatile u8 runtime_busy;

static void gui_poll_runtime(void) {
    static u32 last_frame;
    int changed = 0;
    if (gui_special_pending) {
        u8 special = gui_special_pending;
        gui_special_pending = 0;
        gui_special(special);
        changed = 1;
    }
    if (mouse_event) {
        mouse_event = 0;
        if (!gui_cursor_only_motion(mouse_x, mouse_y, mouse_buttons)) {
            gui_mouse_event(mouse_x, mouse_y, mouse_buttons);
            changed = 1;
        }
    }

    int cid = gui_focused_console();
    if (cid < 0 || cid >= KBD_CONSOLES)
        cid = 0;
    int ring0_runtime = runtime_busy && !scheduler_active;
    if (!ring0_runtime &&
        (gui_runtime_keyboard_wanted() || !console_has_live_task(cid))) {
        while (kbd_tail[cid] != kbd_head[cid]) {
            u8 ch = kbd_buf[cid][kbd_tail[cid]];
            kbd_tail[cid] = (u8)((kbd_tail[cid] + 1) & 63);
            gui_key((char)ch);
            changed = 1;
        }
    }

    if (changed)
        gui_mark_dirty();
    if (gui_dirty_pending() && (u32)(ticks - last_frame) >= 2) {
        gui_redraw();
        last_frame = ticks;
    }
}

void irq0_handler(u32 *frame) {
    ticks++;
    outb(0x20, 0x20);
    if (scheduler_active || runtime_busy)
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
    idt_set(0, (u32)exc0_stub);
    idt_set(6, (u32)exc6_stub);
    idt_set(13, (u32)exc13_stub);
    idt_set(14, (u32)exc14_stub);
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

static struct gdt_entry gdt[6];
static struct gdt_ptr_t gdtp;
static struct tss_entry tss;
static u8 tss_loaded;
static u8 gdt_ready;

#define VBE_VIRT_BASE 0xE0000000u
#define VBE_PDE_INDEX (VBE_VIRT_BASE >> 22)
static u32 vbe_pde_entry;

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
    for (int i = 0; i < 6; i++) {
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
 * ----------------------------------------------------------------------------- */
static u32 page_directory[1024] __attribute__((aligned(4096)));
static u32 page_table0[1024] __attribute__((aligned(4096)));
static u32 page_table1[1024] __attribute__((aligned(4096)));
static u32 direct_pt[3][1024] __attribute__((aligned(4096)));

static void paging_install(void) {
    for (int i = 0; i < 1024; i++) {
        page_directory[i] = 0;
        page_table0[i] = 0;
        page_table1[i] = 0;
    }

    page_directory[0] = (u32)page_table0 | 0x003;   /* supervisor 0-4 MB */
    page_directory[1] = (u32)page_table1 | 0x007;   /* user-capable 4-8 MB */
    for (int i = 0; i < 3; i++)
        page_directory[4 + i] = (u32)direct_pt[i] | 0x003;
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
#define FS_LBA 200u
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
static u8 fs_program[FS_MAX_FILE];

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

static int fs_load_file(const char *name, u8 *dst, u32 max, u32 *out_size) {
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

void list_disk_files(void) {
    if (!fs_load_dir()) {
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

static int fs_load_file_to(const char *name, u8 *dst, u32 max, u32 *out_size) {
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
    if (seq_pos < seq_len)
        return (int)seq[seq_pos++];

    for (;;) {
        int cid = current_console_id();
        if (cid < 0 || cid >= KBD_CONSOLES)
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
        gui_flush();
        __asm__ __volatile__("sti; hlt");
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
#define NTASKS 8
#define TASK_FREE    0
#define TASK_READY   1
#define TASK_RUNNING 2
#define TASK_DONE    3
#define TASK_BLOCKED 4
#define WAIT_NONE    0xFFFFFFFFu

#define TIME_SLICE_TICKS 1

/* One large Ring 3 task for the MicroPython-class layout. */
#define BIG_TASK_SLOT    7
#define BIG_CODE_PHYS    0x00100000u
#define BIG_CODE_MAX     0x00080000u
#define BIG_USER_CODE    0x00400000u
#define BIG_USER_STACK   0x00480000u
#define BIG_USER_HEAP    0x00500000u
#define HEAP_VIRT_BASE   0x00500000u
#define HEAP_VIRT_MAX    0x00700000u
#define HEAP_PAGE_INDEX  ((HEAP_VIRT_BASE - BIG_USER_CODE) / 0x1000)
#define BIG_STACK_PAGES  16
#define BIG_HEAP_PAGES   256
#define BIG_STACK_SIZE   (BIG_STACK_PAGES * 0x1000u)
#define BIG_HEAP_SIZE    (BIG_HEAP_PAGES * 0x1000u)

/* Dynamic physical frames are allocated above the single-Ring3 4 MB page. */
#define FRAME_POOL_START 0x00500000u
#define FRAME_POOL_END   0x01000000u
#define FRAME_COUNT      ((FRAME_POOL_END - FRAME_POOL_START) / 0x1000)
#define DIRECT_PHYS_START 0x00400000u
#define DIRECT_BASE       0x01000000u

struct task {
    u32 esp;
    u32 cr3;
    u32 kstack_top;
    u32 code_phys;
    u32 stack_phys;
    u32 heap_phys;
    u16 code_pages;
    u16 stack_pages;
    u16 heap_pages;
    u8  state;
    char name[16];
    u32 pid;
    u32 parent_pid;
    u32 waiting_pid;
    u32 sleep_until;
    u32 exit_code;
    u8 console_id;
    u8 kill_pending;
};

static struct task tasks[NTASKS];
static int current_task;
static u32 quantum_ticks;
static u32 next_pid = 1;

static int current_console_id(void) {
    if (scheduler_active && current_task >= 0 && current_task < NTASKS)
        return tasks[current_task].console_id;
    return gui_focused_console();
}

int scheduler_active_now(void) {
    return scheduler_active;
}

int kernel_runtime_busy(void) {
    return scheduler_active || runtime_busy;
}

static int console_has_live_task(int cid) {
    for (int t = 0; t < NTASKS; t++) {
        if (tasks[t].state != TASK_FREE && tasks[t].state != TASK_DONE &&
            tasks[t].console_id == cid)
            return 1;
    }
    return 0;
}

#define MAX_FDS 8
struct fd_entry {
    u8 used;
    u8 kind;            /* 0 = MiniFS read-only, 1 = RAMFS */
    u8 writable;
    u8 ram_index;
    u32 start_lba;
    u32 size;
    u32 offset;
    char name[16];
};
static struct fd_entry fd_table[NTASKS][MAX_FDS];

#define MAX_RAMFILES 8
#define RAMFILE_PAGES 4
struct ramfile {
    u8 used;
    char name[16];
    u32 phys;
    u32 capacity;
    u32 size;
};
static struct ramfile ramfiles[MAX_RAMFILES];

static u32 task_kernel_pt[1024] __attribute__((aligned(4096)));
static u32 task_pd[NTASKS][1024] __attribute__((aligned(4096)));
static u32 task_user_pt[NTASKS][1024] __attribute__((aligned(4096)));
static u8 task_kstack[NTASKS][4096] __attribute__((aligned(16)));
static u8 big_kstack[8192] __attribute__((aligned(16)));

/* Direct-map the 4-16 MB physical window at 16-28 MB (declared above). */
static u32 frame_bitmap[(FRAME_COUNT + 31) / 32];
static u8 frames_ready;

/* Never modified while active: safe kernel mapping used during task setup. */
static u32 sched_safe_pd[1024] __attribute__((aligned(4096)));
static u32 sched_safe_pt[1024] __attribute__((aligned(4096)));
static u8 sched_safe_ready;

static void set_cr3(u32 value) {
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(value));
}

static void *direct_ptr(u32 phys) {
    return (void *)(DIRECT_BASE + (phys - DIRECT_PHYS_START));
}

static void frame_init(void) {
    if (frames_ready)
        return;
    for (int t = 0; t < 3; t++) {
        for (int i = 0; i < 1024; i++) {
            u32 phys = DIRECT_PHYS_START + (u32)t * 0x400000u + (u32)i * 0x1000u;
            direct_pt[t][i] = phys | 0x003;
        }
    }
    for (u32 i = 0; i < (FRAME_COUNT + 31) / 32; i++)
        frame_bitmap[i] = 0;
    frames_ready = 1;
}

static u32 frame_alloc_pages(u32 pages) {
    if (pages == 0 || pages > FRAME_COUNT)
        return 0;
    for (u32 start = 0; start + pages <= FRAME_COUNT; start++) {
        u32 ok = 1;
        for (u32 j = 0; j < pages; j++) {
            u32 bit = start + j;
            if (frame_bitmap[bit / 32] & (1u << (bit % 32))) {
                ok = 0;
                break;
            }
        }
        if (!ok)
            continue;
        for (u32 j = 0; j < pages; j++) {
            u32 bit = start + j;
            frame_bitmap[bit / 32] |= (1u << (bit % 32));
        }
        return FRAME_POOL_START + start * 0x1000u;
    }
    return 0;
}

static void frame_free_pages(u32 base, u32 pages) {
    if (base < FRAME_POOL_START || base >= FRAME_POOL_END || pages == 0)
        return;
    u32 start = (base - FRAME_POOL_START) / 0x1000;
    if (start + pages > FRAME_COUNT)
        pages = FRAME_COUNT - start;
    for (u32 j = 0; j < pages; j++) {
        u32 bit = start + j;
        frame_bitmap[bit / 32] &= ~(1u << (bit % 32));
    }
}

static void frame_zero(u32 phys, u32 pages) {
    u8 *p = (u8 *)direct_ptr(phys);
    for (u32 i = 0; i < pages * 0x1000u; i++)
        p[i] = 0;
}

u32 kernel_free_frames(void) {
    u32 n = 0;
    for (u32 i = 0; i < FRAME_COUNT; i++)
        if (!(frame_bitmap[i / 32] & (1u << (i % 32))))
            n++;
    return n;
}

u32 kernel_total_frames(void) {
    return FRAME_COUNT;
}

/* Used by the GUI for large optional buffers after paging/direct-map init. */
void *kernel_alloc_pages_zero(u32 pages) {
    u32 phys = frame_alloc_pages(pages);
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
        for (int i = 0; i < 3; i++)
            sched_safe_pd[4 + i] = (u32)direct_pt[i] | 0x003;
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

static void task_free_heap(int slot) {
    for (int i = 0; i < tasks[slot].heap_pages; i++) {
        u32 pte = task_user_pt[slot][HEAP_PAGE_INDEX + i];
        if (pte & 1)
            frame_free_pages(pte & 0xFFFFF000u, 1);
        task_user_pt[slot][HEAP_PAGE_INDEX + i] = 0;
    }
    tasks[slot].heap_pages = 0;
    tasks[slot].heap_phys = 0;
}

static void task_clear_all(void) {
    for (int t = 0; t < NTASKS; t++) {
        if (tasks[t].state != TASK_FREE) {
            if (tasks[t].code_phys)
                frame_free_pages(tasks[t].code_phys, tasks[t].code_pages);
            if (tasks[t].stack_phys)
                frame_free_pages(tasks[t].stack_phys, tasks[t].stack_pages);
            task_free_heap(t);
        }
        tasks[t].state = TASK_FREE;
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
        tasks[t].sleep_until = 0;
        tasks[t].exit_code = 0;
        tasks[t].console_id = 0;
        tasks[t].kill_pending = 0;
        for (int f = 0; f < MAX_FDS; f++)
            fd_table[t][f].used = 0;
    }
}

static void task_fill_pd(int slot) {
    for (int i = 0; i < 1024; i++) {
        task_pd[slot][i] = 0;
        task_user_pt[slot][i] = 0;
    }
    task_pd[slot][0] = (u32)task_kernel_pt | 0x003;
    task_pd[slot][1] = (u32)task_user_pt[slot] | 0x007;
    for (int i = 0; i < 3; i++)
        task_pd[slot][4 + i] = (u32)direct_pt[i] | 0x003;
    if (vbe_active)
        task_pd[slot][VBE_PDE_INDEX] = vbe_pde_entry;
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

            vbe_pde_entry = map_base | 0x09Bu; /* Present|RW|PWT|PCD|PS */
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

static void task_set_name(int slot, const char *name) {
    int i = 0;
    while (i < 15 && name[i]) {
        tasks[slot].name[i] = name[i];
        i++;
    }
    tasks[slot].name[i] = 0;
}

static int task_create(const char *name, const u8 *src, const u8 *end) {
    int slot = -1;
    for (int t = 0; t < BIG_TASK_SLOT; t++) {
        if (tasks[t].state == TASK_FREE || tasks[t].state == TASK_DONE) {
            slot = t;
            break;
        }
    }
    if (slot < 0)
        return -1;

    u32 len = (u32)(end - src);
    if (len == 0 || len > 4096)
        return -1;
    u32 code = frame_alloc_pages(1);
    u32 stack = frame_alloc_pages(1);
    if (!code || !stack) {
        if (code) frame_free_pages(code, 1);
        if (stack) frame_free_pages(stack, 1);
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

    task_fill_pd(slot);
    task_user_pt[slot][0] = code | 0x007;
    task_user_pt[slot][1] = stack | 0x007;

    u32 top = (u32)(task_kstack[slot] + 4096);
    u32 *frame = (u32 *)(top - 52);
    for (int i = 0; i < 13; i++)
        frame[i] = 0;
    frame[8] = 0x00400000;
    frame[9] = 0x1B;
    frame[10] = 0x202;
    frame[11] = 0x00402000;
    frame[12] = 0x23;

    tasks[slot].esp = (u32)frame;
    tasks[slot].kstack_top = top;
    tasks[slot].cr3 = (u32)task_pd[slot];
    tasks[slot].code_phys = code;
    tasks[slot].stack_phys = stack;
    tasks[slot].heap_phys = 0;
    tasks[slot].code_pages = 1;
    tasks[slot].stack_pages = 1;
    tasks[slot].heap_pages = 0;
    tasks[slot].state = TASK_READY;
    tasks[slot].pid = next_pid++;
    tasks[slot].parent_pid = 0;
    tasks[slot].waiting_pid = WAIT_NONE;
    tasks[slot].sleep_until = 0;
    tasks[slot].exit_code = 0;
    tasks[slot].console_id = (u8)gui_focused_console();
    tasks[slot].kill_pending = 0;
    for (int f = 0; f < MAX_FDS; f++)
        fd_table[slot][f].used = 0;
    task_set_name(slot, name);
    return slot;
}

static int task_create_big(const char *name, u32 image, u32 bss, u32 entry) {
    int slot = BIG_TASK_SLOT;
    if (tasks[slot].state != TASK_FREE && tasks[slot].state != TASK_DONE)
        return -1;
    if (image > BIG_CODE_MAX || bss > BIG_CODE_MAX - image)
        return -1;

    u32 code_pages = (image + bss + 4095) / 4096;
    if (code_pages == 0 || code_pages > BIG_CODE_MAX / 4096)
        return -1;

    u32 code = frame_alloc_pages(code_pages);
    u32 stack = frame_alloc_pages(BIG_STACK_PAGES);
    u32 heap = frame_alloc_pages(BIG_HEAP_PAGES);
    if (!code || !stack || !heap) {
        if (code) frame_free_pages(code, code_pages);
        if (stack) frame_free_pages(stack, BIG_STACK_PAGES);
        if (heap) frame_free_pages(heap, BIG_HEAP_PAGES);
        return -1;
    }

    u8 *codev = (u8 *)direct_ptr(code);
    u8 *src = (u8 *)BIG_CODE_PHYS;
    for (u32 i = 0; i < code_pages * 0x1000u; i++)
        codev[i] = 0;
    for (u32 i = 0; i < image; i++)
        codev[i] = src[i];
    frame_zero(stack, BIG_STACK_PAGES);
    frame_zero(heap, BIG_HEAP_PAGES);

    task_fill_pd(slot);
    for (u32 i = 0; i < code_pages; i++)
        task_user_pt[slot][i] = (code + i * 0x1000) | 0x007;

    u32 stack_page = (BIG_USER_STACK - BIG_USER_CODE) / 0x1000;
    for (u32 i = 0; i < BIG_STACK_PAGES; i++)
        task_user_pt[slot][stack_page + i] = (stack + i * 0x1000) | 0x007;

    u32 heap_page = (BIG_USER_HEAP - BIG_USER_CODE) / 0x1000;
    for (u32 i = 0; i < BIG_HEAP_PAGES; i++)
        task_user_pt[slot][heap_page + i] = (heap + i * 0x1000) | 0x007;

    u32 top = (u32)(big_kstack + sizeof(big_kstack));
    u32 *frame = (u32 *)(top - 52);
    for (int i = 0; i < 13; i++)
        frame[i] = 0;
    frame[8] = BIG_USER_CODE + entry;
    frame[9] = 0x1B;
    frame[10] = 0x202;
    frame[11] = BIG_USER_STACK + BIG_STACK_PAGES * 0x1000;
    frame[12] = 0x23;

    tasks[slot].esp = (u32)frame;
    tasks[slot].kstack_top = top;
    tasks[slot].cr3 = (u32)task_pd[slot];
    tasks[slot].code_phys = code;
    tasks[slot].stack_phys = stack;
    tasks[slot].heap_phys = heap;
    tasks[slot].code_pages = (u16)code_pages;
    tasks[slot].stack_pages = BIG_STACK_PAGES;
    tasks[slot].heap_pages = BIG_HEAP_PAGES;
    tasks[slot].state = TASK_READY;
    tasks[slot].pid = next_pid++;
    tasks[slot].parent_pid = 0;
    tasks[slot].waiting_pid = WAIT_NONE;
    tasks[slot].sleep_until = 0;
    tasks[slot].exit_code = 0;
    tasks[slot].console_id = (u8)gui_focused_console();
    tasks[slot].kill_pending = 0;
    for (int f = 0; f < MAX_FDS; f++)
        fd_table[slot][f].used = 0;
    task_set_name(slot, name);
    return slot;
}

static void start_tasks(int first) {
    current_task = first;
    tasks[first].state = TASK_RUNNING;
    scheduler_active = 1;
    tss.esp0 = tasks[first].kstack_top;
    set_cr3(tasks[first].cr3);
    scheduler_start(tasks[first].esp);
    scheduler_active = 0;
    tss.esp0 = 0x00080000;
    __asm__ __volatile__("sti");
}

static int next_ready(void) {
    for (int k = 1; k < NTASKS; k++) {
        int t = (current_task + k) % NTASKS;
        if (tasks[t].state == TASK_READY)
            return t;
    }
    return -1;
}

static void switch_task(int next) {
    current_task = next;
    tasks[next].state = TASK_RUNNING;
    quantum_ticks = 0;
    tss.esp0 = tasks[next].kstack_top;
    set_cr3(tasks[next].cr3);
    switch_to(tasks[next].esp);
    for (;;) {}
}

static void task_free_frames(int slot) {
    if (tasks[slot].code_phys)
        frame_free_pages(tasks[slot].code_phys, tasks[slot].code_pages);
    if (tasks[slot].stack_phys)
        frame_free_pages(tasks[slot].stack_phys, tasks[slot].stack_pages);
    task_free_heap(slot);
    tasks[slot].code_phys = 0;
    tasks[slot].stack_phys = 0;
    tasks[slot].heap_phys = 0;
    tasks[slot].code_pages = 0;
    tasks[slot].stack_pages = 0;
}

static int find_pid(u32 pid) {
    for (int t = 0; t < NTASKS; t++) {
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
    u32 wait_pid = tasks[parent].waiting_pid;
    if (tasks[parent].state == TASK_BLOCKED &&
        wait_pid != WAIT_NONE &&
        (wait_pid == 0 || wait_pid == tasks[child_slot].pid)) {
        ((u32 *)tasks[parent].esp)[7] = tasks[child_slot].exit_code;
        tasks[parent].waiting_pid = WAIT_NONE;
        tasks[parent].state = TASK_READY;
    }
}

static void task_exit(u32 status);

static void sched_timer_tick(u32 *frame) {
    if (!scheduler_active)
        return;

    if (current_task >= 0 && current_task < NTASKS &&
        tasks[current_task].kill_pending) {
        tasks[current_task].kill_pending = 0;
        task_exit(0xFFFFu);
    }

    for (int t = 0; t < NTASKS; t++) {
        if (tasks[t].state == TASK_BLOCKED && tasks[t].sleep_until != 0 &&
            (int)(ticks - tasks[t].sleep_until) >= 0) {
            tasks[t].sleep_until = 0;
            tasks[t].state = TASK_READY;
            ((u32 *)tasks[t].esp)[7] = 0;
        }
    }

    /* Only preempt a real user context, never the kernel itself. */
    if ((frame[9] & 3) != 3)
        return;

    quantum_ticks++;
    if (quantum_ticks < TIME_SLICE_TICKS)
        return;
    quantum_ticks = 0;

    tasks[current_task].esp = (u32)frame;
    tasks[current_task].state = TASK_READY;
    int next = next_ready();
    if (next < 0) {
        tasks[current_task].state = TASK_RUNNING;
        return;
    }
    switch_task(next);
}

static void task_yield(u32 frame) {
    tasks[current_task].esp = (u32)frame;
    tasks[current_task].state = TASK_READY;
    int next = next_ready();
    if (next < 0) {
        tasks[current_task].state = TASK_RUNNING;
        tss.esp0 = tasks[current_task].kstack_top;
        switch_to(frame);
        for (;;) {}
    }
    switch_task(next);
}

static void task_exit(u32 status) {
    int me = current_task;
    tasks[me].exit_code = status;
    tasks[me].state = TASK_DONE;
    wake_parent(me);
    task_free_frames(me);

    for (;;) {
        int next = next_ready();
        if (next >= 0)
            switch_task(next);

        int blocked = 0;
        for (int t = 0; t < NTASKS; t++) {
            if (tasks[t].state == TASK_BLOCKED)
                blocked = 1;
        }
        if (!blocked) {
            scheduler_active = 0;
            tss.esp0 = 0x00080000;
            scheduler_exit_to_kernel();
            for (;;) {}
        }
        __asm__ __volatile__("sti");
        __asm__ __volatile__("hlt");
        __asm__ __volatile__("cli");
    }
}

void exception_handler(u32 *frame) {
    u32 vector = frame[8];
    u32 cs = frame[11];
    (void)frame[9];
    (void)frame[10];

    if (scheduler_active && (cs & 3) == 3) {
        gui_sys_print("[fault] Ring3 task killed; returning to shell.\n");
        __asm__ __volatile__("movw $0x23, %%ax; movw %%ax, %%ds; movw %%ax, %%es"
                             : : : "eax");
        task_exit(0xDEADu);
    }

    gui_sys_print("[fault] kernel exception; CPU halted.\n");
    (void)vector;
    for (;;) {
        __asm__ __volatile__("cli");
        __asm__ __volatile__("hlt");
    }
}

static char *ps_u32(char *p, u32 v) {
    char b[12];
    int n = 0;
    if (v == 0)
        b[n++] = '0';
    while (v) {
        b[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (n)
        *p++ = b[--n];
    return p;
}

static char *ps_str(char *p, const char *s) {
    while (*s)
        *p++ = *s++;
    return p;
}

void task_ps(void) {
    gui_sys_print("PID CON STATE PAGES NAME\n");
    for (int t = 0; t < NTASKS; t++) {
        if (tasks[t].state == TASK_FREE || tasks[t].state == TASK_DONE)
            continue;
        char line[80];
        char *p = line;
        p = ps_u32(p, tasks[t].pid);
        *p++ = ' ';
        p = ps_u32(p, (u32)tasks[t].console_id + 1);
        *p++ = ' ';
        const char *state = "?";
        if (tasks[t].state == TASK_READY) state = "READY";
        else if (tasks[t].state == TASK_RUNNING) state = "RUN";
        else if (tasks[t].state == TASK_BLOCKED) state = "BLOCK";
        p = ps_str(p, state);
        *p++ = ' ';
        p = ps_u32(p, (u32)tasks[t].code_pages + tasks[t].stack_pages +
                         tasks[t].heap_pages);
        *p++ = ' ';
        p = ps_str(p, tasks[t].name);
        *p = 0;
        gui_sys_print(line);
        gui_sys_print("\n");
    }
}

int task_kill_pid(u32 pid) {
    int target = -1;
    for (int t = 0; t < NTASKS; t++) {
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
    wake_parent(target);
    task_free_frames(target);
    tasks[target].state = TASK_DONE;
    return 1;
}

static int task_spawn_user(const char *name) {
    if (!scheduler_active || current_task < 0 || current_task >= NTASKS)
        return 0;
    u32 size = 0;
    __asm__ __volatile__("sti");
    int ok = fs_load_file_to(name, fs_program, FS_MAX_FILE, &size);
    __asm__ __volatile__("cli");
    if (!ok || size == 0)
        return 0;
    int child = task_create(name, fs_program, fs_program + size);
    if (child < 0)
        return 0;
    tasks[child].parent_pid = tasks[current_task].pid;
    tasks[child].console_id = tasks[current_task].console_id;
    return (int)tasks[child].pid;
}

static int task_spawn_args(const char *name, u32 argv, u32 argc) {
    if (!scheduler_active || current_task < 0 || current_task >= NTASKS)
        return 0;
    if (argc > 8 || argv < 0x400000 || argv >= 0x800000)
        return 0;
    u32 size = 0;
    __asm__ __volatile__("sti");
    int ok = fs_load_file_to(name, fs_program, FS_MAX_FILE, &size);
    __asm__ __volatile__("cli");
    if (!ok || size == 0)
        return 0;

    int child = task_create(name, fs_program, fs_program + size);
    if (child < 0)
        return 0;
    tasks[child].parent_pid = tasks[current_task].pid;
    tasks[child].console_id = tasks[current_task].console_id;

    u8 *stackv = (u8 *)direct_ptr(tasks[child].stack_phys);
    u32 sp = 0x402000;
    u32 ptrs[8];
    for (u32 i = 0; i < argc; i++) {
        u32 up = ((u32 *)argv)[i];
        if (up < 0x400000 || up >= 0x800000) {
            tasks[child].state = TASK_FREE;
            task_free_frames(child);
            return 0;
        }
        int len = 0;
        while (len < 64 && *(volatile u8 *)(up + (u32)len) != 0)
            len++;
        if (len == 64) {
            tasks[child].state = TASK_FREE;
            task_free_frames(child);
            return 0;
        }
        sp -= (u32)len + 1;
        if (sp < 0x401000) {
            tasks[child].state = TASK_FREE;
            task_free_frames(child);
            return 0;
        }
        u8 *dst = stackv + (sp - 0x401000);
        for (int j = 0; j <= len; j++)
            dst[j] = *(volatile u8 *)(up + (u32)j);
        ptrs[i] = sp;
    }
    sp &= ~3u;
    sp -= 4;
    *(u32 *)(stackv + (sp - 0x401000)) = 0;
    for (int i = (int)argc - 1; i >= 0; i--) {
        sp -= 4;
        *(u32 *)(stackv + (sp - 0x401000)) = ptrs[i];
    }
    sp -= 4;
    *(u32 *)(stackv + (sp - 0x401000)) = argc;
    ((u32 *)tasks[child].esp)[11] = sp;
    return (int)tasks[child].pid;
}

static int task_wait_block(u32 want_pid, u32 frame) {
    u32 my_pid = tasks[current_task].pid;
    int child = -1;
    for (int t = 0; t < NTASKS; t++) {
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
        tasks[child].state = TASK_FREE;
        ((u32 *)frame)[7] = status;
        return 0;
    }

    tasks[current_task].state = TASK_BLOCKED;
    tasks[current_task].waiting_pid = want_pid;
    tasks[current_task].esp = frame;
    int next = next_ready();
    if (next < 0) {
        tasks[current_task].state = TASK_RUNNING;
        tasks[current_task].waiting_pid = WAIT_NONE;
        return -1;
    }
    switch_task(next);
    for (;;) {}
}

static void task_sleep_block(u32 ms, u32 frame) {
    if (ms == 0) {
        ((u32 *)frame)[7] = 0;
        return;
    }
    u32 wake = ticks + ms / 10 + 1;
    int next = next_ready();
    if (next < 0) {
        __asm__ __volatile__("sti");
        while ((int)(ticks - wake) < 0)
            __asm__ __volatile__("hlt");
        __asm__ __volatile__("cli");
        ((u32 *)frame)[7] = 0;
        return;
    }
    tasks[current_task].state = TASK_BLOCKED;
    tasks[current_task].waiting_pid = WAIT_NONE;
    tasks[current_task].sleep_until = wake;
    tasks[current_task].esp = frame;
    switch_task(next);
    for (;;) {}
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
    task_system_init();
    task_clear_all();

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
    task_system_init();
    task_clear_all();

    int a = task_create("A.BIN", as, as + ae->size);
    int b = task_create("B.BIN", bs, bs + be->size);
    int c = task_create("C.BIN", cs, cs + ce->size);
    if (a < 0 || b < 0 || c < 0) {
        gui_sys_print("[task] cannot create tasks.\n");
        task_clear_all();
        __asm__ __volatile__("sti");
        return;
    }

    start_tasks(a);
}

static int build_args_on_stack(int slot, u32 argc, const char *const *argv,
                               u32 stack_base, u32 stack_top) {
    if (argc > 8 || tasks[slot].stack_phys == 0)
        return 0;
    u8 *stackv = (u8 *)direct_ptr(tasks[slot].stack_phys);
    u32 sp = stack_top;
    u32 ptrs[8];

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
        u8 *dst = stackv + (sp - stack_base);
        for (int j = 0; j <= len; j++)
            dst[j] = (u8)s[j];
        ptrs[i] = sp;
    }
    sp &= ~3u;
    sp -= 4;
    *(u32 *)(stackv + (sp - stack_base)) = 0;
    for (int i = (int)argc - 1; i >= 0; i--) {
        sp -= 4;
        *(u32 *)(stackv + (sp - stack_base)) = ptrs[i];
    }
    sp -= 4;
    if (sp < stack_base + 64)
        return 0;
    *(u32 *)(stackv + (sp - stack_base)) = argc;
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

    if (cache >= 0) {
        size = warm_modules[cache].size;
    } else if (!fs_load_file_to(name, (u8 *)BIG_CODE_PHYS, BIG_CODE_MAX, &size)) {
        gui_sys_print("[user-mod] load failed.\n");
        return 0;
    }

    __asm__ __volatile__("cli");
    gdt_install();
    idt_set_flags(0x80, (u32)syscall_stub, 0xEE);
    task_system_init();
    task_clear_all();

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

    if (!cache_hit)
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

int run_user_module(const char *name) {
    return run_user_module_ex(name, 0, 0);
}

int run_user_module_args(const char *name, int argc, const char *const *argv) {
    return run_user_module_ex(name, argc, argv);
}

int exec_disk(const char *name) {
    u32 size = 0;
    if (!fs_load_file(name, fs_program, FS_MAX_FILE, &size)) {
        gui_sys_print("[disk] file not found or read failed.\n");
        return 0;
    }

    __asm__ __volatile__("cli");
    gdt_install();
    idt_set_flags(0x80, (u32)syscall_stub, 0xEE);
    task_system_init();
    task_clear_all();

    int slot = task_create(name, fs_program, fs_program + size);
    if (slot < 0) {
        gui_sys_print("[task] no free task slot.\n");
        __asm__ __volatile__("sti");
        return 0;
    }
    start_tasks(slot);
    return 1;
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

static int ramfile_find(const char *name) {
    for (int i = 0; i < MAX_RAMFILES; i++) {
        if (ramfiles[i].used && name16_eq(ramfiles[i].name, name))
            return i;
    }
    return -1;
}

static int ramfile_create(const char *name) {
    int idx = -1;
    for (int i = 0; i < MAX_RAMFILES; i++) {
        if (!ramfiles[i].used) {
            idx = i;
            break;
        }
    }
    if (idx < 0)
        return -1;
    u32 phys = frame_alloc_pages(RAMFILE_PAGES);
    if (!phys)
        return -1;
    frame_zero(phys, RAMFILE_PAGES);
    ramfiles[idx].used = 1;
    ramfiles[idx].phys = phys;
    ramfiles[idx].capacity = RAMFILE_PAGES * 0x1000u;
    ramfiles[idx].size = 0;
    for (int i = 0; i < 16; i++)
        ramfiles[idx].name[i] = name[i];
    return idx;
}

static void ramfile_delete(int idx) {
    if (idx < 0 || idx >= MAX_RAMFILES || !ramfiles[idx].used)
        return;
    frame_free_pages(ramfiles[idx].phys, RAMFILE_PAGES);
    ramfiles[idx].used = 0;
    for (int t = 0; t < NTASKS; t++) {
        for (int f = 0; f < MAX_FDS; f++) {
            if (fd_table[t][f].used && fd_table[t][f].kind == 1 &&
                fd_table[t][f].ram_index == idx)
                fd_table[t][f].used = 0;
        }
    }
}

int serial_upload_file(const char *name) {
    int idx = ramfile_find(name);
    if (idx < 0)
        idx = ramfile_create(name);
    if (idx < 0)
        return 0;
    ramfiles[idx].size = 0;
    u8 *p = (u8 *)direct_ptr(ramfiles[idx].phys);
    u32 n = 0;
    u8 old_busy = runtime_busy;
    runtime_busy = 1;
    gui_sys_print("[serial] send bytes, end with Ctrl-Z...\n");
    while (n < ramfiles[idx].capacity) {
        int ch = serial_getc();
        if (ch < 0 || ch == 0x1A)
            break;
        p[n++] = (u8)ch;
    }
    runtime_busy = old_busy;
    ramfiles[idx].size = n;
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
    u8 *p = (u8 *)direct_ptr(r->phys);
    serial_puts("<<<FILE ");
    for (int i = 0; i < 16 && r->name[i]; i++)
        serial_putc(r->name[i]);
    serial_puts(">>>\n");
    for (u32 i = 0; i < r->size; i++)
        serial_putc((char)p[i]);
    serial_puts("\n<<<END>>>\n");
    return 1;
}

void serial_info(void) {
    serial_puts("MiniOS COM1 115200 8N1 ready\r\n");
}

/* -----------------------------------------------------------------------------
 * SaveFS: snapshot RAMFS into the tail of the floppy.
 * ----------------------------------------------------------------------------- */
#define SAVE_HEADER_LBA  2560u
#define SAVE_DATA_LBA    2561u
#define SAVE_DATA_SECTORS 256u
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
    struct savefs_entry entries[8];
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

static int savefs_sync(void) {
    struct savefs_header h;
    u8 *hp = (u8 *)&h;
    for (u32 i = 0; i < sizeof(h); i++)
        hp[i] = 0;
    h.magic[0] = 'M'; h.magic[1] = 'S'; h.magic[2] = 'V'; h.magic[3] = '1';
    h.generation = ++savefs_generation;

    u32 lba = SAVE_DATA_LBA;
    u32 count = 0;
    for (int i = 0; i < MAX_RAMFILES; i++) {
        if (!ramfiles[i].used)
            continue;
        if (count >= 8 || ramfiles[i].size > RAMFILE_PAGES * 0x1000u)
            return 0;
        u32 sectors = (ramfiles[i].size + 511u) / 512u;
        if (lba + sectors > SAVE_DATA_LBA + SAVE_DATA_SECTORS)
            return 0;
        const u8 *data = (const u8 *)direct_ptr(ramfiles[i].phys);
        for (u32 s = 0; s < sectors; s++) {
            for (int j = 0; j < 512; j++)
                fs_sector[j] = 0;
            u32 chunk = ramfiles[i].size - s * 512u;
            if (chunk > 512) chunk = 512;
            for (u32 j = 0; j < chunk; j++)
                fs_sector[j] = data[s * 512u + j];
            if (!fdc_write_sector(lba + s, fs_sector))
                return 0;
        }
        for (int j = 0; j < 16; j++)
            h.entries[count].name[j] = ramfiles[i].name[j];
        h.entries[count].size = ramfiles[i].size;
        h.entries[count].start_lba = lba;
        h.entries[count].crc = crc32_finish(crc32_bytes(0xFFFFFFFFu, data, ramfiles[i].size));
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
        h.magic[2] != 'V' || h.magic[3] != '1')
        return 0;
    u32 stored = h.header_crc;
    h.header_crc = 0;
    if (crc32_finish(crc32_bytes(0xFFFFFFFFu, hp, sizeof(h))) != stored)
        return 0;
    if (h.count > 8)
        return 0;

    for (u32 i = 0; i < h.count; i++) {
        if (h.entries[i].size > RAMFILE_PAGES * 0x1000u)
            return 0;
        u32 sectors = (h.entries[i].size + 511u) / 512u;
        if (h.entries[i].start_lba < SAVE_DATA_LBA ||
            h.entries[i].start_lba + sectors > SAVE_DATA_LBA + SAVE_DATA_SECTORS)
            return 0;
    }

    int made[8];
    int made_count = 0;
    for (u32 i = 0; i < h.count; i++) {
        int idx = ramfile_create(h.entries[i].name);
        if (idx < 0)
            goto fail;
        made[made_count++] = idx;
        struct ramfile *r = &ramfiles[idx];
        u8 *dst = (u8 *)direct_ptr(r->phys);
        u32 sectors = (h.entries[i].size + 511u) / 512u;
        u32 crc = 0xFFFFFFFFu;
        u32 done = 0;
        for (u32 s = 0; s < sectors; s++) {
            if (!fdc_read_sector(h.entries[i].start_lba + s, fs_sector))
                goto fail;
            u32 chunk = h.entries[i].size - done;
            if (chunk > 512) chunk = 512;
            for (u32 j = 0; j < chunk; j++)
                dst[done + j] = fs_sector[j];
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

void sync_savefs(void) {
    if (savefs_sync())
        gui_sys_print("[savefs] snapshot written.\n");
    else
        gui_sys_print("[savefs] snapshot FAILED.\n");
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
    if (current_task < 0 || current_task >= NTASKS)
        return -1;
    __asm__ __volatile__("sti");
    int ok = fs_load_dir();
    __asm__ __volatile__("cli");
    if (!ok)
        return -1;
    struct fs_header *h = (struct fs_header *)fs_dir;
    struct fs_entry *entries = (struct fs_entry *)(fs_dir + sizeof(*h));
    for (int i = 0; i < h->count; i++) {
        if (!fs_name_eq(&entries[i], name))
            continue;
        for (int f = 0; f < MAX_FDS; f++) {
            if (fd_table[current_task][f].used)
                continue;
            fd_table[current_task][f].used = 1;
            fd_table[current_task][f].kind = 0;
            fd_table[current_task][f].writable = 0;
            fd_table[current_task][f].ram_index = 0;
            fd_table[current_task][f].start_lba = entries[i].start_lba;
            fd_table[current_task][f].size = entries[i].size;
            fd_table[current_task][f].offset = 0;
            for (int j = 0; j < 16; j++)
                fd_table[current_task][f].name[j] = entries[i].name[j];
            return f + 1;
        }
        return -1;
    }
    return -1;
}

static int fd_read(int fd, u8 *buf, u32 len) {
    if (fd < 1 || fd > MAX_FDS || current_task < 0 || current_task >= NTASKS)
        return -1;
    struct fd_entry *f = &fd_table[current_task][fd - 1];
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
        u8 *p = (u8 *)direct_ptr(r->phys) + f->offset;
        for (u32 i = 0; i < n; i++)
            buf[i] = p[i];
        f->offset += n;
        f->size = r->size;
        return (int)n;
    }
    u32 done = 0;
    __asm__ __volatile__("sti");
    while (done < len && f->offset < f->size) {
        u32 lba = f->start_lba + f->offset / 512;
        if (!fdc_read_sector(lba, fs_sector)) {
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
    __asm__ __volatile__("cli");
    return (int)done;
}

#define O_CREAT 1
#define O_TRUNC 2
#define O_WRITE 4

static int fd_alloc_slot(void) {
    for (int f = 0; f < MAX_FDS; f++) {
        if (!fd_table[current_task][f].used)
            return f;
    }
    return -1;
}

static int fd_open_ex(const char *name, u32 flags) {
    if (current_task < 0 || current_task >= NTASKS)
        return -1;

    int ri = ramfile_find(name);
    if (ri >= 0) {
        int f = fd_alloc_slot();
        if (f < 0)
            return -1;
        struct fd_entry *e = &fd_table[current_task][f];
        e->used = 1;
        e->kind = 1;
        e->writable = (flags & O_WRITE) ? 1 : 0;
        e->ram_index = (u8)ri;
        e->start_lba = 0;
        e->size = ramfiles[ri].size;
        e->offset = 0;
        if ((flags & O_TRUNC) && e->writable) {
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
        struct fd_entry *e = &fd_table[current_task][f];
        e->used = 1;
        e->kind = 1;
        e->writable = 1;
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
    if (fd < 1 || fd > MAX_FDS || current_task < 0 || current_task >= NTASKS)
        return -1;
    struct fd_entry *f = &fd_table[current_task][fd - 1];
    if (!f->used || f->kind != 1 || !f->writable)
        return -1;
    if (f->ram_index >= MAX_RAMFILES || !ramfiles[f->ram_index].used)
        return -1;
    struct ramfile *r = &ramfiles[f->ram_index];
    if (f->offset + len > r->capacity)
        return -1;
    u8 *p = (u8 *)direct_ptr(r->phys) + f->offset;
    for (u32 i = 0; i < len; i++)
        p[i] = buf[i];
    f->offset += len;
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
    int x, y;
    u32 buttons;
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

static int sys_stat(const char *name, struct user_stat *out) {
    int ri = ramfile_find(name);
    if (ri >= 0) {
        out->size = ramfiles[ri].size;
        out->kind = 2;
        return 0;
    }

    __asm__ __volatile__("sti");
    int ok = fs_load_dir();
    __asm__ __volatile__("cli");
    if (!ok)
        return -1;
    struct fs_header *h = (struct fs_header *)fs_dir;
    struct fs_entry *entries = (struct fs_entry *)(fs_dir + sizeof(*h));
    for (int i = 0; i < h->count; i++) {
        if (fs_name_eq(&entries[i], name)) {
            out->size = entries[i].size;
            out->kind = 1;
            return 0;
        }
    }
    return -1;
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
    int ok = fs_load_dir();
    __asm__ __volatile__("cli");
    if (!ok)
        return 0;
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
            return 1;
        }
        pos++;
    }
    return 0;
}

int kernel_readdir_gui(u32 index, char *name, u32 *size, u32 *kind) {
    struct user_dirent d;
    if (!sys_readdir(index, &d))
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

static u32 task_brk(u32 addr) {
    if (current_task < 0 || current_task >= NTASKS)
        return 0xFFFFFFFFu;
    u16 old_pages = tasks[current_task].heap_pages;
    u32 cur = HEAP_VIRT_BASE + (u32)old_pages * 0x1000u;
    if (addr == 0)
        return cur;
    if (addr < HEAP_VIRT_BASE || addr > HEAP_VIRT_MAX)
        return 0xFFFFFFFFu;
    u32 pages = (addr - HEAP_VIRT_BASE + 0xFFFu) / 0x1000u;

    if (pages < old_pages) {
        for (u32 i = pages; i < old_pages; i++) {
            u32 pte = task_user_pt[current_task][HEAP_PAGE_INDEX + i];
            if (pte & 1)
                frame_free_pages(pte & 0xFFFFF000u, 1);
            task_user_pt[current_task][HEAP_PAGE_INDEX + i] = 0;
        }
        tasks[current_task].heap_pages = (u16)pages;
        tasks[current_task].heap_phys = 0;
        return HEAP_VIRT_BASE + pages * 0x1000u;
    }

    for (u32 i = old_pages; i < pages; i++) {
        u32 phys = frame_alloc_pages(1);
        if (!phys) {
            for (u32 j = old_pages; j < i; j++) {
                u32 pte = task_user_pt[current_task][HEAP_PAGE_INDEX + j];
                if (pte & 1)
                    frame_free_pages(pte & 0xFFFFF000u, 1);
                task_user_pt[current_task][HEAP_PAGE_INDEX + j] = 0;
            }
            tasks[current_task].heap_pages = old_pages;
            return 0xFFFFFFFFu;
        }
        frame_zero(phys, 1);
        if (i == old_pages)
            tasks[current_task].heap_phys = phys;
        task_user_pt[current_task][HEAP_PAGE_INDEX + i] = phys | 0x007;
    }
    tasks[current_task].heap_pages = (u16)pages;
    return HEAP_VIRT_BASE + pages * 0x1000u;
}

static int fd_unlink(const char *name) {
    int ri = ramfile_find(name);
    if (ri < 0)
        return -1;
    ramfile_delete(ri);
    return 0;
}

static int fd_seek(int fd, int offset, int whence) {
    if (fd < 1 || fd > MAX_FDS || current_task < 0 || current_task >= NTASKS)
        return -1;
    struct fd_entry *f = &fd_table[current_task][fd - 1];
    if (!f->used)
        return -1;
    int base = 0;
    if (whence == 0) base = 0;
    else if (whence == 1) base = (int)f->offset;
    else if (whence == 2) base = (int)f->size;
    else return -1;
    int next = base + offset;
    if (next < 0 || (u32)next > f->size)
        return -1;
    f->offset = (u32)next;
    return next;
}

static int fd_close(int fd) {
    if (fd < 1 || fd > MAX_FDS || current_task < 0 || current_task >= NTASKS)
        return -1;
    fd_table[current_task][fd - 1].used = 0;
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
        gui_sys_print("[ring3] yield ignored.\n");
    } else if (number == 4) {
        u32 ptr = ((u32 *)frame)[4];
        u32 len = ((u32 *)frame)[6];
        int ok = (ptr >= 0x400000 && ptr < 0x800000 &&
                  len <= BIG_HEAP_SIZE && ptr + len <= 0x800000);
        if (ok)
            gui_sys_write_console(current_console_id(), (const char *)ptr, len);
        ((u32 *)frame)[7] = ok ? len : 0xFFFFFFFFu;
    } else if (number == 5) {
        ((u32 *)frame)[7] = (u32)module_getchar();
    } else if (number == 6) {
        ((u32 *)frame)[7] = ticks * 10;
    } else if (number == 7) {
        u32 name = ((u32 *)frame)[4];
        u32 dst = ((u32 *)frame)[6];
        u32 max = ((u32 *)frame)[5];
        u32 size = 0;
        if (name >= 0x400000 && name < 0x800000 &&
            dst >= 0x400000 && dst < 0x800000 &&
            max <= BIG_HEAP_SIZE && dst + max <= 0x800000) {
            __asm__ __volatile__("sti");
            int ok = fs_load_file_to((const char *)name, (u8 *)dst, max, &size);
            __asm__ __volatile__("cli");
            ((u32 *)frame)[7] = ok ? size : 0;
        } else {
            ((u32 *)frame)[7] = 0;
        }
    } else if (number == 8) {
        u32 name = ((u32 *)frame)[4];
        int pid = 0;
        if (name >= 0x400000 && name < 0x800000)
            pid = task_spawn_user((const char *)name);
        ((u32 *)frame)[7] = (u32)pid;
    } else if (number == 9) {
        int r = task_wait_block(((u32 *)frame)[4], frame);
        if (r < 0)
            ((u32 *)frame)[7] = 0xFFFFFFFFu;
    } else if (number == 10) {
        task_sleep_block(((u32 *)frame)[4], frame);
    } else if (number == 11) {
        if (scheduler_active)
            task_exit(((u32 *)frame)[4]);
        user_exit_to_kernel();
    } else if (number == 12) {
        u32 name = ((u32 *)frame)[4];
        int fd = -1;
        if (name >= 0x400000 && name < 0x800000)
            fd = fd_open((const char *)name);
        ((u32 *)frame)[7] = (u32)fd;
    } else if (number == 13) {
        u32 fd = ((u32 *)frame)[4];
        u32 buf = ((u32 *)frame)[6];
        u32 len = ((u32 *)frame)[5];
        int r = -1;
        if (buf >= 0x400000 && buf < 0x800000 && len <= 0x10000 &&
            buf + len <= 0x800000)
            r = fd_read((int)fd, (u8 *)buf, len);
        ((u32 *)frame)[7] = (u32)r;
    } else if (number == 14) {
        ((u32 *)frame)[7] = (u32)fd_close((int)((u32 *)frame)[4]);
    } else if (number == 15) {
        ((u32 *)frame)[7] = (u32)fd_seek((int)((u32 *)frame)[4],
                                        (int)((u32 *)frame)[6],
                                        (int)((u32 *)frame)[5]);
    } else if (number == 16) {
        u32 name = ((u32 *)frame)[4];
        u32 flags = ((u32 *)frame)[6];
        int fd = -1;
        if (name >= 0x400000 && name < 0x800000)
            fd = fd_open_ex((const char *)name, flags);
        ((u32 *)frame)[7] = (u32)fd;
    } else if (number == 17) {
        u32 fd = ((u32 *)frame)[4];
        u32 buf = ((u32 *)frame)[6];
        u32 len = ((u32 *)frame)[5];
        int r = -1;
        if (buf >= 0x400000 && buf < 0x800000 && len <= 0x10000 &&
            buf + len <= 0x800000)
            r = fd_write((int)fd, (const u8 *)buf, len);
        ((u32 *)frame)[7] = (u32)r;
    } else if (number == 18) {
        u32 name = ((u32 *)frame)[4];
        int r = -1;
        if (name >= 0x400000 && name < 0x800000)
            r = fd_unlink((const char *)name);
        ((u32 *)frame)[7] = (u32)r;
    } else if (number == 19) {
        u32 name = ((u32 *)frame)[4];
        u32 argv = ((u32 *)frame)[6];
        u32 argc = ((u32 *)frame)[5];
        int pid = 0;
        if (name >= 0x400000 && name < 0x800000)
            pid = task_spawn_args((const char *)name, argv, argc);
        ((u32 *)frame)[7] = (u32)pid;
    } else if (number == 20) {
        ((u32 *)frame)[7] = task_brk(((u32 *)frame)[4]);
    } else if (number == 21) {
        u32 name = ((u32 *)frame)[4];
        u32 out = ((u32 *)frame)[6];
        int r = -1;
        if (name >= 0x400000 && name < 0x800000 &&
            out >= 0x400000 && out + sizeof(struct user_stat) <= 0x800000)
            r = sys_stat((const char *)name, (struct user_stat *)out);
        ((u32 *)frame)[7] = (u32)r;
    } else if (number == 22) {
        u32 index = ((u32 *)frame)[4];
        u32 out = ((u32 *)frame)[6];
        int r = 0;
        if (out >= 0x400000 && out + sizeof(struct user_dirent) <= 0x800000)
            r = sys_readdir(index, (struct user_dirent *)out);
        ((u32 *)frame)[7] = (u32)r;
    } else if (number == 23) {
        __asm__ __volatile__("sti");
        int r = savefs_sync();
        __asm__ __volatile__("cli");
        ((u32 *)frame)[7] = r ? 0 : 0xFFFFFFFFu;
    } else if (number == 24) {
        u32 out = ((u32 *)frame)[4];
        if (out >= 0x400000 && out + sizeof(struct user_sysinfo) <= 0x800000) {
            struct user_sysinfo *si = (struct user_sysinfo *)out;
            u32 free_frames = 0;
            for (u32 i = 0; i < FRAME_COUNT; i++)
                if (!(frame_bitmap[i / 32] & (1u << (i % 32))))
                    free_frames++;
            u32 ntasks = 0;
            for (int i = 0; i < NTASKS; i++)
                if (tasks[i].state != TASK_FREE)
                    ntasks++;
            si->ticks = ticks;
            si->uptime_s = ticks / 100;
            si->free_frames = free_frames;
            si->total_frames = FRAME_COUNT;
            si->heap_used = heap_used;
            si->tasks = ntasks;
            si->last_module_ticks = user_module_last_ticks;
            ((u32 *)frame)[7] = 0;
        } else {
            ((u32 *)frame)[7] = 0xFFFFFFFFu;
        }
    } else if (number == 25) {
        u32 op = ((u32 *)frame)[4];
        u32 title = ((u32 *)frame)[6];
        if (op == 0) {
            gui_canvas_close();
            ((u32 *)frame)[7] = 0;
        } else if (op == 1) {
            if (title >= 0x400000 && title < 0x800000) {
                gui_canvas_open((const char *)title);
                ((u32 *)frame)[7] = 0;
            } else {
                ((u32 *)frame)[7] = 0xFFFFFFFFu;
            }
        } else if (op == 2) {
            ((u32 *)frame)[7] = ((u32)gui_canvas_w() << 16) | (u32)gui_canvas_h();
        } else if (op == 3) {
            ((u32 *)frame)[7] = ((u32)scr_w << 16) | (u32)scr_h;
        } else if (op == 4) {
            ((u32 *)frame)[7] = vbe_phys_base;
        } else if (op == 5) {
            ((u32 *)frame)[7] = (user_module_cold_ticks << 16) |
                                (user_module_warm_ticks & 0xFFFFu);
        } else {
            ((u32 *)frame)[7] = 0xFFFFFFFFu;
        }
    } else if (number == 26) {
        u32 ptr = ((u32 *)frame)[4];
        if (ptr >= 0x400000 && ptr + sizeof(struct gui_canvas_cmd) <= 0x800000) {
            struct gui_canvas_cmd cmd;
            u8 *dst = (u8 *)&cmd;
            const u8 *src = (const u8 *)ptr;
            for (u32 i = 0; i < sizeof(cmd); i++)
                dst[i] = src[i];
            int r = -1;
            if (cmd.text &&
                (u32)cmd.text >= 0x400000 &&
                (u32)cmd.text + cmd.len <= 0x800000)
                r = gui_canvas_cmd(&cmd);
            else if (!cmd.text && cmd.op != 4)
                r = gui_canvas_cmd(&cmd);
            ((u32 *)frame)[7] = (u32)r;
        } else {
            ((u32 *)frame)[7] = 0xFFFFFFFFu;
        }
    } else if (number == 27) {
        u32 out = ((u32 *)frame)[4];
        int r = 0;
        if (out >= 0x400000 && out + sizeof(struct gui_canvas_event) <= 0x800000) {
            struct gui_canvas_event ev;
            if (gui_canvas_event(&ev)) {
                struct user_canvas_event *ue = (struct user_canvas_event *)out;
                ue->x = ev.x;
                ue->y = ev.y;
                ue->buttons = ev.buttons;
                ue->closed = ev.closed;
                ue->width = ev.width;
                ue->height = ev.height;
                r = 1;
            }
        }
        ((u32 *)frame)[7] = (u32)r;
    } else if (number == 28) {
        u32 op = ((u32 *)frame)[4];
        u32 a = ((u32 *)frame)[6];
        u32 b = ((u32 *)frame)[5];
        if (op == 0)
            pc_beep(a, b);
        ((u32 *)frame)[7] = 0;
    } else if (number == 29) {
        int cid = current_console_id();
        if (cid < 0 || cid >= KBD_CONSOLES)
            cid = 0;
        int found = 0;
        u8 t = kbd_tail[cid];
        while (t != kbd_head[cid]) {
            if (kbd_buf[cid][t] == 3) {
                kbd_tail[cid] = (u8)((t + 1) & 63);
                found = 1;
                break;
            }
            t = (u8)((t + 1) & 63);
        }
        ((u32 *)frame)[7] = (u32)found;
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
void kmain(void) {
    gdt_install();
    cpu_vendor();
    serial_init();
    task_system_init();
    vbe_init();
    gui_alloc_fb();
    gui_init();
    interrupts_setup();
    idt_set_flags(0x80, (u32)syscall_stub, 0xEE);
    mouse_init();

    interrupts_on();
    savefs_restore();
    gui_redraw();

    u32 last_draw = ticks;

    for (;;) {
        int event = 0;

        if (gui_special_pending) {
            u8 special = gui_special_pending;
            gui_special_pending = 0;
            gui_special(special);
            event = 1;
        }

        int cid = gui_focused_console();
        if (cid < 0 || cid >= KBD_CONSOLES)
            cid = 0;
        while (kbd_tail[cid] != kbd_head[cid]) {
            u8 ch = kbd_buf[cid][kbd_tail[cid]];
            kbd_tail[cid] = (u8)((kbd_tail[cid] + 1) & 63);
            gui_key((char)ch);
            event = 1;
        }

        if (mouse_event) {
            mouse_event = 0;
            if (!gui_cursor_only_motion(mouse_x, mouse_y, mouse_buttons)) {
                gui_mouse_event(mouse_x, mouse_y, mouse_buttons);
                event = 1;
            }
        }

        if (event || gui_dirty_pending() || (u32)(ticks - last_draw) >= 10) {
            gui_redraw();
            last_draw = ticks;
        }

        __asm__ __volatile__("hlt");
    }
}













