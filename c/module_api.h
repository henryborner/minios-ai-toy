#ifndef MINIOS_MODULE_API_H
#define MINIOS_MODULE_API_H

#include <stdint.h>

#define MINIOS_MODULE_BASE 0x00100000u
#define MINIOS_MODULE_MAX  0x00100000u
#define MINIOS_MODULE_HEAP 0x00200000u

struct minios_api {
    uint32_t version;
    void (*puts)(const char *s);
    void (*putc)(char c);
    uint32_t (*ticks_ms)(void);
    int (*getchar)(void);
    uint8_t *heap;
    uint32_t heap_size;
    void (*write)(const char *s, uint32_t len);
};

struct minios_module_header {
    uint8_t  magic[4];       /* "MPM1" */
    uint32_t entry_offset;   /* from 0x100000 */
    uint32_t image_size;     /* includes this header */
    uint32_t bss_size;
    uint32_t heap_size;
} __attribute__((packed));

#endif
