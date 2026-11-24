; Child receives argc/argv on its initial stack.
        BITS 32
        ORG 0x400000
start:
        mov     eax, [esp]          ; argc
        cmp     eax, 2
        jb      done

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
done:
        mov     eax, 2
        int     0x80

space:   db ' '
newline: db 10
