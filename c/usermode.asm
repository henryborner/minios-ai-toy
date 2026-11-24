; Enter Ring 3 and return to the kernel when syscall 2 is issued.
;   void run_user(u32 entry, u32 user_stack);
;   void user_exit_to_kernel(void);
        BITS 32
        global run_user
        global user_exit_to_kernel

        section .bss
saved_kernel_esp:
        resd    1

        section .text
run_user:
        mov     [saved_kernel_esp], esp
        mov     eax, [esp + 4]      ; entry
        mov     edx, [esp + 8]      ; user stack top
        cli

        mov     cx, 0x23            ; user data selector (GDT index 4, RPL3)
        mov     ds, cx
        mov     es, cx
        mov     fs, cx
        mov     gs, cx

        push    ecx                 ; SS = 0x23
        push    edx                 ; user ESP
        pushfd
        or      dword [esp], 0x200  ; enable interrupts in user mode
        push    0x1B                ; CS = user code (index 3, RPL3)
        push    eax                 ; EIP
        iretd

user_resume_point:
        sti
        ret

user_exit_to_kernel:
        cli
        mov     ax, 0x10
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax
        mov     ss, ax
        mov     esp, [saved_kernel_esp]
        jmp     user_resume_point
