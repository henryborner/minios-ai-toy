; Embed both task binaries into the kernel image.
        BITS 32
        section .rodata
        global task_a_start
        global task_a_end
        global task_b_start
        global task_b_end

task_a_start:
        incbin "build/task_a.bin"
task_a_end:
task_b_start:
        incbin "build/task_b.bin"
task_b_end:
