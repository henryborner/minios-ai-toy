; =============================================================================
;  MiniOS Ring 3 MicroPython startup header (UMP1) + x86 NLR & GC helpers
;  Linked at virtual base 0x00400000.
; =============================================================================
        BITS 32

        global _ump_header
        global _start
        global nlr_push
        global nlr_jump_asm
        global minios_gc_collect_regs

        extern mpy_main
        extern nlr_push_tail
        extern gc_collect_root
        extern __entry_offset
        extern __image_size
        extern __bss_size
        extern __ro_size

        section .ump_header
_ump_header:
        db 'U', 'M', 'P', '1'
        dd __entry_offset
        dd __image_size
        dd __bss_size
        dd 0x00180000               ; 1.5 MB heap request hint
        dd __ro_size

        section .text
_start:
        cld
        ; Initialize x87 FPU to IEEE-754 53-bit double precision with masked exceptions
        fninit
        fldcw   [fpu_cw_53bit]

        ; Extract argc / argv if kernel placed arguments below 0x00490000
        xor     eax, eax            ; argc = 0
        xor     edx, edx            ; argv = NULL
        cmp     esp, 0x00490000
        jae     .no_args
        mov     eax, [esp]          ; argc
        lea     edx, [esp + 4]      ; argv
.no_args:
        and     esp, 0xFFFFFFF0     ; 16-byte stack alignment
        sub     esp, 8
        push    edx                 ; argv
        push    eax                 ; argc
        call    mpy_main

        ; Exit Ring 3 task with mpy_main return status (syscall 11)
        mov     ebx, eax
        mov     eax, 11
        int     0x80
.hang:
        pause
        jmp     .hang

; -----------------------------------------------------------------------------
; unsigned int nlr_push(nlr_buf_t *nlr)
; Pure assembly implementation without compiler frame-pointer assumptions.
; nlr_buf_t layout: [0]=prev, [4]=ret_val, [8..28]=regs[0..5]
; -----------------------------------------------------------------------------
nlr_push:
        mov     edx, [esp + 4]      ; nlr_buf_t *nlr
        mov     eax, [esp]          ; return EIP
        mov     [edx + 8], eax
        mov     [edx + 12], ebp
        mov     [edx + 16], esp
        mov     [edx + 20], ebx
        mov     [edx + 24], edi
        mov     [edx + 28], esi
        jmp     nlr_push_tail

; -----------------------------------------------------------------------------
; void nlr_jump_asm(nlr_buf_t *top)
; Restores callee-saved registers and jumps back to nlr_push caller with EAX=1.
; -----------------------------------------------------------------------------
nlr_jump_asm:
        mov     edx, [esp + 4]      ; nlr_buf_t *top
        mov     esi, [edx + 28]
        mov     edi, [edx + 24]
        mov     ebx, [edx + 20]
        mov     esp, [edx + 16]
        mov     ebp, [edx + 12]
        mov     eax, [edx + 8]
        mov     [esp], eax
        mov     eax, 1
        ret

; -----------------------------------------------------------------------------
; void minios_gc_collect_regs(uintptr_t stack_top)
; Pushes callee-saved registers onto the stack and scans [esp .. stack_top).
; -----------------------------------------------------------------------------
minios_gc_collect_regs:
        mov     eax, [esp + 4]      ; stack_top
        push    ebp
        push    ebx
        push    esi
        push    edi
        mov     edx, esp            ; root start = current esp
        sub     eax, edx            ; byte count = stack_top - esp
        shr     eax, 2              ; word count (sizeof(uintptr_t) == 4)
        jz      .done
        push    eax                 ; n_words
        push    edx                 ; void **ptrs
        call    gc_collect_root
        add     esp, 8
.done:
        pop     edi
        pop     esi
        pop     ebx
        pop     ebp
        ret

        section .rodata
        align 4
fpu_cw_53bit:
        dw      0x027F
