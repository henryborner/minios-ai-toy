#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "py/builtin.h"
#include "py/compile.h"
#include "py/runtime.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/mperrno.h"
#include "py/stackctrl.h"
#include "py/nlr.h"
#include "py/mphal.h"
#include "shared/runtime/pyexec.h"
#include "shared/runtime/interrupt_char.h"
#include "syscall.h"

#define USER_HEAP_START 0x00500000u
#define USER_HEAP_INIT  0x00600000u /* Default 1 MB mapped by task_create_big */
#define USER_HEAP_GOAL  0x00680000u /* Grow to 1.5 MB via brk (syscall 20) */

extern void nlr_jump_asm(nlr_buf_t *top) __attribute__((noreturn));
extern void minios_gc_collect_regs(uintptr_t stack_top);

static uintptr_t saved_stack_top;

NORETURN void nlr_jump(void *val) {
    MP_NLR_JUMP_HEAD(val, top)
    nlr_jump_asm(top);
}

void nlr_jump_fail(void *val) {
    (void)val;
    printf("[mpy] FATAL: uncaught NLR\n");
    sys_exit_status(1);
}

NORETURN void __fatal_error(const char *msg) {
    printf("[mpy] FATAL: %s\n", msg ? msg : "unknown");
    sys_exit_status(1);
}

void gc_collect(void) {
    gc_collect_start();
    minios_gc_collect_regs(saved_stack_top);
    gc_collect_end();
}

static int exec_str(const char *src, mp_parse_input_kind_t kind, bool print_expr) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, src, strlen(src), 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, kind);
        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, false);
        mp_obj_t res = mp_call_function_0(module_fun);
        nlr_pop();
        if (print_expr && res != mp_const_none) {
            mp_obj_print(res, PRINT_REPR);
            mp_printf(&mp_plat_print, "\n");
        }
        return 0;
    } else {
        mp_obj_t exc = MP_OBJ_FROM_PTR(nlr.ret_val);
        if (mp_obj_is_subclass_fast(MP_OBJ_FROM_PTR(((mp_obj_base_t *)nlr.ret_val)->type), MP_OBJ_FROM_PTR(&mp_type_SystemExit))) {
            return 0;
        }
        mp_obj_print_exception(&mp_plat_print, exc);
        return 1;
    }
}

int mpy_main(int argc, char **argv) {
    volatile uint32_t stack_anchor = 0;
    saved_stack_top = (uintptr_t)&stack_anchor;

    mp_stack_ctrl_init();
    mp_stack_set_limit(48 * 1024); /* 64 KB user stack; keep 16 KB safety margin */

    uint32_t heap_end = sys_brk(USER_HEAP_GOAL);
    if (heap_end == 0xFFFFFFFFu || heap_end <= USER_HEAP_START) {
        heap_end = USER_HEAP_INIT;
    }

    gc_init((void *)USER_HEAP_START, (void *)heap_end);
    mp_init();

    /* Configure sys.path = ['', '.'] and sys.argv */
    mp_obj_list_init(MP_OBJ_TO_PTR(mp_sys_path), 0);
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR_));
    mp_obj_list_init(MP_OBJ_TO_PTR(mp_sys_argv), 0);

    mp_hal_set_interrupt_char(3); /* Ctrl-C */

    int status = 0;

    if (argc >= 3 && strcmp(argv[1], "-c") == 0) {
        mp_obj_list_append(mp_sys_argv, mp_obj_new_str("-c", 2));
        for (int i = 3; i < argc; i++) {
            mp_obj_list_append(mp_sys_argv, mp_obj_new_str(argv[i], strlen(argv[i])));
        }
        status = exec_str(argv[2], MP_PARSE_FILE_INPUT, false);
    } else if (argc >= 3 && strcmp(argv[1], "-e") == 0) {
        status = exec_str(argv[2], MP_PARSE_EVAL_INPUT, true);
    } else if (argc >= 3 && strcmp(argv[1], "-m") == 0) {
        char buf[80];
        snprintf(buf, sizeof(buf), "import %s", argv[2]);
        for (int i = 2; i < argc; i++) {
            mp_obj_list_append(mp_sys_argv, mp_obj_new_str(argv[i], strlen(argv[i])));
        }
        status = exec_str(buf, MP_PARSE_FILE_INPUT, false);
    } else if (argc >= 2 && argv[1][0] != '-') {
        for (int i = 1; i < argc; i++) {
            mp_obj_list_append(mp_sys_argv, mp_obj_new_str(argv[i], strlen(argv[i])));
        }
        int r = pyexec_file(argv[1]);
        status = (r == 1) ? 0 : 1;
    } else {
        pyexec_friendly_repl();
    }

    mp_deinit();
    return status;
}
