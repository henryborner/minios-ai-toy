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
sched_saved_cr3:
        resd    1

        section .text
scheduler_start:
        push    ebp
        push    ebx
        push    esi
        push    edi
        mov     eax, cr3
        mov     [sched_saved_cr3], eax
        mov     [sched_saved_esp], esp
        mov     eax, [esp + 20]     ; first_task_esp after 16 bytes saved regs
        mov     esp, eax
        mov     ax, 0x23
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax
        popa
        iretd

scheduler_resume:
        pop     edi
        pop     esi
        pop     ebx
        pop     ebp
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
        mov     eax, [sched_saved_cr3]
        mov     cr3, eax
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
