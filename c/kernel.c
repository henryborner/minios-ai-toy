/* =============================================================================
 *  MiniOS 2.0 C kernel - first protected-mode milestone
 *  No libc, no standard headers.  Everything here is freestanding C.
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

static inline u8 inb(u16 port) {
    u8 value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
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
        return;
    }
    if (ch == '\b') {
        if (col > 0) {
            col--;
            VGA[row * VGA_COLS + col] = ((u16)attr << 8) | ' ';
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
}

static void puts(const char *s) {
    while (*s)
        putc(*s++);
}

static void cpu_vendor(char *out) {
    u32 a, b, c, d;
    __asm__ __volatile__("cpuid"
                         : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                         : "a"(0));
    (void)a;
    ((u32 *)out)[0] = b;
    ((u32 *)out)[1] = d;
    ((u32 *)out)[2] = c;
    out[12] = 0;
}

/* US Scancode Set 1, unshifted. 0 means unmapped. */
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

void kmain(void) {
    char vendor[13];
    cpu_vendor(vendor);

    clear_screen();
    fill_row(0, 0x1F);
    fill_row(1, 0x1F);
    row = 0;
    col = 0;
    attr = 0x1F;
    puts(" MiniOS 2.0 - C Kernel Prototype");
    row = 1;
    col = 0;
    puts(" 32-bit protected mode  |  clang i386  |  VGA text 80x25");
    attr = 0x07;
    row = 3;
    col = 0;

    puts("Boot: BIOS -> real-mode loader -> protected mode\n");
    puts("CPU:  ");
    puts(vendor);
    puts("\n");
    puts("GDT:  flat 4 GB code/data, A20 enabled, IRQs off\n");
    puts("Video: 0xB8000 text-mode framebuffer\n");
    puts("This is freestanding C, not a masked assembly demo.\n");
    puts("\n");
    puts("Keyboard polling demo (no IDT yet) - type something:\n");
    puts("> ");

    u8 shift = 0;
    u8 extended = 0;

    for (;;) {
        u8 status = inb(0x64);
        if (!(status & 1))
            continue;

        u8 sc = inb(0x60);

        if (status & 0x20)      /* auxiliary/mouse byte */
            continue;

        if (sc == 0xE0) {
            extended = 1;
            continue;
        }
        if (extended) {
            extended = 0;
            continue;
        }

        if (sc & 0x80) {        /* key release */
            u8 make = sc & 0x7F;
            if (make == 0x2A || make == 0x36)
                shift = 0;
            continue;
        }
        if (sc == 0x2A || sc == 0x36) {
            shift = 1;
            continue;
        }

        char ch = keymap[sc];
        if (!ch)
            continue;

        if (shift && ch >= 'a' && ch <= 'z')
            ch = (char)(ch - 32);

        if (ch == '\n') {
            putc('\n');
            puts("> ");
        } else {
            putc(ch);
        }
    }
}
