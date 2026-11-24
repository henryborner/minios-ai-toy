/* =============================================================================
 *  MiniOS 2.0 - 32-bit protected-mode C kernel
 *  Freestanding C, no libc.  Assembly is limited to boot/entry/IRQ stubs.
 * ============================================================================= */

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

#define VGA_COLS 80
#define VGA_ROWS 25

static volatile u16 *const VGA = (volatile u16 *)0xB8000;
static int row;
static int col;
static u8 attr;

static char cpu[13];
static volatile u32 ticks;
static volatile u8 kbd_buf[64];
static volatile u8 kbd_head;
static volatile u8 kbd_tail;
static u8 shift_down;
static u8 extended;
static char cmd[80];
static u32 cmd_len;

static u8 heap[64 * 1024];
static u32 heap_used;

/* -----------------------------------------------------------------------------
 * CPU I/O
 * ----------------------------------------------------------------------------- */
static inline u8 inb(u16 port) {
    u8 value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(u16 port, u8 value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

static inline void interrupts_on(void) {
    __asm__ __volatile__("sti");
}

static inline void halt_cpu(void) {
    __asm__ __volatile__("cli; hlt");
}

/* -----------------------------------------------------------------------------
 * VGA text console
 * ----------------------------------------------------------------------------- */
static void vga_cursor(void) {
    u16 pos = (u16)(row * VGA_COLS + col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (u8)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (u8)(pos >> 8));
}

static void fill_row(int r, u8 color) {
    for (int c = 0; c < VGA_COLS; c++)
        VGA[r * VGA_COLS + c] = ((u16)color << 8) | ' ';
}

static void clear_screen(void) {
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++)
        VGA[i] = 0x0720;
    row = 0;
    col = 0;
    attr = 0x07;
    vga_cursor();
}

static void scroll(void) {
    for (int i = 0; i < VGA_COLS * (VGA_ROWS - 1); i++)
        VGA[i] = VGA[i + VGA_COLS];
    fill_row(VGA_ROWS - 1, 0x07);
    row = VGA_ROWS - 1;
}

static void putc(char ch) {
    if (ch == '\r')
        return;
    if (ch == '\n') {
        col = 0;
        row++;
        if (row >= VGA_ROWS)
            scroll();
        vga_cursor();
        return;
    }
    if (ch == '\b') {
        if (col > 0) {
            col--;
            VGA[row * VGA_COLS + col] = ((u16)attr << 8) | ' ';
            vga_cursor();
        }
        return;
    }
    if (col >= VGA_COLS) {
        col = 0;
        row++;
        if (row >= VGA_ROWS)
            scroll();
    }
    VGA[row * VGA_COLS + col] = ((u16)attr << 8) | (u8)ch;
    col++;
    vga_cursor();
}

static void puts(const char *s) {
    while (*s)
        putc(*s++);
}

static void print_u32(u32 value) {
    char buf[12];
    int i = 0;
    if (value == 0) {
        putc('0');
        return;
    }
    while (value) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (i)
        putc(buf[--i]);
}

/* -----------------------------------------------------------------------------
 * Strings / shell helpers
 * ----------------------------------------------------------------------------- */
static int streq(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static int starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s++ != *prefix++)
            return 0;
    }
    return 1;
}

static void draw_header(void) {
    clear_screen();
    fill_row(0, 0x1F);
    fill_row(1, 0x1F);
    row = 0;
    col = 0;
    attr = 0x1F;
    puts(" MiniOS 2.0 - C Kernel / Protected Mode");
    row = 1;
    col = 0;
    puts(" 32-bit | IDT + PIC + PIT@100Hz + keyboard IRQ | VGA 80x25");
    attr = 0x07;
    row = 3;
    col = 0;
    puts("Type HELP for commands.\n\n");
    vga_cursor();
}

static void prompt(void) {
    puts("MiniOS> ");
}

static void show_help(void) {
    puts("Commands:\n");
    puts("  HELP     this list\n");
    puts("  VER      kernel version\n");
    puts("  CPU      CPU vendor (CPUID)\n");
    puts("  MEM      heap / memory info\n");
    puts("  UPTIME   PIT ticks and elapsed time\n");
    puts("  CLEAR    clear screen\n");
    puts("  ECHO ... print text\n");
    puts("  ABOUT    about this prototype\n");
    puts("  REBOOT   reset the machine\n");
    puts("  HALT     stop the CPU\n");
}

static void cmd_mem(void) {
    puts("Kernel base: 0x00010000\n");
    puts("Heap:        ");
    print_u32(heap_used);
    puts(" / ");
    print_u32((u32)sizeof(heap));
    puts(" bytes\n");
    puts("Stack top:   0x00090000\n");
    puts("PIT ticks:   ");
    print_u32(ticks);
    putc('\n');
}

