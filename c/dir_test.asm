; stat/readdir test: RAMFS overrides MiniFS TEST.TXT.
        BITS 32
        ORG 0x400000
start:
        mov     eax, 16             ; open_ex(TEST.TXT, create|trunc|write)
        mov     ebx, test_name
        mov     ecx, 7
        int     0x80
        mov     [fd], eax
        test    eax, eax
        js      fail

        mov     eax, 17             ; write 3 bytes
        mov     ebx, [fd]
        mov     ecx, ram_data
        mov     edx, 3
        int     0x80
        cmp     eax, 3
        jne     fail

        mov     eax, 14
        mov     ebx, [fd]
        int     0x80

        mov     eax, 21             ; stat TEST.TXT
        mov     ebx, test_name
        mov     ecx, statbuf
        int     0x80
        test    eax, eax
        jnz     fail
        cmp     dword [statbuf], 3
        jne     fail
        cmp     dword [statbuf + 4], 2
        jne     fail

        mov     eax, 21             ; stat FD.BIN (MiniFS)
        mov     ebx, fd_name
        mov     ecx, statbuf
        int     0x80
        test    eax, eax
        jnz     fail
        cmp     dword [statbuf + 4], 1
        jne     fail
        cmp     dword [statbuf], 0
        jbe     fail

        xor     ebx, ebx            ; readdir index
.enum:
        mov     eax, 22
        mov     ecx, dirent
        int     0x80
        test    eax, eax
        jz      .enum_done

        cmp     dword [dirent], 0x54534554  ; "TEST"
        jne     .not_test
        inc     dword [test_count]
        cmp     dword [dirent + 20], 2
        jne     fail
.not_test:
        cmp     dword [dirent], 0x422E4446  ; "FD.B"
        jne     .next
        cmp     dword [dirent + 20], 1
        jne     fail
        mov     byte [seen_fd], 1
.next:
        inc     ebx
        cmp     ebx, 32
        jb      .enum
        jmp     fail

.enum_done:
        cmp     dword [test_count], 1
        jne     fail
        cmp     byte [seen_fd], 1
        jne     fail

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

test_name:  db 'TEST.TXT', 0
fd_name:    db 'FD.BIN', 0
ram_data:   db 'ram'
fd:         dd 0
test_count: dd 0
seen_fd:    db 0
statbuf:    times 8 db 0
dirent:     times 24 db 0
ok:         db 'stat/readdir: PASS (RAMFS overrides MiniFS).', 10
ok_end:
bad:        db 'stat/readdir: FAIL.', 10
bad_end:

