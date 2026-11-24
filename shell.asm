; =============================================================================
;  MiniOS text console + tiny command shell
; =============================================================================

term_init:
        call    term_clear
        mov     si, banner
        call    term_print
        mov     byte [dirty], 1
        ret

term_clear:
        pushad
        push    es
        mov     ax, ds
        mov     es, ax
        mov     di, term_buf
        mov     cx, 14 * 40
        xor     al, al
        cld
        rep     stosb
        mov     word [term_n], 1
        mov     word [term_cur], 0
        pop     es
        popad
        ret

; DI = pointer to the current terminal line
term_cur_ptr:
        mov     dx, [term_cur]
        imul    dx, dx, 40
        add     dx, term_buf
        mov     di, dx
        ret

; AX = current line length
term_len:
        call    term_cur_ptr
        xor     ax, ax
.loop:
        cmp     byte [di], 0
        je      .done
        inc     di
        inc     ax
        cmp     ax, 39
        jb      .loop
.done:
        ret

term_newline:
        pushad
        push    es
        mov     ax, ds
        mov     es, ax
        cld
        mov     ax, [term_n]
        cmp     ax, 14
        jb      .append
        mov     si, term_buf + 40
        mov     di, term_buf
        mov     cx, 13 * 40 / 2
        rep     movsw
        mov     di, term_buf + 13 * 40
        mov     cx, 40
        xor     al, al
        rep     stosb
        mov     word [term_cur], 13
        jmp     .out
.append:
        mov     bx, ax
        imul    bx, bx, 40
        mov     di, term_buf
        add     di, bx
        mov     cx, 40
        xor     al, al
        rep     stosb
        mov     ax, [term_n]
        mov     [term_cur], ax
        inc     word [term_n]
.out:
        pop     es
        popad
        ret

term_putc:
        push    ax
        cmp     al, 10
        je      .nl
        cmp     al, 13
        je      .nl
        pop     ax
        push    ax
        call    term_cur_ptr
        mov     cx, 35
.scan:
        cmp     byte [di], 0
        je      .store
        inc     di
        dec     cx
        jnz     .scan
        pop     ax
        push    ax
        call    term_newline
        pop     ax
        jmp     term_putc
.store:
        pop     ax
        mov     [di], al
        mov     byte [di + 1], 0
        ret
.nl:
        pop     ax
        jmp     term_newline

term_print:
        push    si
.loop:
        lodsb
        test    al, al
        jz      .done
        push    si
        call    term_putc
        pop     si
        jmp     .loop
.done:
        pop     si
        ret

; -----------------------------------------------------------------------------
; console_key - handle one ASCII key from the BIOS keyboard
; -----------------------------------------------------------------------------
console_key:
        cmp     al, 27                  ; ESC closes About / menu
        je      .esc
        cmp     byte [about_visible], 0
        je      .not_about_key
        cmp     al, 13                  ; Enter = OK in the About dialog
        jne     .done
        mov     byte [about_visible], 0
        mov     byte [dirty], 1
        ret
.not_about_key:
        cmp     byte [console_visible], 0
        je      .done
        cmp     al, 13
        je      .enter
        cmp     al, 8
        je      .back
        cmp     al, 127
        je      .back
        cmp     al, 32
        jb      .done
        cmp     al, 126
        ja      .done
        call    term_cur_ptr
        mov     cx, 35
.scan:
        cmp     byte [di], 0
        je      .store
        inc     di
        dec     cx
        jnz     .scan
        ret
.store:
        mov     [di], al
        mov     byte [di + 1], 0
        mov     byte [dirty], 1
        ret
.back:
        call    term_len
        cmp     ax, 5
        jbe     .done
        call    term_cur_ptr
        add     di, ax
        dec     di
        mov     byte [di], 0
        mov     byte [dirty], 1
        ret
.enter:
        call    console_submit
        ret
.esc:
        cmp     byte [about_visible], 0
        je      .esc_system
        mov     byte [about_visible], 0
        jmp     .esc_done
.esc_system:
        cmp     byte [system_visible], 0
        je      .esc_menu
        mov     byte [system_visible], 0
        jmp     .esc_done
.esc_menu:
        mov     byte [menu_open], 0
.esc_done:
        mov     byte [dirty], 1
.done:
        ret

; -----------------------------------------------------------------------------
; console_submit - execute the line after "C:\> "
; -----------------------------------------------------------------------------
console_submit:
        pushad
        call    term_cur_ptr
        add     di, 5
        mov     si, di
        mov     di, cmd_buf
        mov     cx, 38
.copy:
        lodsb
        test    al, al
        jz      .copied
        mov     [di], al
        inc     di
        dec     cx
        jnz     .copy
.copied:
        mov     byte [di], 0
        call    term_newline
        cmp     byte [cmd_buf], 0
        je      .prompt
        call    run_command
.prompt:
        mov     si, prompt
        call    term_print
        mov     byte [dirty], 1
        popad
        ret

