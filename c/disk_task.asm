; Disk-loaded Ring 3 task for the runtime MiniFS.
        BITS 32
        ORG 0x400000
start:
        mov     eax, 1
        mov     ebx, m1
        int     0x80
        mov     ecx, 20000000
.d1:    loop    .d1

        mov     eax, 1
        mov     ebx, m2
        int     0x80
        mov     ecx, 20000000
.d2:    loop    .d2

        mov     eax, 1
        mov     ebx, m3
        int     0x80
        mov     ecx, 20000000
.d3:    loop    .d3

        mov     eax, 2
        int     0x80
.hang:
        jmp     .hang

m1:     db 'Disk task: loaded from floppy!', 10, 0
m2:     db 'Disk task: second sector read OK.', 10, 0
m3:     db 'Disk task: exit syscall.', 10, 0
