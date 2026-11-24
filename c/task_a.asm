; Task A: no yield; the PIT must preempt it during the busy delays.
        BITS 32
        ORG 0x400000
start:
        mov     eax, 1
        mov     ebx, a1
        int     0x80
        mov     ecx, 20000000
.d1:    loop    .d1

        mov     eax, 1
        mov     ebx, a2
        int     0x80
        mov     ecx, 20000000
.d2:    loop    .d2

        mov     eax, 1
        mov     ebx, a3
        int     0x80
        mov     ecx, 20000000
.d3:    loop    .d3

        mov     eax, 1
        mov     ebx, a4
        int     0x80
        mov     ecx, 20000000
.d4:    loop    .d4

        mov     eax, 2
        int     0x80
.hang:
        jmp     .hang

a1:     db 'Task A: step 1', 10, 0
a2:     db 'Task A: step 2', 10, 0
a3:     db 'Task A: step 3', 10, 0
a4:     db 'Task A: step 4', 10, 0

