; UMP1 payload larger than the old 256 KB limit.
        BITS 32
        ORG 0x400000

        dd 'UMP1'
        dd entry - 0x400000
        dd image_end - 0x400000
        dd 0
        dd 0x00100000

entry:
        mov     eax, 4
        mov     ebx, msg
        mov     ecx, msg_end - msg
        int     0x80
        mov     eax, 2
        int     0x80

msg:    db 'big UMP1 image > 256 KB: PASS.', 10
msg_end:
        times 300000 - ($ - $$) db 0x90
image_end:
