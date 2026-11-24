; Long write syscall test: 5000 bytes from a brk-extended heap.
        BITS 32
        ORG 0x400000
start:
        mov     eax, 20
        mov     ebx, 0x502000
        int     0x80
        cmp     eax, 0x502000
        jne     fail

        mov     edi, 0x500000
        mov     ecx, 5000
        mov     al, 'A'
        rep     stosb

        mov     eax, 4
        mov     ebx, 0x500000
        mov     ecx, 5000
        int     0x80
        cmp     eax, 5000
        jne     fail

        mov     eax, 4
        mov     ebx, ok
        mov     ecx, ok_end - ok
        int     0x80
        mov     eax, 2
        int     0x80

fail:
        mov     eax, 4
        mov     ebx, bad
        mov     ecx, bad_end - bad
        int     0x80
        mov     eax, 2
        int     0x80

ok:     db 10, 'long write 5000: PASS.', 10
ok_end:
bad:    db 10, 'long write 5000: FAIL.', 10
bad_end:
