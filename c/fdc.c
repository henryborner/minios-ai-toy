/* Runtime 1.44 MB floppy driver: IRQ6 + 8237 DMA channel 2, CHS reads. */
#include "fdc.h"

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

extern volatile u32 ticks;

#define FDC_DOR   0x3F2
#define FDC_MSR   0x3F4
#define FDC_FIFO  0x3F5

#define FDC_DMA_BUF 0x00050000u

static volatile u8 fdc_irq_seen;
static u8 fdc_ready;
static u8 cur_cyl;
static u8 cur_head;

static inline u8 inb(u16 port) {
    u8 v;
    __asm__ __volatile__("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outb(u16 port, u8 v) {
    __asm__ __volatile__("outb %0, %1" : : "a"(v), "Nd"(port));
}

void fdc_on_irq(void) {
    fdc_irq_seen = 1;
}

static void delay_ms(int ms) {
    u32 start = ticks;
    u32 wait = (u32)(ms / 10 + 1);
    while ((u32)(ticks - start) < wait) {}
}

static int wait_irq(int ms) {
    u32 start = ticks;
    u32 wait = (u32)(ms / 10 + 1);
    while ((u32)(ticks - start) < wait) {
        if (fdc_irq_seen)
            return 1;
    }
    return fdc_irq_seen;
}

static int wait_msr(u8 mask, u8 value) {
    for (u32 i = 0; i < 1000000; i++) {
        if ((inb(FDC_MSR) & mask) == value)
            return 1;
    }
    return 0;
}

static int fdc_write(u8 v) {
    if (!wait_msr(0xC0, 0x80))
        return 0;
    outb(FDC_FIFO, v);
    return 1;
}

static int fdc_read_byte(void) {
    if (!wait_msr(0xC0, 0xC0))
        return -1;
    return inb(FDC_FIFO);
}

static void fdc_sense(void) {
    fdc_write(0x08);
    (void)fdc_read_byte();
    (void)fdc_read_byte();
}

static void dma_prepare(void) {
    outb(0x0A, 0x04);           /* mask channel 2 */
    outb(0x0C, 0xFF);           /* clear flip-flop */
    outb(0x04, (u8)(FDC_DMA_BUF & 0xFF));
    outb(0x04, (u8)((FDC_DMA_BUF >> 8) & 0xFF));
    outb(0x81, (u8)((FDC_DMA_BUF >> 16) & 0xFF));
    outb(0x0C, 0xFF);
    outb(0x05, 0xFF);           /* 512 bytes -> count 511 */
    outb(0x05, 0x01);
    outb(0x0B, 0x46);           /* single, increment, memory write, ch2 */
    outb(0x0A, 0x00);           /* unmask all channels */
}

static int fdc_recalibrate(void) {
    fdc_irq_seen = 0;
    if (!fdc_write(0x07) || !fdc_write(0x00))
        return 0;
    if (!wait_irq(2000))
        return 0;
    fdc_sense();
    cur_cyl = 0;
    cur_head = 0;
    return 1;
}

static int fdc_seek(u8 cyl, u8 head) {
    if (fdc_ready && cyl == cur_cyl && head == cur_head)
        return 1;
    fdc_irq_seen = 0;
    if (!fdc_write(0x0F) ||
        !fdc_write(head ? 0x04 : 0x00) ||
        !fdc_write(cyl))
        return 0;
    if (!wait_irq(2000))
        return 0;
    fdc_sense();
    cur_cyl = cyl;
    cur_head = head;
    return 1;
}

int fdc_init(void) {
    if (fdc_ready)
        return 1;

    fdc_irq_seen = 0;
    outb(FDC_DOR, 0x00);
    delay_ms(30);
    outb(FDC_DOR, 0x1C);        /* motor 0, normal, DMA/IRQ */
    delay_ms(200);

    if (!wait_irq(3000))
        return 0;
    for (int i = 0; i < 4; i++)
        fdc_sense();

    fdc_write(0x03);            /* specify */
    fdc_write(0xDF);
    fdc_write(0x02);

    if (!fdc_recalibrate())
        return 0;
    fdc_ready = 1;
    return 1;
}

int fdc_read_sector(u32 lba, u8 *dst) {
    if (lba >= 2880)
        return 0;
    if (!fdc_init())
        return 0;

    u32 track = lba / 18;
    u8 cyl = (u8)(track / 2);
    u8 head = (u8)(track % 2);
    u8 sector = (u8)(lba % 18 + 1);

    if (!fdc_seek(cyl, head))
        return 0;

    dma_prepare();
    fdc_irq_seen = 0;

    if (!fdc_write(0xE6) ||
        !fdc_write(head ? 0x04 : 0x00) ||
        !fdc_write(cyl) ||
        !fdc_write(head) ||
        !fdc_write(sector) ||
        !fdc_write(0x02) ||
        !fdc_write(0x12) ||
        !fdc_write(0x1B) ||
        !fdc_write(0xFF))
        return 0;

    if (!wait_irq(3000))
        return 0;

    u8 st0 = (u8)fdc_read_byte();
    u8 st1 = (u8)fdc_read_byte();
    (void)fdc_read_byte();
    (void)fdc_read_byte();
    (void)fdc_read_byte();
    (void)fdc_read_byte();
    (void)fdc_read_byte();
    if ((st0 & 0xC0) != 0 || st1 & 0x20)
        return 0;

    const u8 *src = (const u8 *)FDC_DMA_BUF;
    for (int i = 0; i < 512; i++)
        dst[i] = src[i];
    return 1;
}



