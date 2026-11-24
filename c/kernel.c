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
extern void syscall_stub(void);
extern void isr_default(void);
extern void run_user(u32 entry, u32 user_stack);
extern void user_exit_to_kernel(void);
extern void scheduler_start(u32 first_task_esp);
extern void scheduler_exit_to_kernel(void);
extern void switch_to(u32 task_esp);
extern const u8 user_blob_start[];
extern const u8 user_blob_end[];
extern const u8 task_a_start[];
extern const u8 task_a_end[];
extern const u8 task_b_start[];
extern const u8 task_b_end[];

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

void irq0_handler(u32 *frame) {
    ticks++;
    outb(0x20, 0x20);
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

static void gdt_set(int n, u32 base, u32 limit, u8 access, u8 gran) {
    gdt[n].limit_lo = (u16)(limit & 0xFFFF);
    gdt[n].base_lo = (u16)(base & 0xFFFF);
    gdt[n].base_mid = (u8)((base >> 16) & 0xFF);
    gdt[n].access = access;
    gdt[n].gran = (u8)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[n].base_hi = (u8)((base >> 24) & 0xFF);
}

static void gdt_install(void) {
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
    __asm__ __volatile__("lgdt %0" : : "m"(gdtp));

    if (!tss_loaded) {
        u16 tr = 0x28;
        __asm__ __volatile__("ltr %0" : : "rm"(tr));
        tss_loaded = 1;
    }
}

/* -----------------------------------------------------------------------------
 * Paging: identity-map the first 8 MB, mark the 4 MB user pages U/S.
 * ----------------------------------------------------------------------------- */
static u32 page_directory[1024] __attribute__((aligned(4096)));
static u32 page_table0[1024] __attribute__((aligned(4096)));
static u32 page_table1[1024] __attribute__((aligned(4096)));

static void paging_install(void) {
    for (int i = 0; i < 1024; i++) {
        page_directory[i] = 0;
        page_table0[i] = 0;
        page_table1[i] = 0;
    }

    page_directory[0] = (u32)page_table0 | 0x003;   /* supervisor 0-4 MB */
    page_directory[1] = (u32)page_table1 | 0x007;   /* user-capable 4-8 MB */

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
 * Cooperative tasks: two user address spaces, round-robin on SYS_YIELD.
 * ----------------------------------------------------------------------------- */
#define NTASKS 2
#define TASK_READY   1
#define TASK_RUNNING 2
#define TASK_DONE    3

#define TASK_A_CODE  0x00300000u
#define TASK_A_STACK 0x00301000u
#define TASK_B_CODE  0x00302000u
#define TASK_B_STACK 0x00303000u
#define TIME_SLICE_TICKS 1

struct task {
    u32 esp;
    u32 cr3;
    u32 kstack_top;
    u8  state;
};

static struct task tasks[NTASKS];
static int current_task;
static volatile u8 scheduler_active;
static u32 quantum_ticks;

static u32 task_kernel_pt[1024] __attribute__((aligned(4096)));
static u32 task_pd[NTASKS][1024] __attribute__((aligned(4096)));
static u32 task_user_pt[NTASKS][1024] __attribute__((aligned(4096)));
static u8 task_kstack[NTASKS][4096] __attribute__((aligned(16)));

/* Never modified while active: safe kernel mapping used during task setup. */
static u32 sched_safe_pd[1024] __attribute__((aligned(4096)));
static u32 sched_safe_pt[1024] __attribute__((aligned(4096)));
static u8 sched_safe_ready;

static void set_cr3(u32 value) {
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(value));
}

static void task_copy_page(u32 dst, const u8 *src, const u8 *end) {
    u32 len = (u32)(end - src);
    if (len > 4096)
        len = 4096;
    u8 *d = (u8 *)dst;
    for (u32 i = 0; i < 4096; i++)
        d[i] = 0;
    for (u32 i = 0; i < len; i++)
        d[i] = src[i];
}

static void task_setup(void) {
    if (!sched_safe_ready) {
        for (int i = 0; i < 1024; i++) {
            sched_safe_pt[i] = (u32)(i * 0x1000) | 0x003;
            sched_safe_pd[i] = 0;
        }
        sched_safe_pd[0] = (u32)sched_safe_pt | 0x003;
        sched_safe_ready = 1;
    }
    set_cr3((u32)sched_safe_pd);

    for (int i = 0; i < 1024; i++)
        task_kernel_pt[i] = (u32)(i * 0x1000) | 0x003;

    for (int t = 0; t < NTASKS; t++) {
        for (int i = 0; i < 1024; i++) {
            task_pd[t][i] = 0;
            task_user_pt[t][i] = 0;
        }
        task_pd[t][0] = (u32)task_kernel_pt | 0x003;
        task_pd[t][1] = (u32)task_user_pt[t] | 0x007;
        task_user_pt[t][0] = (t == 0 ? TASK_A_CODE : TASK_B_CODE) | 0x007;
        task_user_pt[t][1] = (t == 0 ? TASK_A_STACK : TASK_B_STACK) | 0x007;

        if (t == 0)
            task_copy_page(TASK_A_CODE, task_a_start, task_a_end);
        else
            task_copy_page(TASK_B_CODE, task_b_start, task_b_end);

        u8 *stack = (u8 *)(t == 0 ? TASK_A_STACK : TASK_B_STACK);
        for (int i = 0; i < 4096; i++)
            stack[i] = 0;

        u32 top = (u32)(task_kstack[t] + 4096);
        u32 *frame = (u32 *)(top - 52);
        for (int i = 0; i < 13; i++)
            frame[i] = 0;
        frame[8] = 0x00400000;      /* EIP */
        frame[9] = 0x1B;            /* CS, RPL3 */
        frame[10] = 0x202;          /* EFLAGS, IF=1 */
        frame[11] = 0x00402000;     /* user ESP */
        frame[12] = 0x23;           /* SS, RPL3 */

        tasks[t].esp = (u32)frame;
        tasks[t].kstack_top = top;
        tasks[t].cr3 = (u32)task_pd[t];
        tasks[t].state = TASK_READY;
    }

    set_cr3((u32)task_pd[0]);
    u32 cr0;
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));
    if (!(cr0 & 0x80000000u)) {
        cr0 |= 0x80000000u;
        __asm__ __volatile__("mov %0, %%cr0" : : "r"(cr0));
    }
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

