; Embed the flat user binary into the kernel image.
        BITS 32
        section .rodata
        global user_blob_start
        global user_blob_end
user_blob_start:
        incbin "build/user.bin"
user_blob_end:
