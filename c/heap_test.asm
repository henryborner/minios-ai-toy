; Extend the heap to 0x508000, write/read it, then shrink.
        BITS 32
        ORG 0x400000
start:
        mov     eax, 20
        xor     ebx, ebx
        int     0x80                ; current brk

        mov     eax, 20
        mov     ebx, 0x508000
        int     0x80
        cmp     eax, 0x508000
        jne     fail

        mov     byte [0x500800], 'H'
        mov     byte [0x500801], 'I'
        cmp     byte [0x500800], 'H'
        jne     fail
        cmp     byte [0x500801], 'I'
        jne     fail

        mov     eax, 4
        mov     ebx, ok
        mov     ecx, ok_end - ok
        int     0x80

        mov     eax, 20
        mov     ebx, 0x500000
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

ok:     db 'brk: heap mapped, written and read.', 10
ok_end:
bad:    db 'brk heap test FAILED.', 10
bad_end:
