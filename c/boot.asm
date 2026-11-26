; =============================================================================
;  MiniOS C-kernel boot sector (LBA 0)
;  Loads:
;    - 256 sectors (128 KB) kernel from LBA 1..256  -> 0x1000:0x0000 (0x10000)
;    -  64 sectors (32 KB) MPK1   from LBA 257..320 -> 0x7000:0x0000 (0x70000)
;    -   1 sector  (512 B) stage2 from LBA 321      -> 0x0000:0x7E00 (0x07E00)
;  Then transfers control to stage2 for VBE 640x480x8 LFB setup + 32-bit PM.
; =============================================================================
        BITS 16
        ORG 0x7C00

KERNEL_SEG      equ 0x1000       ; ES for 0x10000
KERNEL_SECTORS  equ 256
PACK_SEG        equ 0x7000       ; ES for 0x70000
PACK_SECTORS    equ 64
STAGE2_OFFSET   equ 0x7E00       ; 0x0000:0x7E00 (outside 0x8000 FDC DMA buf)

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

        ; Kernel: LBA 1..256 (CHS 0/0/2), loaded to 0x10000.
        mov     ax, KERNEL_SEG
        mov     es, ax
        xor     bx, bx
        mov     byte [cyl], 0
        mov     byte [head], 0
        mov     byte [sect], 2
        mov     word [left], KERNEL_SECTORS
        call    load_sectors

        ; Program pack: LBA 257..320 (CHS 7/0/6), loaded to 0x70000.
        mov     ax, PACK_SEG
        mov     es, ax
        xor     bx, bx
        mov     byte [cyl], 7
        mov     byte [head], 0
        mov     byte [sect], 6
        mov     word [left], PACK_SECTORS
        call    load_sectors

        ; Stage 2 VBE/PM trampoline: LBA 321 (CHS 8/1/16), loaded to 0x0000:0x7E00.
        xor     ax, ax
        mov     es, ax
        mov     bx, STAGE2_OFFSET
        mov     byte [cyl], 8
        mov     byte [head], 1
        mov     byte [sect], 16
        mov     word [left], 1
        call    load_sectors

        cmp     word [STAGE2_OFFSET + 510], 0xAA55
        jne     fail
        call    detect_memory
        jmp     0x0000:STAGE2_OFFSET

; -----------------------------------------------------------------------------
; E820 memory map -> 0x2000, header at 0x1800 ("MEMA", count at 0x1804).
; E801 fallback builds two available ranges.
detect_memory:
        mov     dword [0x1800], 0x414D454D
        mov     word  [0x1804], 0
        mov     di, 0x2000
        xor     ebx, ebx
.loop:
        mov     eax, 0xE820
        mov     edx, 0x534D4150
        mov     ecx, 24
        int     0x15
        jc      .done
        cmp     eax, 0x534D4150
        jne     .done
        inc     word [0x1804]
        add     di, 24
        cmp     word [0x1804], 32
        jae     .done
        test    ebx, ebx
        jnz     .loop
.done:
        ret

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
        dec     word [left]
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
boot_drive      db 0
cyl             db 0
head            db 0
sect            db 0
left            dw 0
retry           db 0
msg_loading     db 'Loading MiniOS 2.0...', 13, 10, 0
msg_fail        db 'Floppy read error.', 13, 10, 0

        times 510 - ($ - $$) db 0
        dw 0xAA55


