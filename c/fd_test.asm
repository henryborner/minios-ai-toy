; Test open/read/close on TEST.TXT.
        BITS 32
        ORG 0x400000
start:
        mov     eax, 12             ; open
        mov     ebx, fname
        int     0x80
        mov     [fd], eax
        test    eax, eax
        js      fail

        mov     eax, 13             ; read
        mov     ebx, [fd]
        mov     ecx, buf
        mov     edx, 64
        int     0x80
        mov     [n], eax
        test    eax, eax
        jle     fail

        mov     eax, 4              ; write the bytes
        mov     ebx, buf
        mov     ecx, [n]
        int     0x80

        mov     eax, 14             ; close
        mov     ebx, [fd]
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

fname:  db 'TEST.TXT', 0
fd:     dd 0
n:      dd 0
buf:    times 64 db 0
bad:    db 'FD open/read/close FAILED.', 10
bad_end:
