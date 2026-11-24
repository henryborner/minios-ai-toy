#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "py/builtin.h"
#include "py/runtime.h"
#include "py/stream.h"
#include "py/reader.h"
#include "py/lexer.h"
#include "py/mperrno.h"
#include "py/objstr.h"
#include "py/objtuple.h"
#include "syscall.h"

/* Normalize path like "./foo.py" or "/FOO.PY" to 15-char MiniOS flat name */
static void normalize_name(const char *src, char out[16]) {
    while (src[0] == '.' && src[1] == '/') {
        src += 2;
    }
    while (*src == '/') {
        src++;
    }
    int i = 0;
    while (*src && i < 15) {
        out[i++] = (char)toupper((unsigned char)*src++);
    }
    out[i] = '\0';
}

/* -----------------------------------------------------------------------------
 * Built-in open() stream object backed by MiniOS syscalls 13..17
 * ----------------------------------------------------------------------------- */
typedef struct _minios_file_obj_t {
    mp_obj_base_t base;
    int fd;
    bool is_text;
    bool writable;
} minios_file_obj_t;

extern const mp_obj_type_t minios_file_type;

static void file_check_open(minios_file_obj_t *self) {
    if (self->fd <= 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("I/O operation on closed file"));
    }
}

static mp_uint_t minios_file_read(mp_obj_t o_in, void *buf, mp_uint_t size, int *errcode) {
    minios_file_obj_t *self = MP_OBJ_TO_PTR(o_in);
    if (self->fd <= 0) {
        *errcode = MP_EBADF;
        return MP_STREAM_ERROR;
    }
    mp_uint_t total = 0;
    uint8_t *dst = (uint8_t *)buf;
    while (total < size) {
        uint32_t chunk = (uint32_t)(size - total);
        if (chunk > 4096) chunk = 4096;
        int n = sys_read(self->fd, dst + total, chunk);
        if (n < 0) {
            *errcode = MP_EIO;
            return MP_STREAM_ERROR;
        }
        if (n == 0) {
            break;
        }
        total += (mp_uint_t)n;
    }
    return total;
}

static mp_uint_t minios_file_write(mp_obj_t o_in, const void *buf, mp_uint_t size, int *errcode) {
    minios_file_obj_t *self = MP_OBJ_TO_PTR(o_in);
    if (self->fd <= 0 || !self->writable) {
        *errcode = MP_EBADF;
        return MP_STREAM_ERROR;
    }
    int n = sys_write(self->fd, buf, (uint32_t)size);
    if (n < 0) {
        *errcode = MP_ENOSPC;
        return MP_STREAM_ERROR;
    }
    return (mp_uint_t)n;
}

static mp_uint_t minios_file_ioctl(mp_obj_t o_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    minios_file_obj_t *self = MP_OBJ_TO_PTR(o_in);
    if (request == MP_STREAM_SEEK) {
        file_check_open(self);
        struct mp_stream_seek_t *s = (struct mp_stream_seek_t *)arg;
        int r = sys_seek(self->fd, (int32_t)s->offset, s->whence);
        if (r < 0) {
            *errcode = MP_EINVAL;
            return MP_STREAM_ERROR;
        }
        s->offset = r;
        return 0;
    } else if (request == MP_STREAM_FLUSH) {
        return 0;
    } else if (request == MP_STREAM_CLOSE) {
        if (self->fd > 0) {
            sys_close(self->fd);
            self->fd = -1;
        }
        return 0;
    }
    *errcode = MP_EINVAL;
    return MP_STREAM_ERROR;
}

static mp_obj_t minios_file_close_method(mp_obj_t self_in) {
    minios_file_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->fd > 0) {
        sys_close(self->fd);
        self->fd = -1;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(minios_file_close_obj, minios_file_close_method);

static mp_obj_t minios_file_exit(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    return minios_file_close_method(args[0]);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(minios_file_exit_obj, 4, 4, minios_file_exit);

static const mp_rom_map_elem_t minios_file_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mp_stream_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&mp_stream_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_readline), MP_ROM_PTR(&mp_stream_unbuffered_readline_obj) },
    { MP_ROM_QSTR(MP_QSTR_readlines), MP_ROM_PTR(&mp_stream_unbuffered_readlines_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mp_stream_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_flush), MP_ROM_PTR(&mp_stream_flush_obj) },
    { MP_ROM_QSTR(MP_QSTR_seek), MP_ROM_PTR(&mp_stream_seek_obj) },
    { MP_ROM_QSTR(MP_QSTR_tell), MP_ROM_PTR(&mp_stream_tell_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&minios_file_close_obj) },
    { MP_ROM_QSTR(MP_QSTR___enter__), MP_ROM_PTR(&mp_identity_obj) },
    { MP_ROM_QSTR(MP_QSTR___exit__), MP_ROM_PTR(&minios_file_exit_obj) },
};
static MP_DEFINE_CONST_DICT(minios_file_locals_dict, minios_file_locals_dict_table);

static const mp_stream_p_t minios_textio_stream_p = {
    .read = minios_file_read,
    .write = minios_file_write,
    .ioctl = minios_file_ioctl,
    .is_text = true,
};

static const mp_stream_p_t minios_fileio_stream_p = {
    .read = minios_file_read,
    .write = minios_file_write,
    .ioctl = minios_file_ioctl,
    .is_text = false,
};

MP_DEFINE_CONST_OBJ_TYPE(
    minios_textfile_type,
    MP_QSTR_TextIOWrapper,
    MP_TYPE_FLAG_ITER_IS_STREAM,
    protocol, &minios_textio_stream_p,
    locals_dict, &minios_file_locals_dict
);

MP_DEFINE_CONST_OBJ_TYPE(
    minios_binfile_type,
    MP_QSTR_FileIO,
    MP_TYPE_FLAG_ITER_IS_STREAM,
    protocol, &minios_fileio_stream_p,
    locals_dict, &minios_file_locals_dict
);

mp_obj_t mp_builtin_open(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    (void)kwargs;
    const char *raw_path = mp_obj_str_get_str(args[0]);
    const char *mode = (n_args >= 2) ? mp_obj_str_get_str(args[1]) : "r";

    char name[16];
    normalize_name(raw_path, name);

    bool is_binary = false;
    bool writable = false;
    bool append = false;
    uint32_t flags = 0;

    for (const char *m = mode; *m; m++) {
        if (*m == 'b') is_binary = true;
        else if (*m == 'w') { writable = true; flags |= MINIOS_O_CREAT | MINIOS_O_TRUNC | MINIOS_O_WRITE; }
        else if (*m == 'a') { writable = true; append = true; flags |= MINIOS_O_CREAT | MINIOS_O_WRITE; }
        else if (*m == '+') { writable = true; flags |= MINIOS_O_WRITE; }
    }

    int fd = sys_open_ex(name, flags);
    if (fd < 0) {
        mp_raise_OSError(writable ? MP_EACCES : MP_ENOENT);
    }
    if (append) {
        (void)sys_seek(fd, 0, 2);
    }

    minios_file_obj_t *o = mp_obj_malloc(minios_file_obj_t, is_binary ? &minios_binfile_type : &minios_textfile_type);
    o->fd = fd;
    o->is_text = !is_binary;
    o->writable = writable;
    return MP_OBJ_FROM_PTR(o);
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, mp_builtin_open);

/* -----------------------------------------------------------------------------
 * Module import stat & file lexer (RAMFS + MiniFS overlay)
 * ----------------------------------------------------------------------------- */
mp_import_stat_t mp_import_stat(const char *path) {
    char name[16];
    normalize_name(path, name);
    if (name[0] == '\0') {
        return MP_IMPORT_STAT_NO_EXIST;
    }
    struct minios_stat st;
    if (sys_stat_file(name, &st) == 0) {
        return MP_IMPORT_STAT_FILE;
    }
    int fd = sys_open_ex(name, 0);
    if (fd > 0) {
        sys_close(fd);
        return MP_IMPORT_STAT_FILE;
    }
    return MP_IMPORT_STAT_NO_EXIST;
}

typedef struct _minios_reader_t {
    int fd;
} minios_reader_t;

static mp_uint_t minios_reader_readbyte(void *data) {
    minios_reader_t *r = (minios_reader_t *)data;
    uint8_t ch = 0;
    int n = sys_read(r->fd, &ch, 1);
    if (n <= 0) {
        return MP_READER_EOF;
    }
    return ch;
}

