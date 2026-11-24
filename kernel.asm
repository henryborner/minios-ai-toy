; =============================================================================
;  MiniOS kernel - 16-bit x86 real mode, VGA 320x200x256 GUI
;  Loaded at 0000:8000 by the boot sector.  Stack is below the boot sector.
; =============================================================================
        BITS 16
        ORG 0x8000

; --- screen / layout constants ----------------------------------------------
SCREEN_W        equ 320
SCREEN_H        equ 200
TASKBAR_Y       equ 186
CONSOLE_W       equ 296
CONSOLE_H       equ 144
SYSTEM_W        equ 224
SYSTEM_H        equ 96
ABOUT_W         equ 200
ABOUT_H         equ 104
MENU_X          equ 2
MENU_Y          equ 132
MENU_W          equ 108
MENU_H          equ 52

; --- tiny macros -------------------------------------------------------------
%macro RECT 5
        mov     ax, %1
        mov     [g_x], ax
        mov     ax, %2
        mov     [g_y], ax
        mov     ax, %3
        mov     [g_w], ax
        mov     ax, %4
        mov     [g_h], ax
        mov     al, %5
        mov     [g_color], al
        call    g_fill
%endmacro

%macro TEXT 4
        mov     ax, %1
        mov     [g_x], ax
        mov     ax, %2
        mov     [g_y], ax
        mov     ax, %3
        mov     [g_str], ax
        mov     al, %4
        mov     [g_color], al
        call    g_text
%endmacro

%macro HIT 4
        mov     al, 0
        mov     bx, [mouse_x]
        cmp     bx, %1
        jl      %%no
        mov     cx, %1
        add     cx, %3
        cmp     bx, cx
        jge     %%no
        mov     bx, [mouse_y]
        cmp     bx, %2
        jl      %%no
        mov     cx, %2
        add     cx, %4
        cmp     bx, cx
        jge     %%no
        mov     al, 1
%%no:
%endmacro

; =============================================================================
;  Entry point
; =============================================================================
start:
        cli
        xor     ax, ax
        mov     ds, ax
        mov     es, ax
        mov     ss, ax
        mov     sp, 0x7C00
        sti
        mov     [boot_drive], dl
        mov     eax, [0x046C]
        mov     [boot_ticks], eax

        call    vga_init
        call    draw_splash
        call    blit
        call    splash_delay

        call    sysinfo_init
        call    mouse_init
        call    keyboard_irq_install
        call    mouse_irq_install
        call    update_clock
        call    update_uptime
        call    term_init

        mov     dword [last_tick], 0
        mov     byte [dirty], 1

main_loop:
        xor     ax, ax
        mov     ds, ax
        call    keyboard_process

        ; Idle without burning host CPU: check flags with interrupts off,
        ; then sti/hlt.  A timer, key or mouse IRQ wakes us.
        cli
        cmp     byte [dirty], 0
        jne     .full
        cmp     byte [mouse_moved], 0
        jne     .fast
        mov     eax, [0x046C]
        mov     ebx, eax
        sub     ebx, [last_tick]
        cmp     ebx, 3                  ; ~6 Hz idle refresh is plenty
        jae     .full
        sti
        hlt
        jmp     main_loop

.fast:
        ; cursor-only update: restore the old 12x18 cell from the back buffer,
        ; then overlay the arrow at its new position.  No full-screen redraw.
        ; Keep IRQs off for the tiny erase/draw pair so mouse_x/y cannot
        ; change halfway through it.
        cli
        mov     byte [mouse_moved], 0
        call    cursor_erase
        call    draw_cursor
        sti
        jmp     main_loop

.full:
        sti
        mov     eax, [0x046C]
        mov     [last_tick], eax
        mov     byte [dirty], 0
        mov     byte [mouse_moved], 0
        call    redraw
        jmp     main_loop

; =============================================================================
;  Video init
; =============================================================================
vga_init:
        mov     ax, 0x0013
        int     0x10
        xor     ax, ax
        mov     ds, ax
        mov     es, ax
        mov     ax, 0x1012              ; set 16-colour DAC block
        xor     bx, bx
        mov     cx, 16
        mov     dx, palette
        int     0x10

        xor     ax, ax
        mov     ds, ax
        mov     es, ax
        mov     ax, 0x2000
        mov     es, ax
        xor     di, di
        xor     ax, ax
        mov     cx, 32000
        cld
        rep     stosw
        xor     ax, ax
        mov     ds, ax
        mov     es, ax
        ret

; =============================================================================
;  Boot splash
; =============================================================================
draw_splash:
        RECT 0, 0, 320, 200, 1
        RECT 0, 0, 320, 2, 3
        RECT 0, 198, 320, 2, 3

        mov     word [g_scale], 4
        mov     ax, 66
        mov     bx, 52
        mov     si, label_logo
        mov     cl, 8
        call    text_scale_r
        mov     ax, 64
        mov     bx, 50
        mov     si, label_logo
        mov     cl, 15
        call    text_scale_r

        RECT 40, 96, 240, 1, 11
        RECT 40, 98, 240, 1, 2

        TEXT 52, 108, splash_msg1, 15
        TEXT 120, 120, splash_msg2, 11
        TEXT 84, 132, splash_msg3, 7

        RECT 88, 158, 144, 12, 15
        RECT 90, 160, 140, 8, 8
        RECT 90, 160, 96, 8, 10
        TEXT 120, 178, splash_msg4, 7
        ret

splash_delay:
        mov     eax, [0x046C]
        add     eax, 24
.wait:
        mov     ebx, [0x046C]
        cmp     ebx, eax
        jb      .wait
        ret

; =============================================================================
;  Keyboard IRQ1 driver - our own handler, so the BIOS cannot steal mouse bytes
;  with int 16h.  Scancodes are translated to ASCII and queued for the shell.
; =============================================================================
keyboard_irq_install:
        cli
        mov     word [0x0024], keyboard_irq      ; vector 0x09 = IRQ1
        mov     word [0x0026], 0
        in      al, 0x21
        and     al, 0xFD                         ; unmask IRQ1
        out     0x21, al
        sti
        ret

keyboard_irq:
        pusha
        in      al, 0x60
        call    keyboard_scancode
        mov     al, 0x20
        out     0x20, al
        popa
        iret

keyboard_scancode:
        cmp     byte [kbd_ext], 0
        je      .not_ext
        mov     byte [kbd_ext], 0
        ret
.not_ext:
        cmp     al, 0xE0
        jne     .shifts
        mov     byte [kbd_ext], 1
        ret
.shifts:
        cmp     al, 0x2A
        je      .shift_on
        cmp     al, 0x36
        je      .shift_on
        cmp     al, 0xAA
        je      .shift_off
        cmp     al, 0xB6
        je      .shift_off
        test    al, 0x80
        jnz     .ret
        mov     bx, keymap_normal
        cmp     byte [kbd_shift], 0
        je      .lookup
        mov     bx, keymap_shift
.lookup:
        movzx   ax, al
        add     bx, ax
        mov     al, [bx]
        test    al, al
        jz      .ret
        call    kbd_enqueue
.ret:
        ret
.shift_on:
        mov     byte [kbd_shift], 1
        ret
.shift_off:
        mov     byte [kbd_shift], 0
        ret

kbd_enqueue:
        push    ax
        push    bx
        push    di
        mov     bx, [kbd_head]
        mov     di, bx
        inc     bx
        and     bx, 63
        cmp     bx, [kbd_tail]
        je      .full
        mov     [kbd_buf + di], al
        mov     [kbd_head], bx
.full:
        pop     di
        pop     bx
        pop     ax
        ret

; main-loop side: drain the keyboard ring buffer
keyboard_process:
        mov     bx, [kbd_tail]
.loop:
        cmp     bx, [kbd_head]
        je      .done
        mov     al, [kbd_buf + bx]
        inc     bx
        and     bx, 63
        mov     [kbd_tail], bx
        cmp     al, 0xF1
        je      .f1
        cmp     al, 0xF2
        je      .f2
        cmp     al, 0xF3
        je      .f3
        push    bx
        call    console_key
        pop     bx
        jmp     .loop
.f1:
        mov     byte [about_visible], 1
        mov     byte [dirty], 1
        jmp     .loop
.f2:
        mov     byte [system_visible], 1
        mov     byte [dirty], 1
        jmp     .loop
.f3:
        mov     byte [console_visible], 1
        mov     byte [dirty], 1
        jmp     .loop
.done:
        ret

; =============================================================================
;  PS/2 mouse (IRQ12, packets assembled in the interrupt handler)
; =============================================================================
mouse_init:
        cli
        call    kbc_flush
        mov     al, 0xA8                ; enable auxiliary device
        call    kbc_cmd
        mov     al, 0x20                ; read controller config
        call    kbc_cmd
        call    kbc_read
        jc      .done
        and     al, 0xDD                ; IRQ12 off, aux clock on
        mov     bl, al
        mov     al, 0x60
        call    kbc_cmd
        mov     al, bl
        call    kbc_data
        mov     al, 0xD4                ; next byte goes to mouse
        call    kbc_cmd
        mov     al, 0xF6                ; set defaults
        call    kbc_data
        call    kbc_read
        mov     al, 0xD4
        call    kbc_cmd
        mov     al, 0xF4                ; enable data reporting
        call    kbc_data
        call    kbc_read
        call    kbc_flush
.done:
        sti
        ret

kbc_cmd:
        push    ax
        mov     cx, 0xFFFF
.wait:
        in      al, 0x64
        test    al, 2
        jz      .ready
        loop    .wait
.ready:
        pop     ax
        out     0x64, al
        ret

kbc_data:
        push    ax
        mov     cx, 0xFFFF
.wait:
        in      al, 0x64
        test    al, 2
        jz      .ready
        loop    .wait
.ready:
        pop     ax
        out     0x60, al
        ret

kbc_read:
        push    cx
        mov     cx, 0xFFFF
.wait:
        in      al, 0x64
        test    al, 1
        jnz     .ready
        loop    .wait
        stc
        pop     cx
        ret
.ready:
        in      al, 0x60
        clc
        pop     cx
        ret

kbc_flush:
        push    cx
        mov     cx, 64
.loop:
        in      al, 0x64
        test    al, 1
        jz      .done
        in      al, 0x60
        loop    .loop
.done:
        pop     cx
        ret

; -----------------------------------------------------------------------------
; IRQ12 mouse interrupt: install vector, unmask PIC, enable IRQ12 in the KBC
; -----------------------------------------------------------------------------
mouse_irq_install:
        cli
        mov     word [0x01D0], mouse_irq      ; vector 0x74 = IRQ12
        mov     word [0x01D2], 0
        in      al, 0x21
        and     al, 0xFB                      ; unmask cascade IRQ2
        out     0x21, al
        in      al, 0xA1
        and     al, 0xEF                      ; unmask slave IRQ4 (IRQ12)
        out     0xA1, al

        call    kbc_flush
        mov     al, 0x20
        call    kbc_cmd
        call    kbc_read
        jc      .done
        and     al, 0xDD
        or      al, 0x03                      ; enable IRQ1 + IRQ12, aux clock on
        mov     bl, al
        mov     al, 0x60
        call    kbc_cmd
        mov     al, bl
        call    kbc_data
        call    kbc_flush
.done:
        sti
        ret

mouse_irq:
        pusha
        in      al, 0x60
        call    mouse_byte
        mov     al, 0x20
        out     0xA0, al
        out     0x20, al
        popa
        iret

; feed one byte from the mouse into the 3-byte packet assembler
mouse_byte:
        push    ax
        push    bx
        mov     bl, [mouse_cycle]
        test    bl, bl
        jnz     .store
        test    al, 0x08                ; packet always has bit 3 set
        jz      .done
.store:
        movzx   bx, bl
        mov     [mouse_packet + bx], al
        inc     byte [mouse_cycle]
        cmp     byte [mouse_cycle], 3
        jne     .done
        mov     byte [mouse_cycle], 0
        call    mouse_process
.done:
        pop     bx
        pop     ax
        ret

mouse_process:
        pushad
        mov     al, [mouse_packet]
        test    al, 0x08
        jz      .done
        cmp     byte [drag_win], 0
        jne     .need_full
        mov     byte [mouse_moved], 1
        jmp     .position
.need_full:
        mov     byte [dirty], 1
.position:
        mov     al, [mouse_packet + 1]
        cbw
        add     ax, [mouse_x]
        cmp     ax, 0
        jge     .xmin_ok
        xor     ax, ax
.xmin_ok:
        cmp     ax, 307
        jle     .xmax_ok
        mov     ax, 307
.xmax_ok:
        mov     [mouse_x], ax

        mov     al, [mouse_packet + 2]
        cbw
        mov     bx, [mouse_y]
        sub     bx, ax
        cmp     bx, 0
        jge     .ymin_ok
        xor     bx, bx
.ymin_ok:
        cmp     bx, 199
        jle     .ymax_ok
        mov     bx, 199
.ymax_ok:
        mov     [mouse_y], bx

        mov     al, [mouse_packet]
        and     al, 7
        mov     ah, [mouse_prev]
        mov     [mouse_prev], al
        test    al, 1
        jz      .released
        cmp     byte [drag_win], 0
        je      .press_check
        call    drag_move
.press_check:
        test    ah, 1
        jnz     .done
        call    mouse_click
        jmp     .done
