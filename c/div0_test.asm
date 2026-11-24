; Ring3 divide-by-zero test.
        BITS 32
        ORG 0x400000
start:
        xor     edx, edx
        xor     eax, eax
        div     edx
        jmp     $
