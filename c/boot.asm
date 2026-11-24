; =============================================================================
;  MiniOS C-kernel boot sector
;  Loads 64 sectors (32 KB) from the floppy to 0000:10000, enables A20,
;  installs a flat GDT and enters 32-bit protected mode at 0x10000.
; =============================================================================
        BITS 16
        ORG 0x7C00

KERNEL_SEG      equ 0x1000       ; ES for 0x10000
KERNEL_SECTORS  equ 64
PACK_SEG        equ 0x7000       ; ES for 0x70000
PACK_SECTORS    equ 64

start:
        cli
        xor     ax, ax
        mov     ds, ax
        mov     es, ax
        mov     ss, ax
        mov     sp, 0x7C00
        sti
        mov     [boot_drive], dl

        mov     ax, 0x0003       ; 80x25 text mode
        int     0x10
        mov     si, msg_loading
        call    print

        mov     dl, [boot_drive]
        xor     ah, ah
        int     0x13
        jc      fail

        ; Kernel: sectors 2-65, loaded to 0x10000.
        mov     ax, KERNEL_SEG
        mov     es, ax
        xor     bx, bx
        mov     byte [cyl], 0
        mov     byte [head], 0
        mov     byte [sect], 2
        mov     byte [left], KERNEL_SECTORS
        call    load_sectors

        ; Program pack: sectors 66-129, loaded to 0x70000.
        mov     ax, PACK_SEG
        mov     es, ax
        xor     bx, bx
        mov     byte [cyl], 1
        mov     byte [head], 1
        mov     byte [sect], 12
        mov     byte [left], PACK_SECTORS
        call    load_sectors

        mov     ax, 0x0013       ; VGA 320x200x256 graphics mode
        int     0x10

        cli
        ; Fast A20 gate
        in      al, 0x92
        or      al, 0x02
        and     al, 0xFE
        out     0x92, al

        lgdt    [gdt_ptr]
        mov     eax, cr0
        or      eax, 1
        mov     cr0, eax
        jmp     0x08:pm_entry

; -----------------------------------------------------------------------------
; In: ES:BX = destination, cyl/head/sect = CHS, left = sector count.
load_sectors:
.loop:
        mov     byte [retry], 3
.try:
        mov     ch, [cyl]
        mov     dh, [head]
        mov     cl, [sect]
        mov     dl, [boot_drive]
        mov     ax, 0x0201
        int     0x13
        jnc     .ok
        xor     ah, ah
        mov     dl, [boot_drive]
        int     0x13
        dec     byte [retry]
        jnz     .try
        jmp     fail
.ok:
        add     bx, 512
        jnc     .no_wrap
        mov     ax, es
        add     ax, 0x1000
        mov     es, ax
.no_wrap:
        inc     byte [sect]
        cmp     byte [sect], 19
        jb      .next
        mov     byte [sect], 1
        xor     byte [head], 1
        jnz     .next
        inc     byte [cyl]
.next:
        dec     byte [left]
        jnz     .loop
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

fail:
        mov     si, msg_fail
        call    print
        cli
        hlt
        jmp     $

; -----------------------------------------------------------------------------
        BITS 32
pm_entry:
        mov     ax, 0x10
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax
        mov     ss, ax
        mov     esp, 0x90000
        jmp     0x10000

; -----------------------------------------------------------------------------
        BITS 16
boot_drive      db 0
cyl             db 0
head            db 0
sect            db 0
left            db 0
retry           db 0
msg_loading     db 'Loading MiniOS C kernel...', 13, 10, 0
msg_fail        db 'Floppy read error.', 13, 10, 0

        align 8
gdt_start:
        dq      0
        dq      0x00CF9A000000FFFF     ; flat 32-bit code, DPL0
        dq      0x00CF92000000FFFF     ; flat 32-bit data
gdt_end:
gdt_ptr:
        dw      gdt_end - gdt_start - 1
        dd      gdt_start

        times 510 - ($ - $$) db 0
        dw 0xAA55
