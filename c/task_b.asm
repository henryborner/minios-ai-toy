; Task B: same shape as task A, but a separate address space.
        BITS 32
        ORG 0x400000
start:
        mov     eax, 1
        mov     ebx, b1
        int     0x80
        mov     eax, 3              ; yield
        int     0x80

        mov     eax, 1
        mov     ebx, b2
        int     0x80
        mov     eax, 3              ; yield
        int     0x80

        mov     eax, 1
        mov     ebx, b3
        int     0x80
        mov     eax, 2              ; exit
        int     0x80
.hang:
        jmp     .hang

b1:     db 'Task B: step 1', 10, 0
b2:     db 'Task B: step 2', 10, 0
b3:     db 'Task B: step 3', 10, 0