static void minios_reader_close(void *data) {
    minios_reader_t *r = (minios_reader_t *)data;
    if (r->fd > 0) {
        sys_close(r->fd);
        r->fd = -1;
    }
    m_del_obj(minios_reader_t, r);
}

mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    char name[16];
    normalize_name(qstr_str(filename), name);
    int fd = sys_open_ex(name, 0);
    if (fd < 0) {
        mp_raise_OSError(MP_ENOENT);
    }
    minios_reader_t *rp = m_new_obj(minios_reader_t);
    rp->fd = fd;
    mp_reader_t reader = {
        .data = rp,
        .readbyte = minios_reader_readbyte,
        .close = minios_reader_close,
    };
    return mp_lexer_new(filename, reader);
}

/* -----------------------------------------------------------------------------
 * Built-in `os` module for MiniOS
 * ----------------------------------------------------------------------------- */
static mp_obj_t os_listdir(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    (void)args;
    mp_obj_t list = mp_obj_new_list(0, NULL);
    struct minios_dirent de;
    for (uint32_t idx = 0; idx < 64; idx++) {
        if (sys_readdir_idx(idx, &de) <= 0) {
            break;
        }
        size_t len = strnlen(de.name, 16);
        mp_obj_list_append(list, mp_obj_new_str(de.name, len));
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(os_listdir_obj, 0, 1, os_listdir);

static mp_obj_t os_stat_fn(mp_obj_t path_in) {
    char name[16];
    normalize_name(mp_obj_str_get_str(path_in), name);
    struct minios_stat st;
    if (sys_stat_file(name, &st) != 0) {
        mp_raise_OSError(MP_ENOENT);
    }
    /* Standard 10-tuple (mode, ino, dev, nlink, uid, gid, size, atime, mtime, ctime) */
    mp_obj_t items[10] = {
        MP_OBJ_NEW_SMALL_INT(0100644),
        MP_OBJ_NEW_SMALL_INT(0),
        MP_OBJ_NEW_SMALL_INT((mp_int_t)st.kind),
        MP_OBJ_NEW_SMALL_INT(1),
        MP_OBJ_NEW_SMALL_INT(0),
        MP_OBJ_NEW_SMALL_INT(0),
        mp_obj_new_int_from_uint(st.size),
        MP_OBJ_NEW_SMALL_INT(0),
        MP_OBJ_NEW_SMALL_INT(0),
        MP_OBJ_NEW_SMALL_INT(0),
    };
    return mp_obj_new_tuple(10, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(os_stat_obj, os_stat_fn);

static mp_obj_t os_unlink_fn(mp_obj_t path_in) {
    char name[16];
    normalize_name(mp_obj_str_get_str(path_in), name);
    if (sys_unlink(name) != 0) {
        mp_raise_OSError(MP_ENOENT);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(os_unlink_obj, os_unlink_fn);

static mp_obj_t os_sync_fn(void) {
    (void)sys_sync_fs();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(os_sync_obj, os_sync_fn);

static mp_obj_t os_uname_fn(void) {
    mp_obj_t items[5] = {
        mp_obj_new_str("minios", 6),
        mp_obj_new_str("minios", 6),
        mp_obj_new_str("2.0", 3),
        mp_obj_new_str("MiniOS 2.0 Protected-C", 22),
        mp_obj_new_str("i386", 4),
    };
    return mp_obj_new_tuple(5, items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(os_uname_obj, os_uname_fn);

static const mp_rom_map_elem_t mp_module_os_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_os) },
    { MP_ROM_QSTR(MP_QSTR_listdir), MP_ROM_PTR(&os_listdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_stat), MP_ROM_PTR(&os_stat_obj) },
    { MP_ROM_QSTR(MP_QSTR_remove), MP_ROM_PTR(&os_unlink_obj) },
    { MP_ROM_QSTR(MP_QSTR_unlink), MP_ROM_PTR(&os_unlink_obj) },
    { MP_ROM_QSTR(MP_QSTR_sync), MP_ROM_PTR(&os_sync_obj) },
    { MP_ROM_QSTR(MP_QSTR_uname), MP_ROM_PTR(&os_uname_obj) },
};
static MP_DEFINE_CONST_DICT(mp_module_os_globals, mp_module_os_globals_table);

const mp_obj_module_t mp_module_os_minios = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_os_globals,
};

MP_REGISTER_MODULE(MP_QSTR_os, mp_module_os_minios);
