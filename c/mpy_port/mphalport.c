#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "py/runtime.h"
#include "py/mphal.h"
#include "py/mpstate.h"
#include "shared/runtime/interrupt_char.h"
#include "syscall.h"

static uint32_t vm_poll_counter;

void minios_vm_poll_hook(void) {
    if ((++vm_poll_counter & 0x3FFu) == 0) {
        if (sys_poll_ctrl_c()) {
            mp_sched_keyboard_interrupt();
        }
        mp_handle_pending(true);
    }
}

mp_uint_t mp_hal_ticks_ms(void) {
    return (mp_uint_t)sys_ticks_ms();
}

mp_uint_t mp_hal_ticks_us(void) {
    return (mp_uint_t)(sys_ticks_ms() * 1000u);
}

mp_uint_t mp_hal_ticks_cpu(void) {
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    (void)hi;
    return (mp_uint_t)lo;
}

uint64_t mp_hal_time_ns(void) {
    return (uint64_t)sys_ticks_ms() * 1000000ull;
}

void mp_hal_delay_ms(mp_uint_t ms) {
    mp_uint_t start = mp_hal_ticks_ms();
    while ((mp_uint_t)(mp_hal_ticks_ms() - start) < ms) {
        mp_uint_t rem = ms - (mp_uint_t)(mp_hal_ticks_ms() - start);
        uint32_t step = rem > 20 ? 20 : (uint32_t)rem;
        if (step > 0) {
            sys_sleep_ms(step);
        }
        if (sys_poll_ctrl_c()) {
            mp_sched_keyboard_interrupt();
        }
        mp_handle_pending(true);
    }
}

void mp_hal_delay_us(mp_uint_t us) {
    if (us >= 10000) {
        mp_hal_delay_ms(us / 1000);
        us %= 1000;
    }
    uint32_t loops = (uint32_t)us * 50u;
    while (loops--) {
        __asm__ __volatile__("pause");
    }
}

int mp_hal_stdin_rx_chr(void) {
    for (;;) {
        mp_handle_pending(true);
        int c = sys_getchar();
        if (c == mp_interrupt_char) {
            /* readline consumes Ctrl-C while a line is being edited; do not
               raise NLR here, because the REPL read path has no NLR frame. */
            return c;
        }
        if (c == '\n') {
            return '\r';
        }
        return c;
    }
}

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        size_t chunk = len - sent;
        if (chunk > 2048) {
            chunk = 2048;
        }
        (void)sys_write_bytes(str + sent, (uint32_t)chunk);
        sent += chunk;
    }
    return (mp_uint_t)len;
}
