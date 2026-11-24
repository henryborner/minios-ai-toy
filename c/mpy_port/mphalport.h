#ifndef MINIOS_MPHALPORT_H
#define MINIOS_MPHALPORT_H

#include <stddef.h>
#include <stdint.h>
#include "py/mpconfig.h"
#include "shared/runtime/interrupt_char.h"

mp_uint_t mp_hal_ticks_ms(void);
mp_uint_t mp_hal_ticks_us(void);
mp_uint_t mp_hal_ticks_cpu(void);
uint64_t mp_hal_time_ns(void);

void mp_hal_delay_ms(mp_uint_t ms);
void mp_hal_delay_us(mp_uint_t us);

int mp_hal_stdin_rx_chr(void);
mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len);
void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len);
void mp_hal_stdout_tx_str(const char *str);

#endif
