; =============================================================================
;  MiniOS 2.0 — Stage 2 VBE 2.0 (640x480x8bpp LFB) setup & Protected Mode entry
;  Loaded by boot.asm from LBA 193 (CHS 5/0/14) to 0x0000:0x7E00 (outside the
;  0x8000..0xA3FF FDC DMA buffer). Writes a 16-byte video handoff block at
;  0x00000500 ("VBE1" or "VGA1" fallback), enables A20, and enters 32-bit PM.
; =============================================================================
        BITS 16
        ORG 0x7E00

VBE_INFO_ADDR   equ 0x0500       ; 512B VbeInfoBlock (0x0500..0x06FF)
MODE_INFO_ADDR  equ 0x0700       ; 256B ModeInfoBlock (0x0700..0x07FF)
TARGET_W        equ 640
TARGET_H        equ 480
TARGET_BPP      equ 8

stage2_start:
        cli
        xor     ax, ax
        mov     ds, ax
        mov     es, ax
        mov     ss, ax
        mov     sp, 0x7C00
        sti

        ; Holding Left/Right Shift at boot forces 320x200 VGA fallback.
        test    byte [0x0417], 0x03
        jnz     vga_fallback
        cmp     byte [force_vga], 0
        jne     vga_fallback

        ; Zero 0x0500..0x07FF and request VBE 2.0 controller info.
        cld
        xor     ax, ax
        mov     di, VBE_INFO_ADDR
        mov     cx, (512 + 256) / 2
        rep     stosw

        mov     dword [VBE_INFO_ADDR], 0x32454256   ; "VBE2"
        mov     ax, 0x4F00
        mov     di, VBE_INFO_ADDR
        int     0x10
        cmp     ax, 0x004F
        jne     vga_fallback
        cmp     dword [VBE_INFO_ADDR], 0x41534556   ; "VESA"
        jne     vga_fallback

        mov     si, [VBE_INFO_ADDR + 0x0E]          ; VideoModePtr offset
        mov     ax, [VBE_INFO_ADDR + 0x10]          ; VideoModePtr segment
        mov     dx, si
        or      dx, ax
        jz      vga_fallback
        mov     fs, ax

.mode_loop:
        mov     cx, [fs:si]
        cmp     cx, 0xFFFF
        je      vga_fallback
        add     si, 2

        push    fs
        push    si
        push    cx
        xor     ax, ax
        mov     es, ax
        mov     di, MODE_INFO_ADDR
        mov     cx, 256 / 2
        rep     stosw
        pop     cx
        push    cx

        mov     ax, 0x4F01
        mov     di, MODE_INFO_ADDR
        int     0x10
        pop     cx
        pop     si
        pop     fs
        cmp     ax, 0x004F
        jne     .mode_loop

        ; Require: mode supported (bit 0) + graphics (bit 4) + LFB (bit 7).
        mov     ax, [MODE_INFO_ADDR + 0x00]
        and     ax, 0x0091
        cmp     ax, 0x0091
        jne     .mode_loop

        cmp     word [MODE_INFO_ADDR + 0x12], TARGET_W
        jne     .mode_loop
        cmp     word [MODE_INFO_ADDR + 0x14], TARGET_H
        jne     .mode_loop
        cmp     byte [MODE_INFO_ADDR + 0x19], TARGET_BPP
        jne     .mode_loop

        mov     eax, [MODE_INFO_ADDR + 0x28]        ; PhysBasePtr
        cmp     eax, 0x00100000
        jb      .mode_loop

        ; Activate VBE mode with Linear Framebuffer (bit 14 = 0x4000).
        mov     bx, cx
        or      bx, 0x4000
        mov     ax, 0x4F02
        int     0x10
        cmp     ax, 0x004F
        jne     vga_fallback

        ; Populate 16-byte handoff struct at 0x0500 for the 32-bit kernel.
        mov     dword [0x0500], 0x31454256          ; "VBE1"
        mov     ax, [MODE_INFO_ADDR + 0x12]         ; width (640)
        mov     [0x0504], ax
        mov     ax, [MODE_INFO_ADDR + 0x14]         ; height (480)
        mov     [0x0506], ax
        mov     ax, [MODE_INFO_ADDR + 0x10]         ; BytesPerScanLine (pitch)
        test    ax, ax
        jnz     .have_pitch
        mov     ax, TARGET_W
.have_pitch:
        mov     [0x0508], ax
        mov     al, [MODE_INFO_ADDR + 0x19]         ; bpp (8)
        mov     [0x050A], al
        mov     byte [0x050B], 1                    ; vbe_active = 1
        mov     eax, [MODE_INFO_ADDR + 0x28]        ; PhysBasePtr
        mov     [0x050C], eax
        jmp     enter_pm

vga_fallback:
        mov     ax, 0x0013                          ; Standard VGA 320x200x256
        int     0x10
        mov     dword [0x0500], 0x31414756          ; "VGA1"
        mov     word  [0x0504], 320
        mov     word  [0x0506], 200
        mov     word  [0x0508], 320
        mov     byte  [0x050A], 8
        mov     byte  [0x050B], 0
        mov     dword [0x050C], 0x000A0000

enter_pm:
        cli
        in      al, 0x92
        or      al, 0x02
        and     al, 0xFE
        out     0x92, al

        lgdt    [gdt_ptr]
        mov     eax, cr0
        or      eax, 1
        mov     cr0, eax
        jmp     0x08:pm_entry

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

        BITS 16
        align 8
gdt_start:
        dq      0
        dq      0x00CF9A000000FFFF     ; flat 32-bit Ring 0 code (0x08)
        dq      0x00CF92000000FFFF     ; flat 32-bit Ring 0 data (0x10)
gdt_end:
gdt_ptr:
        dw      gdt_end - gdt_start - 1
        dd      gdt_start

        times 509 - ($ - $$) db 0
force_vga:
        db      0                      ; patch to 1 to force VGA 320x200 fallback
        dw      0xAA55
