; Task A: three steps, yield after the first two, then exit.
        BITS 32
        ORG 0x400000
start:
        mov     eax, 1
        mov     ebx, a1
        int     0x80
        mov     eax, 3              ; yield
        int     0x80

        mov     eax, 1
        mov     ebx, a2
        int     0x80
        mov     eax, 3              ; yield
        int     0x80

        mov     eax, 1
        mov     ebx, a3
        int     0x80
        mov     eax, 2              ; exit
        int     0x80
.hang:
        jmp     .hang

a1:     db 'Task A: step 1', 10, 0
a2:     db 'Task A: step 2', 10, 0
a3:     db 'Task A: step 3', 10, 0