static void sched_timer_tick(u32 *frame) {
    if (!scheduler_active)
        return;

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

static void task_exit(void) {
    tasks[current_task].state = TASK_DONE;
    int next = next_ready();
    if (next < 0) {
        scheduler_active = 0;
        tss.esp0 = 0x00080000;
        scheduler_exit_to_kernel();
        for (;;) {}
    }
    switch_task(next);
}

void run_task_demo(void) {
    __asm__ __volatile__("cli");
    gdt_install();
    idt_set_flags(0x80, (u32)syscall_stub, 0xEE);
    task_setup();

    scheduler_active = 1;
    current_task = 0;
    tasks[0].state = TASK_RUNNING;
    tasks[1].state = TASK_READY;
    tss.esp0 = tasks[0].kstack_top;
    set_cr3(tasks[0].cr3);

    scheduler_start(tasks[0].esp);

    scheduler_active = 0;
    tss.esp0 = 0x00080000;
    __asm__ __volatile__("sti");
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
        gui_sys_print((const char *)arg);
    } else if (number == 2) {
        if (scheduler_active) {
            gui_sys_print("[task] exit; scheduler.\n");
            task_exit();
        }
        gui_sys_print("[ring3] exit syscall; returning to GUI.\n");
        user_exit_to_kernel();
    } else if (number == 3) {
        if (scheduler_active)
            task_yield(frame);
        gui_sys_print("[ring3] yield ignored.\n");
    } else {
        gui_sys_print("[ring3] unknown syscall.\n");
    }
}

void enter_ring3_demo(void) {
    __asm__ __volatile__("cli");
    gdt_install();
    idt_set_flags(0x80, (u32)syscall_stub, 0xEE);   /* DPL3 interrupt gate */
    paging_install();
    run_user(0x400000, 0x402000);
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