; -----------------------------------------------------------------------------
; run_command - dispatch the command in cmd_buf
; -----------------------------------------------------------------------------
run_command:
        mov     si, cmd_buf
        mov     di, cmd_help
        call    cmd_match
        test    al, al
        jnz     .help

        mov     si, cmd_buf
        mov     di, cmd_ver
        call    cmd_match
        test    al, al
        jnz     .ver

        mov     si, cmd_buf
        mov     di, cmd_time
        call    cmd_match
        test    al, al
        jnz     .time

        mov     si, cmd_buf
        mov     di, cmd_date
        call    cmd_match
        test    al, al
        jnz     .date

        mov     si, cmd_buf
        mov     di, cmd_mem
        call    cmd_match
        test    al, al
        jnz     .mem

        mov     si, cmd_buf
        mov     di, cmd_cpu
        call    cmd_match
        test    al, al
        jnz     .cpu

        mov     si, cmd_buf
        mov     di, cmd_dir
        call    cmd_match
        test    al, al
        jnz     .dir

        mov     si, cmd_buf
        mov     di, cmd_cls
        call    cmd_match
        test    al, al
        jnz     .cls

        mov     si, cmd_buf
        mov     di, cmd_sysinfo
        call    cmd_match
        test    al, al
        jnz     .sysinfo

        mov     si, cmd_buf
        mov     di, cmd_about
        call    cmd_match
        test    al, al
        jnz     .about

        mov     si, cmd_buf
        mov     di, cmd_reboot
        call    cmd_match
        test    al, al
        jnz     .reboot

        mov     si, msg_bad
        call    term_print
        call    term_newline
        ret

.help:
        mov     si, help_msg1
        call    term_print
        call    term_newline
        mov     si, help_msg2
        call    term_print
        call    term_newline
        mov     si, help_msg3
        call    term_print
        call    term_newline
        ret
.ver:
        mov     si, ver_msg1
        call    term_print
        call    term_newline
        mov     si, ver_msg2
        call    term_print
        call    term_newline
        ret
.time:
        call    update_clock
        mov     si, label_time
        call    term_print
        mov     si, time_hms
        call    term_print
        call    term_newline
        ret
.date:
        call    update_clock
        mov     si, label_date
        call    term_print
        mov     si, date_str
        call    term_print
        call    term_newline
        ret
.mem:
        mov     si, mem_line
        call    term_print
        call    term_newline
        ret
.cpu:
        mov     si, cpu_line
        call    term_print
        call    term_newline
        ret
.dir:
        mov     si, dir1
        call    term_print
        call    term_newline
        mov     si, dir2
        call    term_print
        call    term_newline
        mov     si, dir3
        call    term_print
        call    term_newline
        mov     si, dir4
        call    term_print
        call    term_newline
        mov     si, dir5
        call    term_print
        call    term_newline
        ret
.cls:
        call    term_clear
        ret
.sysinfo:
        mov     byte [system_visible], 1
        mov     byte [dirty], 1
        mov     si, msg_sys_open
        call    term_print
        call    term_newline
        ret
.about:
        mov     byte [about_visible], 1
        mov     byte [dirty], 1
        mov     si, msg_about_open
        call    term_print
        call    term_newline
        ret
.reboot:
        mov     si, msg_reboot
        call    term_print
        call    term_newline
        call    redraw
        call    splash_delay
        mov     al, 0xFE
        out     0x64, al
        cli
        hlt
        jmp     $

; -----------------------------------------------------------------------------
; cmd_match - case-insensitive command match, returns AL=1/0
;   SI = typed text, DI = command
; -----------------------------------------------------------------------------
cmd_match:
        push    si
        push    di
.loop:
        mov     al, [di]
        test    al, al
        jz      .end
        mov     ah, [si]
        cmp     al, 'A'
        jb      .l1
        cmp     al, 'Z'
        ja      .l1
        add     al, 32
.l1:
        cmp     ah, 'A'
        jb      .l2
        cmp     ah, 'Z'
        ja      .l2
        add     ah, 32
.l2:
        cmp     al, ah
        jne     .no
        inc     si
        inc     di
        jmp     .loop
.end:
        mov     al, [si]
        test    al, al
        jz      .yes
        cmp     al, ' '
        je      .yes
        jmp     .no
.yes:
        mov     al, 1
        jmp     .out
.no:
        xor     al, al
.out:
        pop     di
        pop     si
        ret

; -----------------------------------------------------------------------------
; small formatting/string helpers
; -----------------------------------------------------------------------------
; AX = unsigned value, DI = destination; writes decimal, advances DI
u16_to_dec:
        push    ax
        push    bx
        push    cx
        push    dx
        mov     bx, 10
        xor     cx, cx
.div:
        xor     dx, dx
        div     bx
        push    dx
        inc     cx
        test    ax, ax
        jnz     .div
.out:
        pop     dx
        add     dl, '0'
        mov     [di], dl
        inc     di
        loop    .out
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

; copy ASCIIZ at SI to DI
append_str:
        lodsb
        test    al, al
        jz      .done
        mov     [di], al
        inc     di
        jmp     append_str
.done:
        ret

; AL = packed BCD byte, DI = destination; writes 2 ASCII digits
put_bcd:
        push    ax
        mov     ah, al
        shr     al, 4
        add     al, '0'
        mov     [di], al
        inc     di
        mov     al, ah
        and     al, 0x0F
        add     al, '0'
        mov     [di], al
        inc     di
        pop     ax
        ret

; AL = binary 0..99, DI = destination; writes 2 ASCII digits
put_dec2:
        push    ax
        aam
        add     ax, 0x3030
        mov     [di], ah
        mov     [di + 1], al
        add     di, 2
        pop     ax
        ret
