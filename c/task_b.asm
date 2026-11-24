; Task B: no yield; the PIT must preempt it during the busy delays.
        BITS 32
        ORG 0x400000
start:
        mov     eax, 1
        mov     ebx, b1
        int     0x80
        mov     ecx, 20000000
.d1:    loop    .d1

        mov     eax, 1
        mov     ebx, b2
        int     0x80
        mov     ecx, 20000000
.d2:    loop    .d2

        mov     eax, 1
        mov     ebx, b3
        int     0x80
        mov     ecx, 20000000
.d3:    loop    .d3

        mov     eax, 1
        mov     ebx, b4
        int     0x80
        mov     ecx, 20000000
.d4:    loop    .d4

        mov     eax, 2
        int     0x80
.hang:
        jmp     .hang

b1:     db 'Task B: step 1', 10, 0
b2:     db 'Task B: step 2', 10, 0
b3:     db 'Task B: step 3', 10, 0
b4:     db 'Task B: step 4', 10, 0

