; Ring3 page-fault test: write a supervisor-only address.
        BITS 32
        ORG 0x400000
start:
        mov     dword [0xB8000], 0x41414141
        jmp     $
