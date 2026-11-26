; C3 native heap test: grow brk to 0x04500000 (64 MiB heap window),
; write across page-table boundaries, report on COM1, exit.
        BITS 32
        ORG 0x00400000

_start:
        mov     eax, 20
        mov     ebx, 0x04500000
        int     0x80
        cmp     eax, 0xFFFFFFFF
        je      .fail

        mov     dword [0x00600000], 0x60000001
        mov     dword [0x00900000], 0x90000002
        mov     dword [0x02000000], 0x20000003
        mov     dword [0x04400000], 0x44000004

        cmp     dword [0x00600000], 0x60000001
        jne     .fail
        cmp     dword [0x00900000], 0x90000002
        jne     .fail
        cmp     dword [0x02000000], 0x20000003
        jne     .fail
        cmp     dword [0x04400000], 0x44000004
        jne     .fail

        mov     eax, 28
        mov     ebx, 1
        mov     ecx, msg_ok
        mov     edx, msg_ok_len
        int     0x80

        mov     eax, 11
        xor     ebx, ebx
        int     0x80

.fail:
        mov     eax, 28
        mov     ebx, 1
        mov     ecx, msg_fail
        mov     edx, msg_fail_len
        int     0x80
        mov     eax, 11
        mov     ebx, 1
        int     0x80

msg_ok:         db 'BRK64 OK', 10
msg_ok_len      equ $ - msg_ok
msg_fail:       db 'BRK64 FAIL', 10
msg_fail_len    equ $ - msg_fail

        times 4096 - ($ - $$) db 0
