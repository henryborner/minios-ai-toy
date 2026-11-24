; Exercise terminal control chars: tab, CR, backspace, ANSI CSI.
        BITS 32
        ORG 0x400000
start:
        mov     eax, 4
        mov     ebx, msg
        mov     ecx, msg_end - msg
        int     0x80
        mov     eax, 2
        int     0x80

msg:    db 'A', 9, 'B', 13, 'C', 8, 'D', 27, '[31m', 'E', 10
msg_end:
