; =============================================================================
;  MiniOS boot sector
;  Reads 143 sectors (71.5 KB) one by one from a 1.44 MB floppy to 0000:8000.
;  Reading one sector per BIOS call is slower but works on every BIOS.
; =============================================================================
        BITS 16
        ORG 0x7C00

KERNEL_SEG  equ 0x0800
LOAD_COUNT  equ 143

start:
        cli
        xor     ax, ax
        mov     ds, ax
        mov     es, ax
        mov     ss, ax
        mov     sp, 0x7C00
        sti
        mov     [boot_drive], dl

        mov     si, msg_loading
        call    print

        mov     dl, [boot_drive]
        xor     ah, ah
        int     0x13
        jc      boot_fail

        mov     word [dest_seg], KERNEL_SEG
        mov     word [dest_off], 0
        mov     byte [cyl], 0
        mov     byte [head], 0
        mov     byte [sect], 2
        mov     bp, LOAD_COUNT

.load_loop:
        mov     ax, [dest_seg]
        mov     es, ax
        mov     bx, [dest_off]
        mov     ch, [cyl]
        mov     dh, [head]
        mov     cl, [sect]
        mov     al, 1
        mov     dl, [boot_drive]
        call    read_sector
        jc      boot_fail

        add     word [dest_off], 512
        jnc     .no_wrap
        add     word [dest_seg], 0x1000
.no_wrap:
        inc     byte [sect]
        cmp     byte [sect], 19
        jb      .next
        mov     byte [sect], 1
        xor     byte [head], 1
        jnz     .next
        inc     byte [cyl]
.next:
        dec     bp
        jnz     .load_loop

        mov     dl, [boot_drive]
        jmp     0x0000:0x8000

; -----------------------------------------------------------------------------
; read_sector: ES:BX <- one sector, CHS = CH/CL/DH, drive DL
; -----------------------------------------------------------------------------
read_sector:
        mov     byte [retry], 4
.try:
        push    ax
        push    bx
        push    cx
        push    dx
        mov     ah, 0x02
        int     0x13
        mov     [last_error], ah
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        jnc     .ok

        push    ax
        push    dx
        xor     ah, ah
        mov     dl, [boot_drive]
        int     0x13
        pop     dx
        pop     ax
        dec     byte [retry]
        jnz     .try
        stc
        ret
.ok:
        clc
        ret

; -----------------------------------------------------------------------------
print:
        push    ax
        push    bx
.next:
        lodsb
        test    al, al
        jz      .done
        mov     ah, 0x0E
        mov     bx, 0x0007
        int     0x10
        jmp     .next
.done:
        pop     bx
        pop     ax
        ret

print_hex:
        push    ax
        mov     ah, al
        shr     al, 4
        call    .nibble
        mov     al, ah
        call    .nibble
        pop     ax
        ret
.nibble:
        and     al, 0x0F
        add     al, '0'
        cmp     al, '9'
        jbe     .out
        add     al, 7
.out:
        mov     ah, 0x0E
        mov     bx, 0x0007
        int     0x10
        ret

boot_fail:
        mov     si, msg_fail
        call    print
        mov     al, [last_error]
        call    print_hex
        mov     si, msg_drive
        call    print
        mov     al, [boot_drive]
        call    print_hex
        mov     si, msg_crlf
        call    print
        cli
        hlt
        jmp     $

; -----------------------------------------------------------------------------
boot_drive      db 0
dest_seg        dw 0
dest_off        dw 0
cyl             db 0
head            db 0
sect            db 0
retry           db 0
last_error      db 0
msg_loading     db 'MiniOS boot...', 13, 10, 0
msg_fail        db 'Boot error: INT13 AH=', 0
msg_drive       db ' drive=', 0
msg_crlf        db 13, 10, 0

        times 510 - ($ - $$) db 0
        dw 0xAA55
