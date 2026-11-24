; UMP1 large-task argv test.
        BITS 32
        ORG 0x400000

        dd 'UMP1'
        dd entry - 0x400000
        dd image_end - 0x400000
        dd 0
        dd 0x00100000

entry:
        mov     eax, [esp]          ; argc
        cmp     eax, 2
        jb      fail

        mov     eax, 1
        mov     ebx, [esp + 4]      ; argv[0]
        int     0x80
        mov     eax, 4
        mov     ebx, space
        mov     ecx, 1
        int     0x80
        mov     eax, 1
        mov     ebx, [esp + 8]      ; argv[1]
        int     0x80
        mov     eax, 4
        mov     ebx, newline
        mov     ecx, 1
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

space:   db ' '
newline: db 10
bad:     db 'big argv test FAILED.', 10
bad_end:
image_end:
