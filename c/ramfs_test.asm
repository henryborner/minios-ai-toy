; RAMFS create/write/seek/read/close test.
        BITS 32
        ORG 0x400000
start:
        mov     eax, 16             ; open_ex(name, O_CREAT|O_TRUNC|O_WRITE)
        mov     ebx, fname
        mov     ecx, 7
        int     0x80
        mov     [fd], eax
        test    eax, eax
        js      fail

        mov     eax, 17             ; write
        mov     ebx, [fd]
        mov     ecx, text
        mov     edx, text_end - text
        int     0x80
        test    eax, eax
        js      fail

        mov     eax, 15             ; seek(0)
        mov     ebx, [fd]
        xor     ecx, ecx
        xor     edx, edx
        int     0x80

        mov     eax, 13             ; read back
        mov     ebx, [fd]
        mov     ecx, buf
        mov     edx, 64
        int     0x80
        mov     [n], eax
        test    eax, eax
        jle     fail

        mov     eax, 4              ; print contents
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

fname:  db 'TMP.TXT', 0
fd:     dd 0
n:      dd 0
buf:    times 64 db 0
text:   db 'RAMFS write through FD syscalls works.', 10
text_end:
bad:    db 'RAMFS test FAILED.', 10
bad_end:
