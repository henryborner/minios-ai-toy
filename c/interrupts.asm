; 32-bit interrupt entry stubs for the C kernel.
        BITS 32
        global irq0_stub
        global irq1_stub
        global irq6_stub
        global irq12_stub
        global syscall_stub
        global isr_default
        global exc_stubs
        global exc0_stub
        global exc6_stub
        global exc13_stub
        global exc14_stub
        extern irq0_handler
        extern irq1_handler
        extern irq6_handler
        extern irq12_handler
        extern syscall_handler
        extern exception_handler

        section .text
irq0_stub:
        pusha
        cld
        mov     edx, esp            ; frame pointer before segment saves
        push    ds
        push    es
        mov     ax, 0x10
        mov     ds, ax
        mov     es, ax
        push    edx
        call    irq0_handler
        add     esp, 4
        pop     es
        pop     ds
        popa
        iretd

irq1_stub:
        pusha
        cld
        push    ds
        push    es
        mov     ax, 0x10
        mov     ds, ax
        mov     es, ax
        call    irq1_handler
        pop     es
        pop     ds
        popa
        iretd

irq6_stub:
        pusha
        cld
        push    ds
        push    es
        mov     ax, 0x10
        mov     ds, ax
        mov     es, ax
        call    irq6_handler
        pop     es
        pop     ds
        popa
        iretd

irq12_stub:
        pusha
        cld
        push    ds
        push    es
        mov     ax, 0x10
        mov     ds, ax
        mov     es, ax
        call    irq12_handler
        pop     es
        pop     ds
        popa
        iretd

syscall_stub:
        pusha
        cld
        mov     ax, 0x10
        mov     ds, ax
        mov     es, ax
        mov     edx, esp            ; pointer to the saved task frame

        mov     eax, [esp + 28]     ; saved EAX = syscall number
        mov     ebx, [esp + 16]     ; saved EBX = argument
        mov     ecx, [esp + 36]     ; saved CS from the privilege transition
        push    edx                 ; frame pointer
        push    ecx                 ; CS
        push    ebx                 ; argument
        push    eax                 ; syscall number
        call    syscall_handler
        add     esp, 16

        mov     ax, 0x23            ; back to user data segments
        mov     ds, ax
        mov     es, ax
        popa
        iretd

%macro EXC_NOERR 1
        global exc%1_stub
exc%1_stub:
        push    dword 0             ; dummy error code
        push    dword %1            ; vector
        jmp     exc_common
%endmacro

%macro EXC_ERR 1
        global exc%1_stub
exc%1_stub:
        push    dword %1            ; CPU already pushed error code
        jmp     exc_common
%endmacro

        EXC_NOERR 0
        EXC_NOERR 1
        EXC_NOERR 2
        EXC_NOERR 3
        EXC_NOERR 4
        EXC_NOERR 5
        EXC_NOERR 6
        EXC_NOERR 7
        EXC_ERR   8
        EXC_NOERR 9
        EXC_ERR   10
        EXC_ERR   11
        EXC_ERR   12
        EXC_ERR   13
        EXC_ERR   14
        EXC_NOERR 15
        EXC_NOERR 16
        EXC_ERR   17
        EXC_NOERR 18
        EXC_NOERR 19
        EXC_NOERR 20
        EXC_NOERR 21
        EXC_NOERR 22
        EXC_NOERR 23
        EXC_NOERR 24
        EXC_NOERR 25
        EXC_NOERR 26
        EXC_NOERR 27
        EXC_NOERR 28
        EXC_NOERR 29
        EXC_ERR   30
        EXC_NOERR 31

        section .data
        align 4
exc_stubs:
        dd exc0_stub,  exc1_stub,  exc2_stub,  exc3_stub
        dd exc4_stub,  exc5_stub,  exc6_stub,  exc7_stub
        dd exc8_stub,  exc9_stub,  exc10_stub, exc11_stub
        dd exc12_stub, exc13_stub, exc14_stub, exc15_stub
        dd exc16_stub, exc17_stub, exc18_stub, exc19_stub
        dd exc20_stub, exc21_stub, exc22_stub, exc23_stub
        dd exc24_stub, exc25_stub, exc26_stub, exc27_stub
        dd exc28_stub, exc29_stub, exc30_stub, exc31_stub

        section .text
exc_common:
        pusha
        cld
        mov     ax, 0x10
        mov     ds, ax
        mov     es, ax
        mov     eax, esp
        push    eax
        call    exception_handler
        add     esp, 4
        cli
        hlt
        jmp     $

isr_default:
        cli
        hlt
        jmp     isr_default
