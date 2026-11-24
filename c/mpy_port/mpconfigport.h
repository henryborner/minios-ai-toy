#ifndef MINIOS_MPCONFIGPORT_H
#define MINIOS_MPCONFIGPORT_H

#include <stdint.h>
#include <alloca.h>

// Base feature level: core + everyday ergonomics
#define MICROPY_CONFIG_ROM_LEVEL            (MICROPY_CONFIG_ROM_LEVEL_CORE_FEATURES)

// Architecture & NLR exception handling (auto-detected for __i386__, implemented in start.asm)
#define MICROPY_NLR_SETJMP                  (0)
#define MICROPY_GCREGS_SETJMP               (0)

// Compiler, GC & REPL
#define MICROPY_ENABLE_COMPILER             (1)
#define MICROPY_ENABLE_GC                   (1)
#define MICROPY_HELPER_REPL                 (1)
#define MICROPY_REPL_AUTO_INDENT            (1)
#define MICROPY_ENABLE_SOURCE_LINE          (1)
#define MICROPY_ERROR_REPORTING             (MICROPY_ERROR_REPORTING_DETAILED)
#define MICROPY_KBD_EXCEPTION               (1)
#define MICROPY_ENABLE_SCHEDULER            (1)
#define MICROPY_STACK_CHECK                 (1)
#define MICROPY_ENABLE_EXTERNAL_IMPORT      (1)
#define MICROPY_READER_VFS                  (0)

// Numeric precision: arbitrary-precision ints + 64-bit IEEE-754 double floats
#define MICROPY_LONGINT_IMPL                (MICROPY_LONGINT_IMPL_MPZ)
#define MICROPY_FLOAT_IMPL                  (MICROPY_FLOAT_IMPL_DOUBLE)
#define MICROPY_USE_INTERNAL_PRINTF         (1)
#define MICROPY_USE_INTERNAL_ERRNO          (1)

// Language & Built-in ergonomics for everyday tool usage
#define MICROPY_PY_FSTRINGS                 (1)
#define MICROPY_PY_BUILTINS_INPUT           (1)
#define MICROPY_PY_BUILTINS_HELP            (1)
#define MICROPY_PY_BUILTINS_EXECFILE        (1)
#define MICROPY_PY_BUILTINS_POW3            (1)
#define MICROPY_PY_BUILTINS_ROUND_INT       (1)
#define MICROPY_PY_BUILTINS_SLICE_ATTRS     (1)
#define MICROPY_PY_BUILTINS_SLICE_INDICES   (1)
#define MICROPY_PY_BUILTINS_STR_UNICODE     (1)
#define MICROPY_PY_BUILTINS_STR_CENTER      (1)
#define MICROPY_PY_BUILTINS_STR_PARTITION   (1)
#define MICROPY_PY_BUILTINS_STR_SPLITLINES  (1)
#define MICROPY_PY_BUILTINS_MEMORYVIEW      (1)
#define MICROPY_PY_BUILTINS_SET             (1)
#define MICROPY_PY_BUILTINS_FROZENSET       (1)
#define MICROPY_PY_BUILTINS_PROPERTY        (1)
#define MICROPY_PY_BUILTINS_ENUMERATE       (1)
#define MICROPY_PY_BUILTINS_FILTER          (1)
#define MICROPY_PY_BUILTINS_REVERSED        (1)
#define MICROPY_PY_BUILTINS_MIN_MAX         (1)
#define MICROPY_PY_ALL_SPECIAL_METHODS      (1)
#define MICROPY_PY_REVERSE_SPECIAL_METHODS  (1)
#define MICROPY_PY_SYS_EXIT                 (1)
#define MICROPY_PY_SYS_MODULES              (1)
#define MICROPY_PY_SYS_PATH_ARGV_DEFAULTS   (1)
#define MICROPY_PY_SYS_PLATFORM             "minios"
#define MICROPY_PY_SYS_STDIO_BUFFER         (1)

// Standard library modules
#define MICROPY_PY_ARRAY                    (1)
#define MICROPY_PY_COLLECTIONS              (1)
#define MICROPY_PY_COLLECTIONS_DEQUE        (1)
#define MICROPY_PY_COLLECTIONS_ORDEREDDICT  (1)
#define MICROPY_PY_MATH                     (1)
#define MICROPY_PY_CMATH                    (0)
#define MICROPY_PY_GC                       (1)
#define MICROPY_PY_IO                       (1)
#define MICROPY_PY_IO_IOBASE                (1)
#define MICROPY_PY_STRUCT                   (1)
#define MICROPY_PY_ERRNO                    (1)
#define MICROPY_PY_JSON                     (1)
#define MICROPY_PY_RE                       (1)
#define MICROPY_PY_BINASCII                 (1)
#define MICROPY_PY_HEAPQ                    (1)
#define MICROPY_PY_RANDOM                   (1)
#define MICROPY_PY_RANDOM_EXTRA_FUNCS       (1)
#define MICROPY_PY_TIME                     (1)
#define MICROPY_PY_TIME_TIME_TIME_NS        (1)
#define MICROPY_PY_TIME_INCLUDEFILE         "c/mpy_port/mp_time_port.h"
#define MICROPY_PY_MICROPYTHON_MEM_INFO     (1)

#define MICROPY_ALLOC_PATH_MAX              (64)
#define MICROPY_ALLOC_PARSE_CHUNK_INIT      (64)

// Type definitions for 32-bit i386
typedef int32_t mp_int_t;
typedef uint32_t mp_uint_t;
typedef int32_t mp_off_t;

#define MP_SSIZE_MAX                        (0x7FFFFFFF)
#define MICROPY_HW_BOARD_NAME               "MiniOS-2.0"
#define MICROPY_HW_MCU_NAME                 "i386-ring3"
#define MP_STATE_PORT                       MP_STATE_VM

// Periodic scheduler hook inside VM loop
#define MICROPY_VM_HOOK_LOOP \
    do { \
        extern void minios_vm_poll_hook(void); \
        minios_vm_poll_hook(); \
    } while (0);

#endif
