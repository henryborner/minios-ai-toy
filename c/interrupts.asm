; 32-bit interrupt entry stubs for the C kernel.
        BITS 32
        global irq0_stub
        global irq1_stub
        global irq6_stub
        global irq12_stub
        global syscall_stub
        global isr_default
        extern irq0_handler
        extern irq1_handler
        extern irq6_handler
        extern irq12_handler
        extern syscall_handler

        section .text
irq0_stub:
        pusha
        cld
        mov     edx, esp            ; pointer to EIP/CS/EFLAGS/ESP/SS frame
        push    edx
        call    irq0_handler
        add     esp, 4
        popa
        iretd

irq1_stub:
        pusha
        cld
        call    irq1_handler
        popa
        iretd

irq6_stub:
        pusha
        cld
        call    irq6_handler
        popa
        iretd

irq12_stub:
        pusha
        cld
        call    irq12_handler
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

isr_default:
        cli
        hlt
        jmp     isr_default
