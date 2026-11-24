; 32-bit interrupt entry stubs for the C kernel.
        BITS 32
        global irq0_stub
        global irq1_stub
        global isr_default
        extern irq0_handler
        extern irq1_handler

        section .text
irq0_stub:
        pusha
        cld
        call    irq0_handler
        popa
        iretd

irq1_stub:
        pusha
        cld
        call    irq1_handler
        popa
        iretd

isr_default:
        cli
        hlt
        jmp     isr_default
