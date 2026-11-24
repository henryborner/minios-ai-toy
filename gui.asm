; =============================================================================
;  MiniOS window manager, taskbar, start menu, mouse hit testing
; =============================================================================

; -----------------------------------------------------------------------------
; g_raise - 3D raised button
; AX=x BX=y CX=w DX=h
; -----------------------------------------------------------------------------
g_raise:
        mov     [rx], ax
        mov     [ry], bx
        mov     [rw], cx
        mov     [rh], dx

        mov     ax, [rx]
        mov     bx, [ry]
        mov     cx, [rw]
        mov     dx, [rh]
        mov     si, 0
        call    fill_r

        mov     ax, [rx]
        inc     ax
        mov     bx, [ry]
        inc     bx
        mov     cx, [rw]
        sub     cx, 2
        mov     dx, [rh]
        sub     dx, 2
        mov     si, 7
        call    fill_r

        mov     ax, [rx]
        inc     ax
        mov     bx, [ry]
        inc     bx
        mov     cx, [rw]
        sub     cx, 2
        mov     dx, 1
        mov     si, 15
        call    fill_r

        mov     ax, [rx]
        inc     ax
        mov     bx, [ry]
        inc     bx
        mov     cx, 1
        mov     dx, [rh]
        sub     dx, 2
        mov     si, 15
        call    fill_r

        mov     ax, [rx]
        inc     ax
        mov     bx, [ry]
        add     bx, [rh]
        dec     bx
        mov     cx, [rw]
        sub     cx, 2
        mov     dx, 1
        mov     si, 8
        call    fill_r

        mov     ax, [rx]
        add     ax, [rw]
        dec     ax
        mov     bx, [ry]
        inc     bx
        mov     cx, 1
        mov     dx, [rh]
        sub     dx, 2
        mov     si, 8
        call    fill_r
        ret

; -----------------------------------------------------------------------------
; draw_frame - window chrome + white content area
; wf_x/wf_y/wf_w/wf_h/wf_title
; -----------------------------------------------------------------------------
draw_frame:
        mov     ax, [wf_x]
        add     ax, 3
        mov     bx, [wf_y]
        add     bx, 3
        mov     cx, [wf_w]
        mov     dx, [wf_h]
        mov     si, 8
        call    fill_r

        mov     ax, [wf_x]
        mov     bx, [wf_y]
        mov     cx, [wf_w]
        mov     dx, [wf_h]
        mov     si, 0
        call    fill_r

        mov     ax, [wf_x]
        inc     ax
        mov     bx, [wf_y]
        inc     bx
        mov     cx, [wf_w]
        sub     cx, 2
        mov     dx, [wf_h]
        sub     dx, 2
        mov     si, 7
        call    fill_r

        mov     ax, [wf_x]
        inc     ax
        mov     bx, [wf_y]
        inc     bx
        mov     cx, [wf_w]
        sub     cx, 2
        mov     dx, 10
        mov     si, 1
        call    fill_r

        mov     ax, [wf_x]
        add     ax, 4
        mov     bx, [wf_y]
        add     bx, 2
        mov     si, [wf_title]
        mov     cl, 15
        call    text_r

        mov     ax, [wf_x]
        add     ax, [wf_w]
        sub     ax, 14
        mov     bx, [wf_y]
        add     bx, 2
        mov     cx, 12
        mov     dx, 8
        call    g_raise

        mov     ax, [wf_x]
        add     ax, [wf_w]
        sub     ax, 12
        mov     bx, [wf_y]
        add     bx, 2
        mov     si, label_close
        mov     cl, 0
        call    text_r

        mov     ax, [wf_x]
        inc     ax
        mov     bx, [wf_y]
        add     bx, 11
        mov     cx, [wf_w]
        sub     cx, 2
        mov     dx, [wf_h]
        sub     dx, 12
        mov     si, 15
        call    fill_r
        ret

; -----------------------------------------------------------------------------
; desktop / taskbar / start menu
; -----------------------------------------------------------------------------
draw_desktop:
        RECT 0, 0, 320, TASKBAR_Y, 3
        TEXT 8, 5, label_desktop, 15
        TEXT 206, 5, label_size, 11
        RECT 0, 17, 320, 1, 2
        ret

