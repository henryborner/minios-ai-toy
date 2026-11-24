#ifndef MINIOS_MP_TIME_PORT_H
#define MINIOS_MP_TIME_PORT_H

#include "py/obj.h"
#include "syscall.h"

static inline mp_obj_t mp_time_time_get(void) {
    return mp_obj_new_float((mp_float_t)sys_ticks_ms() * (mp_float_t)0.001);
}

#endif