static void cmd_uptime(void) {
    u32 sec = ticks / 100;
    u32 h = sec / 3600;
    u32 m = (sec / 60) % 60;
    u32 s = sec % 60;
    puts("Uptime: ");
    print_u32(h);
    putc(':');
    if (m < 10) putc('0');
    print_u32(m);
    putc(':');
    if (s < 10) putc('0');
    print_u32(s);
    putc('\n');
}

static void cmd_about(void) {
    puts("MiniOS 2.0 - C kernel prototype\n");
    puts("1.44 MB bootable floppy image\n");
    puts("clang i386 freestanding + lld\n");
    puts("Assembly: boot, entry, IRQ stubs only\n");
}

static void run_command(char *line) {
    while (*line == ' ')
        line++;
    if (!*line)
        return;

    if (streq(line, "help")) {
        show_help();
    } else if (streq(line, "ver")) {
        puts("MiniOS 2.0 C kernel (protected mode)\n");
    } else if (streq(line, "cpu")) {
        puts("CPU: ");
        puts(cpu);
        putc('\n');
    } else if (streq(line, "mem")) {
        cmd_mem();
    } else if (streq(line, "uptime")) {
        cmd_uptime();
    } else if (streq(line, "clear")) {
        draw_header();
    } else if (streq(line, "about")) {
        cmd_about();
    } else if (streq(line, "echo")) {
        putc('\n');
    } else if (starts_with(line, "echo ")) {
        puts(line + 5);
        putc('\n');
    } else if (streq(line, "reboot")) {
        puts("Rebooting...\n");
        outb(0x64, 0xFE);
        halt_cpu();
        for (;;) {}
    } else if (streq(line, "halt")) {
        puts("CPU halted.\n");
        halt_cpu();
        for (;;) {}
    } else {
        puts("Unknown command. Type HELP.\n");
    }
}

static void handle_key(char ch) {
    if (ch == '\n') {
        putc('\n');
        cmd[cmd_len] = 0;
        run_command(cmd);
        cmd_len = 0;
        prompt();
        return;
    }
    if (ch == '\b') {
        if (cmd_len > 0) {
            cmd_len--;
            putc('\b');
        }
        return;
    }
    if (ch >= 32 && ch < 127 && cmd_len < sizeof(cmd) - 1) {
        cmd[cmd_len++] = ch;
        putc(ch);
    }
}

/* -----------------------------------------------------------------------------
 * Keyboard IRQ
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

static void kbd_push(char ch) {
    u8 next = (u8)((kbd_head + 1) & 63);
    if (next != kbd_tail) {
        kbd_buf[kbd_head] = (u8)ch;
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

    char ch = keymap[sc];
    if (!ch)
        return;
    if (shift_down && ch >= 'a' && ch <= 'z')
        ch = (char)(ch - 32);
    kbd_push(ch);
}

void irq1_handler(void) {
    u8 sc = inb(0x60);
    keyboard_scancode(sc);
    outb(0x20, 0x20);
}

void irq0_handler(void) {
    ticks++;
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
extern void isr_default(void);

static void idt_set(int n, u32 handler) {
    idt[n].base_lo = (u16)(handler & 0xFFFF);
    idt[n].selector = 0x08;
    idt[n].zero = 0;
    idt[n].flags = 0x8E;
    idt[n].base_hi = (u16)(handler >> 16);
}

static void pit_init(void) {
    u32 divisor = 11932;        /* 1193182 / 100 Hz */
    outb(0x43, 0x36);
    outb(0x40, (u8)(divisor & 0xFF));
    outb(0x40, (u8)(divisor >> 8));
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

    outb(0x21, 0xFC);   /* IRQ0 (PIT) + IRQ1 (keyboard) */
    outb(0xA1, 0xFF);
}

static void interrupts_init(void) {
    __asm__ __volatile__("cli");

    for (int i = 0; i < 256; i++)
        idt_set(i, (u32)isr_default);
    idt_set(0x20, (u32)irq0_stub);
    idt_set(0x21, (u32)irq1_stub);

    idtp.limit = (u16)(sizeof(idt) - 1);
    idtp.base = (u32)&idt;
    __asm__ __volatile__("lidt %0" : : "m"(idtp));

    pic_remap();
    pit_init();
    interrupts_on();
}

/* -----------------------------------------------------------------------------
 * Misc kernel services
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

void *kmalloc(u32 size) {
    size = (size + 3) & ~3u;
    if (heap_used + size > sizeof(heap))
        return 0;
    void *p = &heap[heap_used];
    heap_used += size;
    return p;
}

/* -----------------------------------------------------------------------------
 * Kernel entry
 * ----------------------------------------------------------------------------- */
void kmain(void) {
    cpu_vendor();
    draw_header();
    interrupts_init();
    prompt();

    for (;;) {
        while (kbd_tail != kbd_head) {
            char ch = (char)kbd_buf[kbd_tail];
            kbd_tail = (u8)((kbd_tail + 1) & 63);
            handle_key(ch);
        }
        __asm__ __volatile__("hlt");
    }
}