.released:
        test    ah, 1
        jz      .done
        mov     byte [drag_win], 0
.done:
        popad
        ret

; =============================================================================
;  System information (CPU / memory)
; =============================================================================
sysinfo_init:
        pushad
        mov     ax, [0x0413]
        mov     [base_kb], ax

        mov     ax, 0xE801
        int     0x15
        jc      .fallback
        movzx   eax, ax
        shr     eax, 10
        movzx   ebx, bx
        shr     ebx, 4
        add     eax, ebx
        mov     ecx, eax
        jmp     .segments
.fallback:
        mov     ah, 0x88
        int     0x15
        jc      .no_ext
        movzx   eax, ax
        shr     eax, 10
        mov     ecx, eax
        jmp     .segments
.no_ext:
        xor     ecx, ecx
.segments:
        xor     ax, ax
        mov     ds, ax
        mov     es, ax
        mov     eax, ecx
        mov     [ext_mb], ax

        xor     eax, eax
        cpuid
        mov     [cpu_vendor], ebx
        mov     [cpu_vendor + 4], edx
        mov     [cpu_vendor + 8], ecx
        mov     byte [cpu_vendor + 12], 0

        cld
        mov     di, cpu_line
        mov     si, label_cpu_prefix
        call    append_str
        mov     si, cpu_vendor
        call    append_str
        mov     byte [di], 0

        mov     di, ram_short
        mov     si, label_ram_prefix
        call    append_str
        mov     ax, [base_kb]
        call    u16_to_dec
        mov     si, label_k_plus
        call    append_str
        mov     ax, [ext_mb]
        call    u16_to_dec
        mov     si, label_m_suffix
        call    append_str
        mov     byte [di], 0

        mov     di, mem_line
        mov     si, label_ram_prefix
        call    append_str
        mov     ax, [base_kb]
        call    u16_to_dec
        mov     si, label_k_base
        call    append_str
        mov     ax, [ext_mb]
        call    u16_to_dec
        mov     si, label_m_ext
        call    append_str
        mov     byte [di], 0
        popad
        ret

; =============================================================================
;  RTC clock and uptime
; =============================================================================
update_clock:
        pushad
        mov     ah, 2
        int     0x1A                    ; CH=h, CL=m, DH=s, BCD
        xor     ax, ax
        mov     ds, ax
        mov     es, ax
        cld
        mov     di, time_hm
        mov     al, ch
        call    put_bcd
        mov     al, ':'
        stosb
        mov     al, cl
        call    put_bcd
        mov     byte [di], 0

        mov     di, time_hms
        mov     al, ch
        call    put_bcd
        mov     al, ':'
        stosb
        mov     al, cl
        call    put_bcd
        mov     al, ':'
        stosb
        mov     al, dh
        call    put_bcd
        mov     byte [di], 0

        mov     ah, 4
        int     0x1A                    ; CH=century, CL=year, DH=month, DL=day
        xor     ax, ax
        mov     ds, ax
        mov     es, ax
        mov     di, date_str
        mov     al, ch
        call    put_bcd
        mov     al, cl
        call    put_bcd
        mov     al, '-'
        stosb
        mov     al, dh
        call    put_bcd
        mov     al, '-'
        stosb
        mov     al, dl
        call    put_bcd
        mov     byte [di], 0
        popad
        ret

update_uptime:
        pushad
        push    es
        xor     ax, ax
        mov     es, ax
        cld
        mov     eax, [0x046C]
        sub     eax, [boot_ticks]
        jnc     .ok
        add     eax, 0x1800B0
.ok:
        xor     edx, edx
        mov     ecx, 18
        div     ecx
        cmp     eax, 359999
        jbe     .secs_ok
        mov     eax, 359999
.secs_ok:
        xor     edx, edx
        mov     ecx, 3600
        div     ecx
        cmp     eax, 99
        jbe     .hours_ok
        mov     eax, 99
.hours_ok:
        mov     [up_h], al
        mov     eax, edx
        xor     edx, edx
        mov     ecx, 60
        div     ecx
        mov     [up_m], al
        mov     [up_s], dl

        mov     di, uptime_str
        mov     al, [up_h]
        call    put_dec2
        mov     al, ':'
        stosb
        mov     al, [up_m]
        call    put_dec2
        mov     al, ':'
        stosb
        mov     al, [up_s]
        call    put_dec2
        mov     byte [di], 0
        pop     es
        popad
        ret

; =============================================================================
;  Includes
; =============================================================================
%include "graphics.asm"
%include "shell.asm"
%include "gui.asm"
%include "data.asm"





