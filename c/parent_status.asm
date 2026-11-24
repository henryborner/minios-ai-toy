; Spawn CHILD5.BIN, wait, and verify status == 5.
        BITS 32
        ORG 0x400000
start:
        mov     eax, 8
        mov     ebx, name
        int     0x80
        test    eax, eax
        jz      fail
        mov     ebx, eax

        mov     eax, 9
        int     0x80
        cmp     eax, 5
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

name:   db 'CHILD5.BIN', 0
ok:     db 'exit(5) -> wait() returned 5: PASS.', 10
ok_end:
bad:    db 'exit status test FAILED.', 10
bad_end:
