; Runaway task for the ps/kill test.
        BITS 32
        ORG 0x400000
spin:
        jmp     spin
