; Spawn ARG.BIN with argv = {"hello", "world", NULL}.
        BITS 32
        ORG 0x400000
start:
        mov     eax, 19             ; spawn_args
        mov     ebx, name
        mov     ecx, argv
        mov     edx, 2
        int     0x80
        test    eax, eax
        jz      fail
        mov     ebx, eax

        mov     eax, 9              ; wait
        int     0x80

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

name:   db 'ARG.BIN', 0
argv:   dd a0, a1, 0
a0:     db 'hello', 0
a1:     db 'world', 0
ok:     db 'Parent: argv child exited.', 10
ok_end:
bad:    db 'argv spawn FAILED.', 10
bad_end:
