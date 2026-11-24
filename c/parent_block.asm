; Parent that blocks immediately waiting for HELLO.BIN.
        BITS 32
        ORG 0x400000
start:
        mov     eax, 8              ; spawn
        mov     ebx, fname
        int     0x80
        mov     [child_pid], eax
        test    eax, eax
        jz      fail

        mov     eax, 9              ; wait: this must block
        mov     ebx, [child_pid]
        int     0x80

        mov     eax, 4
        mov     ebx, msg_ok
        mov     ecx, msg_ok_end - msg_ok
        int     0x80
        mov     eax, 2
        int     0x80

fail:
        mov     eax, 4
        mov     ebx, msg_fail
        mov     ecx, msg_fail_end - msg_fail
        int     0x80
        mov     eax, 2
        int     0x80

fname:      db 'HELLO.BIN', 0
child_pid:  dd 0
msg_ok:     db 'Parent: blocking wait woke after child exit.', 10
msg_ok_end:
msg_fail:   db 'Parent-block: spawn FAILED.', 10
msg_fail_end:
