; Enter Ring 3 via IRET.
;   void enter_user(u32 entry, u32 user_stack);
        BITS 32
        global enter_user
        section .text
enter_user:
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
