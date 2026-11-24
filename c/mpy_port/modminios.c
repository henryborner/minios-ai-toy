#include <stdint.h>
#include <string.h>

#include "py/runtime.h"
#include "py/objtuple.h"
#include "py/objstr.h"
#include "syscall.h"

static void canvas_cmd(struct minios_canvas_cmd *cmd) {
    (void)sys_canvas_draw(cmd);
}

static mp_obj_t minios_open(size_t n_args, const mp_obj_t *args) {
    const char *title = n_args >= 1 ? mp_obj_str_get_str(args[0]) : "Canvas";
    return mp_obj_new_int(sys_canvas_ctl(1, title));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(minios_open_obj, 0, 1, minios_open);

static mp_obj_t minios_close(void) {
    return mp_obj_new_int(sys_canvas_ctl(0, NULL));
}
static MP_DEFINE_CONST_FUN_OBJ_0(minios_close_obj, minios_close);

static mp_obj_t minios_clear(mp_obj_t color_in) {
    struct minios_canvas_cmd cmd = {0};
    cmd.op = 0;
    cmd.color = mp_obj_get_int(color_in);
    canvas_cmd(&cmd);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(minios_clear_obj, minios_clear);

static mp_obj_t minios_pixel(mp_obj_t x_in, mp_obj_t y_in, mp_obj_t c_in) {
    struct minios_canvas_cmd cmd = {0};
    cmd.op = 1;
    cmd.x = mp_obj_get_int(x_in);
    cmd.y = mp_obj_get_int(y_in);
    cmd.color = mp_obj_get_int(c_in);
    canvas_cmd(&cmd);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(minios_pixel_obj, minios_pixel);

static mp_obj_t minios_rect(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    mp_obj_t x_in = args[0], y_in = args[1], w_in = args[2];
    mp_obj_t h_in = args[3], c_in = args[4];
    struct minios_canvas_cmd cmd = {0};
    cmd.op = 2;
    cmd.x = mp_obj_get_int(x_in);
    cmd.y = mp_obj_get_int(y_in);
    cmd.w = mp_obj_get_int(w_in);
    cmd.h = mp_obj_get_int(h_in);
    cmd.color = mp_obj_get_int(c_in);
    canvas_cmd(&cmd);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(minios_rect_obj, 5, 5, minios_rect);

static mp_obj_t minios_line(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    mp_obj_t x0 = args[0], y0 = args[1], x1 = args[2];
    mp_obj_t y1 = args[3], c_in = args[4];
    struct minios_canvas_cmd cmd = {0};
    cmd.op = 3;
    cmd.x = mp_obj_get_int(x0);
    cmd.y = mp_obj_get_int(y0);
    cmd.w = mp_obj_get_int(x1);
    cmd.h = mp_obj_get_int(y1);
    cmd.color = mp_obj_get_int(c_in);
    canvas_cmd(&cmd);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(minios_line_obj, 5, 5, minios_line);

static mp_obj_t minios_text(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    mp_obj_t x_in = args[0], y_in = args[1];
    mp_obj_t text_in = args[2], c_in = args[3];
    struct minios_canvas_cmd cmd = {0};
    const char *s = mp_obj_str_get_str(text_in);
    cmd.op = 4;
    cmd.x = mp_obj_get_int(x_in);
    cmd.y = mp_obj_get_int(y_in);
    cmd.color = mp_obj_get_int(c_in);
    cmd.text = s;
    cmd.len = (uint32_t)strlen(s);
    canvas_cmd(&cmd);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(minios_text_obj, 4, 4, minios_text);

static mp_obj_t minios_event(void) {
    struct minios_canvas_event ev;
    if (!sys_canvas_event(&ev))
        return mp_const_none;
    mp_obj_t items[4] = {
        mp_obj_new_int(ev.x),
        mp_obj_new_int(ev.y),
        mp_obj_new_int_from_uint(ev.buttons),
        mp_obj_new_int_from_uint(ev.closed),
    };
    return mp_obj_new_tuple(4, items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(minios_event_obj, minios_event);

static mp_obj_t minios_sysinfo(void) {
    struct minios_sysinfo si;
    if (sys_sysinfo(&si) != 0)
        mp_raise_OSError(5);
    mp_obj_t items[7] = {
        mp_obj_new_int_from_uint(si.ticks),
        mp_obj_new_int_from_uint(si.uptime_s),
        mp_obj_new_int_from_uint(si.free_frames),
        mp_obj_new_int_from_uint(si.total_frames),
        mp_obj_new_int_from_uint(si.heap_used),
        mp_obj_new_int_from_uint(si.tasks),
        mp_obj_new_int_from_uint(si.last_module_ticks),
    };
    return mp_obj_new_tuple(7, items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(minios_sysinfo_obj, minios_sysinfo);

static mp_obj_t minios_beep(mp_obj_t freq_in, mp_obj_t ms_in) {
    (void)minios_syscall3(SYS_HW_IO, 0,
                          (uint32_t)mp_obj_get_int(freq_in),
                          (uint32_t)mp_obj_get_int(ms_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(minios_beep_obj, minios_beep);

typedef struct _minios_canvas_obj_t {
    mp_obj_base_t base;
} minios_canvas_obj_t;

static mp_obj_t canvas_m_clear(mp_obj_t self, mp_obj_t color) {
    (void)self;
    return minios_clear(color);
}
static MP_DEFINE_CONST_FUN_OBJ_2(canvas_m_clear_obj, canvas_m_clear);

static mp_obj_t canvas_m_pixel(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    return minios_pixel(args[1], args[2], args[3]);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(canvas_m_pixel_obj, 4, 4, canvas_m_pixel);

static mp_obj_t canvas_m_rect(size_t n_args, const mp_obj_t *args) {
    return minios_rect(n_args - 1, args + 1);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(canvas_m_rect_obj, 6, 6, canvas_m_rect);

static mp_obj_t canvas_m_line(size_t n_args, const mp_obj_t *args) {
    return minios_line(n_args - 1, args + 1);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(canvas_m_line_obj, 6, 6, canvas_m_line);

static mp_obj_t canvas_m_text(size_t n_args, const mp_obj_t *args) {
    return minios_text(n_args - 1, args + 1);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(canvas_m_text_obj, 5, 5, canvas_m_text);

static mp_obj_t canvas_m_event(mp_obj_t self) {
    (void)self;
    return minios_event();
}
static MP_DEFINE_CONST_FUN_OBJ_1(canvas_m_event_obj, canvas_m_event);

static mp_obj_t canvas_m_close(mp_obj_t self) {
    (void)self;
    return minios_close();
}
static MP_DEFINE_CONST_FUN_OBJ_1(canvas_m_close_obj, canvas_m_close);

static mp_obj_t canvas_m_beep(mp_obj_t self, mp_obj_t freq, mp_obj_t ms) {
    (void)self;
    return minios_beep(freq, ms);
}
static MP_DEFINE_CONST_FUN_OBJ_3(canvas_m_beep_obj, canvas_m_beep);

static const mp_rom_map_elem_t canvas_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_clear), MP_ROM_PTR(&canvas_m_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_pixel), MP_ROM_PTR(&canvas_m_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_rect), MP_ROM_PTR(&canvas_m_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_line), MP_ROM_PTR(&canvas_m_line_obj) },
    { MP_ROM_QSTR(MP_QSTR_text), MP_ROM_PTR(&canvas_m_text_obj) },
    { MP_ROM_QSTR(MP_QSTR_event), MP_ROM_PTR(&canvas_m_event_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&canvas_m_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_beep), MP_ROM_PTR(&canvas_m_beep_obj) },
};
static MP_DEFINE_CONST_DICT(canvas_locals, canvas_locals_table);

static mp_obj_t minios_canvas_make_new(const mp_obj_type_t *type, size_t n_args,
                                       size_t n_kw, const mp_obj_t *args) {
    (void)n_kw;
    const char *title = n_args ? mp_obj_str_get_str(args[0]) : "Canvas";
    (void)sys_canvas_ctl(1, title);
    minios_canvas_obj_t *o = mp_obj_malloc(minios_canvas_obj_t, type);
    return MP_OBJ_FROM_PTR(o);
}

MP_DEFINE_CONST_OBJ_TYPE(
    minios_canvas_type,
    MP_QSTR_Canvas,
    MP_TYPE_FLAG_NONE,
    make_new, minios_canvas_make_new,
    locals_dict, &canvas_locals
);

static const mp_rom_map_elem_t minios_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_minios) },
    { MP_ROM_QSTR(MP_QSTR_Canvas), MP_ROM_PTR(&minios_canvas_type) },
    { MP_ROM_QSTR(MP_QSTR_open), MP_ROM_PTR(&minios_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&minios_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear), MP_ROM_PTR(&minios_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_pixel), MP_ROM_PTR(&minios_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_rect), MP_ROM_PTR(&minios_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_line), MP_ROM_PTR(&minios_line_obj) },
    { MP_ROM_QSTR(MP_QSTR_text), MP_ROM_PTR(&minios_text_obj) },
    { MP_ROM_QSTR(MP_QSTR_event), MP_ROM_PTR(&minios_event_obj) },
    { MP_ROM_QSTR(MP_QSTR_sysinfo), MP_ROM_PTR(&minios_sysinfo_obj) },
    { MP_ROM_QSTR(MP_QSTR_beep), MP_ROM_PTR(&minios_beep_obj) },
};
static MP_DEFINE_CONST_DICT(minios_module_globals, minios_module_globals_table);

const mp_obj_module_t mp_module_minios = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&minios_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_minios, mp_module_minios);



