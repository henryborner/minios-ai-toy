; Child exits with status 5.
        BITS 32
        ORG 0x400000
start:
        mov     eax, 11
        mov     ebx, 5
        int     0x80
.hang:
        jmp     .hang