draw_taskbar:
        RECT 0, TASKBAR_Y, 320, 14, 7
        RECT 0, TASKBAR_Y, 320, 1, 15
        RECT 0, TASKBAR_Y + 1, 320, 1, 8

        mov     ax, 2
        mov     bx, 188
        mov     cx, 50
        mov     dx, 10
        call    g_raise
        TEXT 6, 189, label_start, 0

        mov     ax, 56
        mov     bx, 188
        mov     cx, 62
        mov     dx, 10
        call    g_raise
        TEXT 59, 189, label_console, 0

        mov     ax, 122
        mov     bx, 188
        mov     cx, 56
        mov     dx, 10
        call    g_raise
        TEXT 124, 189, label_system, 0

        TEXT 184, 189, label_brand, 1

        RECT 264, 188, 52, 10, 0
        RECT 265, 189, 50, 8, 15
        TEXT 274, 189, time_hm, 0
        ret

draw_menu:
        cmp     byte [menu_open], 0
        je      .ret
        RECT MENU_X + 3, MENU_Y + 3, MENU_W, MENU_H, 8
        RECT MENU_X, MENU_Y, MENU_W, MENU_H, 0
        RECT MENU_X + 1, MENU_Y + 1, MENU_W - 2, MENU_H - 2, 7

        mov     word [menu_i], 0
.loop:
        mov     ax, [menu_i]
        imul    ax, ax, 12
        add     ax, MENU_Y + 2
        mov     [menu_iy], ax

        HIT MENU_X + 2, [menu_iy], MENU_W - 4, 12
        mov     [menu_hover], al
        test    al, al
        jz      .no_fill
        mov     ax, MENU_X + 2
        mov     bx, [menu_iy]
        mov     cx, MENU_W - 4
        mov     dx, 12
        mov     si, 1
        call    fill_r
.no_fill:
        mov     bx, [menu_i]
        shl     bx, 1
        mov     si, [menu_items + bx]
        mov     ax, MENU_X + 8
        mov     bx, [menu_iy]
        add     bx, 2
        mov     cl, 0
        cmp     byte [menu_hover], 0
        je      .txt
        mov     cl, 15
.txt:
        call    text_r
        inc     word [menu_i]
        cmp     word [menu_i], 4
        jb      .loop
.ret:
        ret

; -----------------------------------------------------------------------------
; windows
; -----------------------------------------------------------------------------
draw_console:
        cmp     byte [console_visible], 0
        je      .ret
        mov     ax, [console_x]
        mov     [wf_x], ax
        mov     ax, [console_y]
        mov     [wf_y], ax
        mov     word [wf_w], CONSOLE_W
        mov     word [wf_h], CONSOLE_H
        mov     ax, title_console
        mov     [wf_title], ax
        call    draw_frame
        call    draw_term
.ret:
        ret

draw_term:
        mov     byte [term_draw_i], 0
.loop:
        movzx   ax, byte [term_draw_i]
        imul    ax, ax, 40
        add     ax, term_buf
        mov     si, ax

        mov     ax, [console_x]
        add     ax, 4
        mov     bx, [console_y]
        add     bx, 14
        movzx   cx, byte [term_draw_i]
        imul    cx, cx, 9
        add     bx, cx
        mov     cl, 0
        call    text_r

        inc     byte [term_draw_i]
        cmp     byte [term_draw_i], 14
        jb      .loop

        mov     al, [0x046C]
        test    al, 0x10
        jz      .done

        mov     bx, [term_cur]
        imul    bx, bx, 9
        add     bx, [console_y]
        add     bx, 14

        mov     ax, [term_cur]
        imul    ax, ax, 40
        add     ax, term_buf
        mov     si, ax
        xor     cx, cx
.len:
        cmp     byte [si], 0
        je      .len_done
        inc     si
        inc     cx
        cmp     cx, 35
        jb      .len
.len_done:
        shl     cx, 3
        mov     ax, [console_x]
        add     ax, 4
        add     ax, cx
        mov     cx, 1
        mov     dx, 8
        mov     si, 0
        call    fill_r
.done:
        ret

draw_system:
        cmp     byte [system_visible], 0
        je      .ret
        mov     ax, [system_x]
        mov     [wf_x], ax
        mov     ax, [system_y]
        mov     [wf_y], ax
        mov     word [wf_w], SYSTEM_W
        mov     word [wf_h], SYSTEM_H
        mov     ax, title_system
        mov     [wf_title], ax
        call    draw_frame

        mov     ax, [system_y]
        add     ax, 14
        mov     [sys_ty], ax

        mov     ax, [system_x]
        add     ax, 6
        mov     bx, [sys_ty]
        mov     si, label_sys_title
        mov     cl, 1
        call    text_r

        add     word [sys_ty], 9
        mov     ax, [system_x]
        add     ax, 6
        mov     bx, [sys_ty]
        mov     si, cpu_line
        mov     cl, 0
        call    text_r

        add     word [sys_ty], 9
        mov     ax, [system_x]
        add     ax, 6
        mov     bx, [sys_ty]
        mov     si, ram_short
        mov     cl, 0
        call    text_r

        add     word [sys_ty], 9
        mov     ax, [system_x]
        add     ax, 6
        mov     bx, [sys_ty]
        mov     si, label_video
        mov     cl, 0
        call    text_r

        add     word [sys_ty], 9
        mov     ax, [system_x]
        add     ax, 6
        mov     bx, [sys_ty]
        mov     si, label_bootline
        mov     cl, 0
        call    text_r

        add     word [sys_ty], 9
        mov     ax, [system_x]
        add     ax, 6
        mov     bx, [sys_ty]
        mov     si, label_mode
        mov     cl, 0
        call    text_r

        add     word [sys_ty], 9
        mov     ax, [system_x]
        add     ax, 6
        mov     bx, [sys_ty]
        mov     si, label_uptime_prefix
        mov     cl, 0
        call    text_r
        mov     ax, [system_x]
        add     ax, 70
        mov     bx, [sys_ty]
        mov     si, uptime_str
        mov     cl, 0
        call    text_r

        add     word [sys_ty], 9
        mov     ax, [system_x]
        add     ax, 6
        mov     bx, [sys_ty]
        mov     si, label_date_prefix
        mov     cl, 0
        call    text_r
        mov     ax, [system_x]
        add     ax, 54
        mov     bx, [sys_ty]
        mov     si, date_str
        mov     cl, 0
        call    text_r
.ret:
        ret

draw_about:
        cmp     byte [about_visible], 0
        je      .ret
        mov     ax, [about_x]
        mov     [wf_x], ax
        mov     ax, [about_y]
        mov     [wf_y], ax
        mov     word [wf_w], ABOUT_W
        mov     word [wf_h], ABOUT_H
        mov     ax, title_about
        mov     [wf_title], ax
        call    draw_frame

        mov     word [g_scale], 2
        mov     ax, [about_x]
        add     ax, 52
        mov     bx, [about_y]
        add     bx, 16
        mov     si, label_logo
        mov     cl, 1
        call    text_scale_r

        mov     ax, [about_x]
        add     ax, 24
        mov     bx, [about_y]
        add     bx, 42
        mov     si, about_msg1
        mov     cl, 0
        call    text_r

        mov     ax, [about_x]
        add     ax, 24
        mov     bx, [about_y]
        add     bx, 53
        mov     si, about_msg2
        mov     cl, 0
        call    text_r

        mov     ax, [about_x]
        add     ax, 8
        mov     bx, [about_y]
        add     bx, 64
        mov     si, about_msg3
        mov     cl, 8
        call    text_r

        mov     ax, [about_x]
        add     ax, 80
        mov     bx, [about_y]
        add     bx, 80
        mov     cx, 40
        mov     dx, 14
        call    g_raise
        mov     ax, [about_x]
        add     ax, 92
        mov     bx, [about_y]
        add     bx, 83
        mov     si, label_ok
        mov     cl, 0
        call    text_r
.ret:
        ret

; -----------------------------------------------------------------------------
; full frame
; -----------------------------------------------------------------------------
redraw:
        call    update_clock
        call    update_uptime
        call    draw_desktop
        call    draw_taskbar
        call    draw_console
        call    draw_system
        call    draw_about
        call    draw_menu
        call    blit
        call    draw_cursor
        ret

; -----------------------------------------------------------------------------
; hit_rect_xy - AL=1 if the mouse is inside AX,BX,CX,DX rectangle
; -----------------------------------------------------------------------------
hit_rect_xy:
        mov     si, [mouse_x]
        cmp     si, ax
        jl      .no
        add     cx, ax
        cmp     si, cx
        jge     .no
        mov     si, [mouse_y]
        cmp     si, bx
        jl      .no
        add     dx, bx
        cmp     si, dx
        jge     .no
        mov     al, 1
        ret
.no:
        xor     al, al
        ret

; -----------------------------------------------------------------------------
; mouse click handling
; -----------------------------------------------------------------------------
mouse_click:
        pushad

        ; About is modal and always on top
        cmp     byte [about_visible], 0
        je      .no_about
        HIT [about_x], [about_y], ABOUT_W, ABOUT_H
        test    al, al
        jz      .done
        mov     ax, [about_x]
        add     ax, ABOUT_W - 20
        mov     bx, [about_y]
        add     bx, 0
        mov     cx, 20
        mov     dx, 12
        call    hit_rect_xy
        test    al, al
        jnz     .close_about

        mov     ax, [about_x]
        add     ax, 76
        mov     bx, [about_y]
        add     bx, 77
        mov     cx, 48
        mov     dx, 19
        call    hit_rect_xy
        test    al, al
        jnz     .close_about

        mov     ax, [about_x]
        inc     ax
        mov     bx, [about_y]
        inc     bx
        mov     cx, ABOUT_W - 2
        mov     dx, 10
        call    hit_rect_xy
        test    al, al
        jz      .done
        mov     byte [drag_win], 3
        mov     ax, [mouse_x]
        sub     ax, [about_x]
        mov     [drag_dx], ax
        mov     ax, [mouse_y]
        sub     ax, [about_y]
        mov     [drag_dy], ax
        jmp     .done
.close_about:
        mov     byte [about_visible], 0
        mov     byte [dirty], 1
        jmp     .done

.no_about:
        cmp     byte [menu_open], 0
        je      .taskbar
        HIT MENU_X, MENU_Y, MENU_W, MENU_H
        test    al, al
        jz      .menu_outside
        mov     ax, [mouse_y]
        sub     ax, MENU_Y + 2
        js      .done
        xor     dx, dx
        mov     bx, 12
        div     bx
        cmp     ax, 3
        ja      .done
        mov     byte [menu_open], 0
        cmp     ax, 0
        je      .m_system
        cmp     ax, 1
        je      .m_console
        cmp     ax, 2
        je      .m_about
        mov     al, 0xFE                ; reboot
        out     0x64, al
        cli
        hlt
        jmp     $
.m_system:
        mov     byte [system_visible], 1
        mov     byte [dirty], 1
        jmp     .done
.m_console:
        mov     byte [console_visible], 1
        mov     byte [dirty], 1
        jmp     .done
.m_about:
        mov     byte [about_visible], 1
        mov     byte [dirty], 1
        jmp     .done
.menu_outside:
        mov     byte [menu_open], 0
        mov     byte [dirty], 1
        HIT 2, TASKBAR_Y, 50, 14
        test    al, al
        jnz     .done                   ; Start just closed the menu

.taskbar:
        HIT 2, TASKBAR_Y, 50, 14
        test    al, al
        jz      .tb_console
        mov     byte [menu_open], 1
        mov     byte [dirty], 1
        jmp     .done
.tb_console:
        HIT 56, TASKBAR_Y, 62, 14
        test    al, al
        jz      .tb_system
        mov     byte [console_visible], 1
        mov     byte [dirty], 1
        jmp     .done
.tb_system:
        HIT 122, TASKBAR_Y, 56, 14
        test    al, al
        jz      .windows
        mov     byte [system_visible], 1
        mov     byte [dirty], 1
        jmp     .done

.windows:
        cmp     byte [system_visible], 0
        je      .console_win
        HIT [system_x], [system_y], SYSTEM_W, SYSTEM_H
        test    al, al
        jz      .console_win
        mov     ax, [system_x]
        add     ax, SYSTEM_W - 20
        mov     bx, [system_y]
        add     bx, 0
        mov     cx, 20
        mov     dx, 12
        call    hit_rect_xy
        test    al, al
        jnz     .close_system

        mov     ax, [system_x]
        inc     ax
        mov     bx, [system_y]
        inc     bx
        mov     cx, SYSTEM_W - 2
        mov     dx, 10
        call    hit_rect_xy
        test    al, al
        jz      .done
        mov     byte [drag_win], 2
        mov     ax, [mouse_x]
        sub     ax, [system_x]
        mov     [drag_dx], ax
        mov     ax, [mouse_y]
        sub     ax, [system_y]
        mov     [drag_dy], ax
        jmp     .done
.close_system:
        mov     byte [system_visible], 0
        mov     byte [dirty], 1
        jmp     .done

.console_win:
        cmp     byte [console_visible], 0
        je      .done
        HIT [console_x], [console_y], CONSOLE_W, CONSOLE_H
        test    al, al
        jz      .done
        mov     ax, [console_x]
        add     ax, CONSOLE_W - 20
        mov     bx, [console_y]
        add     bx, 0
        mov     cx, 20
        mov     dx, 12
        call    hit_rect_xy
        test    al, al
        jnz     .close_console

        mov     ax, [console_x]
        inc     ax
        mov     bx, [console_y]
        inc     bx
        mov     cx, CONSOLE_W - 2
        mov     dx, 10
        call    hit_rect_xy
        test    al, al
        jz      .done
        mov     byte [drag_win], 1
        mov     ax, [mouse_x]
        sub     ax, [console_x]
        mov     [drag_dx], ax
        mov     ax, [mouse_y]
        sub     ax, [console_y]
        mov     [drag_dy], ax
        jmp     .done
.close_console:
        mov     byte [console_visible], 0
        mov     byte [dirty], 1
        jmp     .done

.done:
        popad
        ret

; -----------------------------------------------------------------------------
; drag_move - move the window being dragged
; -----------------------------------------------------------------------------
drag_move:
        cmp     byte [drag_win], 1
        je      .console
        cmp     byte [drag_win], 2
        je      .system
        cmp     byte [drag_win], 3
        je      .about
        ret
.console:
        mov     ax, [mouse_x]
        sub     ax, [drag_dx]
        cmp     ax, 0
        jge     .cx1
        xor     ax, ax
.cx1:
        cmp     ax, 320 - CONSOLE_W
        jle     .cx2
        mov     ax, 320 - CONSOLE_W
.cx2:
        mov     [console_x], ax
        mov     ax, [mouse_y]
        sub     ax, [drag_dy]
        cmp     ax, 0
        jge     .cy1
        xor     ax, ax
.cy1:
        cmp     ax, TASKBAR_Y - CONSOLE_H
        jle     .cy2
        mov     ax, TASKBAR_Y - CONSOLE_H
.cy2:
        mov     [console_y], ax
        mov     byte [dirty], 1
        ret
.system:
        mov     ax, [mouse_x]
        sub     ax, [drag_dx]
        cmp     ax, 0
        jge     .sx1
        xor     ax, ax
.sx1:
        cmp     ax, 320 - SYSTEM_W
        jle     .sx2
        mov     ax, 320 - SYSTEM_W
.sx2:
        mov     [system_x], ax
        mov     ax, [mouse_y]
        sub     ax, [drag_dy]
        cmp     ax, 0
        jge     .sy1
        xor     ax, ax
.sy1:
        cmp     ax, TASKBAR_Y - SYSTEM_H
        jle     .sy2
        mov     ax, TASKBAR_Y - SYSTEM_H
.sy2:
        mov     [system_y], ax
        mov     byte [dirty], 1
        ret
.about:
        mov     ax, [mouse_x]
        sub     ax, [drag_dx]
        cmp     ax, 0
        jge     .ax1
        xor     ax, ax
.ax1:
        cmp     ax, 320 - ABOUT_W
        jle     .ax2
        mov     ax, 320 - ABOUT_W
.ax2:
        mov     [about_x], ax
        mov     ax, [mouse_y]
        sub     ax, [drag_dy]
        cmp     ax, 0
        jge     .ay1
        xor     ax, ax
.ay1:
        cmp     ax, TASKBAR_Y - ABOUT_H
        jle     .ay2
        mov     ax, TASKBAR_Y - ABOUT_H
.ay2:
        mov     [about_y], ax
        mov     byte [dirty], 1
        ret







