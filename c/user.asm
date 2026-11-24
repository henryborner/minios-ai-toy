; Minimal Ring 3 program.  Linked at 0x400000 and copied there by the kernel.
        BITS 32
        ORG 0x400000

start:
        mov     eax, 1              ; syscall: write string
        mov     ebx, msg
        int     0x80

        mov     eax, 2              ; syscall: exit to kernel
        int     0x80

.hang:
        jmp     .hang

msg:
        db 'Hello from Ring 3! User code is running at CPL=3.', 10, 0
