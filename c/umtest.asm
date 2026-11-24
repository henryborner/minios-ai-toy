; Ring 3 user-mode layout test for MicroPython bring-up.
; Linked at 0x400000; image has an UMP1 header. Heap at 0x500000, stack top 0x490000.
        BITS 32
        ORG 0x400000

        dd 'UMP1'
        dd entry - 0x400000
        dd image_end - 0x400000
        dd 0                        ; BSS size
        dd 0x00100000               ; requested heap

entry:
        ; syscall 4: write(buf, len)
        mov     eax, 4
        mov     ebx, msg
        mov     ecx, msg_end - msg
        int     0x80

        ; touch the 1 MB heap at 0x500000
        mov     byte [0x500000], 'A'
        cmp     byte [0x500000], 'A'
        jne     fail

        ; syscall 6: ticks_ms -> EAX
        mov     eax, 6
        int     0x80
        test    eax, eax
        jz      fail

        ; syscall 7: read HELLO.BIN into the user heap
        mov     eax, 7
        mov     ebx, fname
        mov     ecx, 0x500000
        mov     edx, 0x1000
        int     0x80
        test    eax, eax
        jz      fail

        mov     eax, 4
        mov     ebx, fileok
        mov     ecx, fileok_end - fileok
        int     0x80

        ; syscall 5: blocking console input
        mov     eax, 4
        mov     ebx, keyprompt
        mov     ecx, keyprompt_end - keyprompt
        int     0x80
        mov     eax, 5
        int     0x80
        mov     [keybuf], al

        mov     eax, 4
        mov     ebx, keybuf
        mov     ecx, 1
        int     0x80
        mov     eax, 4
        mov     ebx, newline
        mov     ecx, 1
        int     0x80

        ; syscall 4 again through the success string
        mov     eax, 4
        mov     ebx, okmsg
        mov     ecx, okend - okmsg
        int     0x80
        jmp     done

fail:
        mov     eax, 4
        mov     ebx, badmsg
        mov     ecx, badend - badmsg
        int     0x80

done:
        mov     eax, 2              ; exit
        int     0x80
.hang:
        jmp     .hang

msg:     db 'Ring3 large module loaded: multi-page code + 1MB heap.', 10
msg_end:
fname:  db 'HELLO.BIN', 0
fileok: db 'read_file syscall loaded HELLO.BIN into user heap.', 10
fileok_end:
keyprompt: db 'Type one key for blocking getchar: ', 0
keyprompt_end:
newline: db 10
keybuf: db 0
okmsg:  db 'Heap, read_file, getchar and ticks_ms OK.', 10
okend:
badmsg: db 'User-mode syscall/heap test FAILED.', 10
badend:

        times 12288 db 0x90         ; force image beyond one 4 KB page
image_end:
