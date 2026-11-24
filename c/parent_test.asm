; Parent/child test: spawn HELLO.BIN, sleep while child runs, wait(status), exit.
        BITS 32
        ORG 0x400000
start:
        mov     eax, 4
        mov     ebx, msg_spawn
        mov     ecx, msg_spawn_end - msg_spawn
        int     0x80

        mov     eax, 8              ; spawn(name)
        mov     ebx, fname
        int     0x80
        mov     [child_pid], eax
        test    eax, eax
        jz      fail

        mov     eax, 10             ; sleep(100 ms)
        mov     ebx, 100
        int     0x80

        mov     eax, 9              ; wait(child pid)
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
msg_spawn:  db 'Parent: spawning HELLO.BIN, then sleeping...', 10
msg_spawn_end:
msg_ok:     db 'Parent: child exited, wait(status) returned.', 10
msg_ok_end:
msg_fail:   db 'Parent: spawn/wait FAILED.', 10
msg_fail_end:
