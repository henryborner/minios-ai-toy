; 32-bit kernel entry stub.  The boot sector has already installed flat
; segments and landed at 0x10000 with interrupts off.
        BITS 32
        global _start
        extern kmain
        extern __bss_start
        extern __bss_end

        section .text
_start:
        cli
        mov     esp, 0x90000
        mov     ebp, esp

        ; zero .bss (objcopy binary does not contain it)
        mov     edi, __bss_start
        mov     ecx, __bss_end
        sub     ecx, edi
        xor     eax, eax
        cld
        rep     stosb

        call    kmain

.hang:
        cli
        hlt
        jmp     .hang
