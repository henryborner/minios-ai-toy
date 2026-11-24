; =============================================================================
;  MiniOS graphics primitives
;  320x200x256 VGA, all drawing goes to a 64000-byte back buffer at 2000:0000
;  and is copied to A000:0000 once per frame (no flicker).
; =============================================================================

; -----------------------------------------------------------------------------
; g_fill - fill the rectangle described by g_x/g_y/g_w/g_h with g_color
; Clips against the screen.  Preserves all registers.
; -----------------------------------------------------------------------------
g_fill:
        pushad
        push    es
        cld
        movzx   eax, word [g_x]
        movzx   ebx, word [g_y]
        movzx   ecx, word [g_w]
        movzx   edx, word [g_h]
        test    ecx, ecx
        jz      .done
        test    edx, edx
        jz      .done
        cmp     eax, 320
        jae     .done
        cmp     ebx, 200
        jae     .done
        add     ecx, eax                ; x1
        cmp     ecx, 320
        jbe     .xok
        mov     ecx, 320
.xok:
        add     edx, ebx                ; y1
        cmp     edx, 200
        jbe     .yok
        mov     edx, 200
.yok:
        sub     ecx, eax                ; width
        jbe     .done
        sub     edx, ebx                ; height
        jbe     .done
        imul    ebx, ebx, 320
        add     eax, ebx
        mov     edi, eax
        mov     ax, 0x2000
        mov     es, ax
        mov     al, [g_color]
        mov     ah, al
        mov     ebx, edx                ; rows
        mov     edx, ecx                ; width
.row:
        mov     ecx, edx
        rep     stosb
        add     edi, 320
        sub     edi, edx
        dec     ebx
        jnz     .row
.done:
        pop     es
        popad
        ret

; -----------------------------------------------------------------------------
; text rendering
;   g_text       - 1x scale (8x8 font)
;   g_text_big   - 3x scale
;   g_text_scale - use current g_scale (1..4)
; Inputs: g_x, g_y, g_str (ASCIIZ), g_color
; Font is the VGA ROM 8x8 font at F000:FA6E.
; -----------------------------------------------------------------------------
g_text:
        mov     word [g_scale], 1
        jmp     g_text_common
g_text_big:
        mov     word [g_scale], 3
        jmp     g_text_common
g_text_scale:
        jmp     g_text_common

g_text_common:
        pushad
        push    ds
        push    es
        push    fs
        cld
        mov     ax, 0x2000
        mov     es, ax
        mov     ax, 0xF000
        mov     fs, ax

        movzx   ebp, word [g_scale]
        test    ebp, ebp
        jnz     .scale_ok
        mov     ebp, 1
.scale_ok:
        movzx   edi, word [g_y]
        imul    edi, edi, 320
        movzx   eax, word [g_x]
        add     edi, eax
        movzx   esi, word [g_str]
        mov     bl, [g_color]

.char:
        lodsb
        test    al, al
        jz      .done
        movzx   edx, al
        shl     edx, 3
        add     edx, 0xFA6E             ; VGA ROM 8x8 font base (F000:FA6E)
        mov     byte [g_rowcnt], 8
.row:
        mov     ah, [fs:edx]
        inc     edx
        mov     byte [g_bitcnt], 8
.bit:
        shl     ah, 1
        jnc     .skip
        push    edi
        mov     ecx, ebp
.sy:
        push    ecx
        mov     ecx, ebp
        mov     al, bl
        rep     stosb
        pop     ecx
        add     edi, 320
        sub     edi, ebp                ; undo the horizontal advance from stosb
        dec     ecx
        jnz     .sy
        pop     edi
.skip:
        add     edi, ebp
        dec     byte [g_bitcnt]
        jnz     .bit
        mov     eax, ebp
        imul    eax, eax, 312           ; (320 - 8) * scale
        add     edi, eax
        dec     byte [g_rowcnt]
        jnz     .row
        ; after 8 glyph rows EDI is 8 scanlines below the glyph origin;
        ; bring it back up and advance one character cell to the right
        mov     eax, ebp
        imul    eax, eax, 2552          ; 8*320 - 8
        sub     edi, eax
        jmp     .char
.done:
        pop     fs
        pop     es
        pop     ds
        popad
        ret

; -----------------------------------------------------------------------------
; fill_r / text_r / text_scale_r - register wrappers
;   fill_r       AX=x BX=y CX=w DX=h SI=color
;   text_r       AX=x BX=y SI=string CL=color
;   text_scale_r AX=x BX=y SI=string CL=color ; uses current g_scale
; -----------------------------------------------------------------------------
fill_r:
        mov     [g_x], ax
        mov     [g_y], bx
        mov     [g_w], cx
        mov     [g_h], dx
        mov     ax, si
        mov     [g_color], al
        jmp     g_fill

text_r:
        mov     [g_x], ax
        mov     [g_y], bx
        mov     [g_str], si
        mov     [g_color], cl
        jmp     g_text

text_scale_r:
        mov     [g_x], ax
        mov     [g_y], bx
        mov     [g_str], si
        mov     [g_color], cl
        jmp     g_text_scale

; -----------------------------------------------------------------------------
; blit - copy the 320x200 back buffer to VGA memory
; -----------------------------------------------------------------------------
blit:
        pushad
        push    ds
        push    es
        cld
        mov     ax, 0x2000
        mov     ds, ax
        mov     ax, 0xA000
        mov     es, ax
        xor     esi, esi
        xor     edi, edi
        mov     ecx, 32000
        rep     movsw
        pop     es
        pop     ds
        popad
        ret

; -----------------------------------------------------------------------------
; draw_cursor - overlay the arrow bitmap directly on VGA at mouse_x/mouse_y
; The back buffer stays free of cursor pixels, so cursor_erase can restore
; the background underneath it without a full-screen redraw.
; -----------------------------------------------------------------------------
draw_cursor:
        pushad
        push    es
        cld
        ; read the position once, so cursor_drawn_x/y always match where
        ; this invocation actually draws, even if an IRQ updates the mouse
        movzx   eax, word [mouse_x]
        mov     [cursor_drawn_x], ax
        movzx   ebx, word [mouse_y]
        mov     [cursor_drawn_y], bx
        imul    ebx, ebx, 320
        add     ebx, eax
        mov     edi, ebx
        movzx   ebp, word [cursor_drawn_y]

        mov     ax, 0xA000
        mov     es, ax
        mov     si, cursor_art
        mov     bx, 18
.row:
        cmp     ebp, 200
        jae     .done
        mov     cx, 12
.col:
        mov     al, [si]
        inc     si
        cmp     al, '.'
        je      .skip
        mov     dl, 0
        cmp     al, 'X'
        je      .plot
        mov     dl, 15
.plot:
        mov     [es:edi], dl
.skip:
        inc     edi
        dec     cx
        jnz     .col
        add     edi, 308                ; 320 - 12
        inc     ebp
        dec     bx
        jnz     .row
.done:
        pop     es
        popad
        ret

; -----------------------------------------------------------------------------
; cursor_erase - restore the background under the last drawn cursor from the
; back buffer.  Only the 12x18 cell is copied, not the whole frame.
; -----------------------------------------------------------------------------
cursor_erase:
        pushad
        push    ds
        push    es
        cld
        ; read the saved position BEFORE switching DS to the back buffer,
        ; otherwise the coordinates are read from the wrong segment
        movzx   edi, word [cursor_drawn_y]
        mov     ebp, edi
        cmp     ebp, 200
        jae     .done
        imul    edi, edi, 320
        movzx   eax, word [cursor_drawn_x]
        add     edi, eax
        mov     esi, edi
        mov     bx, 18

        mov     ax, 0x2000
        mov     ds, ax
        mov     ax, 0xA000
        mov     es, ax
.row:
        cmp     ebp, 200
        jae     .done
        mov     cx, 12
        rep     movsb
        add     esi, 308
        add     edi, 308
        inc     ebp
        dec     bx
        jnz     .row
.done:
        pop     es
        pop     ds
        popad
        ret
