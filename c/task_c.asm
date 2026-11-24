; Task C: third preemptive task, loaded from the program pack.
        BITS 32
        ORG 0x400000
start:
        mov     eax, 1
        mov     ebx, c1
        int     0x80
        mov     ecx, 20000000
.d1:    loop    .d1

        mov     eax, 1
        mov     ebx, c2
        int     0x80
        mov     ecx, 20000000
.d2:    loop    .d2

        mov     eax, 1
        mov     ebx, c3
        int     0x80
        mov     ecx, 20000000
.d3:    loop    .d3

        mov     eax, 1
        mov     ebx, c4
        int     0x80
        mov     ecx, 20000000
.d4:    loop    .d4

        mov     eax, 2
        int     0x80
.hang:
        jmp     .hang

c1:     db 'Task C: step 1', 10, 0
c2:     db 'Task C: step 2', 10, 0
c3:     db 'Task C: step 3', 10, 0
c4:     db 'Task C: step 4', 10, 0
