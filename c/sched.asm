; Cooperative scheduler entry/exit and context switch.
;   void scheduler_start(u32 first_task_esp);
;   void scheduler_exit_to_kernel(void);
;   void switch_to(u32 task_esp);
        BITS 32
        global scheduler_start
        global scheduler_exit_to_kernel
        global switch_to

        section .bss
sched_saved_esp:
        resd    1

        section .text
scheduler_start:
        mov     [sched_saved_esp], esp
        mov     eax, [esp + 4]
        mov     esp, eax
        mov     ax, 0x23
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax
        popa
        iretd

scheduler_resume:
        sti
        ret

scheduler_exit_to_kernel:
        cli
        mov     ax, 0x10
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax
        mov     ss, ax
        mov     esp, [sched_saved_esp]
        jmp     scheduler_resume

switch_to:
        mov     eax, [esp + 4]
        mov     esp, eax
        mov     ax, 0x23
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax
        popa
        iretd
