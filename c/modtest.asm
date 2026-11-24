; Minimal kernel-mode module for the MicroPython bring-up ABI.
; Linked at 0x100000; the loader copies it there, zeroes BSS, and calls entry(api).
        BITS 32
        ORG 0x100000

        dd 'MPM1'                   ; magic
        dd entry - 0x100000         ; entry offset
        dd image_end - 0x100000     ; image size (includes header)
        dd 0                        ; BSS size
        dd 0x00100000               ; requested heap size (1 MB)

entry:
        mov     eax, [esp + 4]      ; struct minios_api *
        mov     esi, eax
        mov     ebx, msg
        push    ebx
        call    dword [esi + 4]     ; api->puts
        add     esp, 4

        call    dword [esi + 12]    ; api->ticks_ms
        mov     ebx, msg_ticks
        push    ebx
        call    dword [esi + 4]
        add     esp, 4

        ret

msg:          db 'Kernel module ABI works: hello from 0x100000!', 10, 0
msg_ticks:    db 'Module API ticks_ms called.', 10, 0
image_end:
