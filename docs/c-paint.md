# C Paint (native Ring 3)

`c/paint_c.c` is a freestanding C MicroPython-free Paint demo.

- Raw task loaded at `0x00400000`
- `int 0x80` Canvas syscalls only; no libc
- 688-byte flat binary
- draw with left drag, `0`–`9` select color, `c` clear, `q`/Esc quit
- `q` closes Canvas and exits

Build/inject without changing `c/build_c.py` or the MiniCC tree:

```text
python tools/build_paintc.py
```

The tool builds `c/build/paint_c.bin`, adds `PAINTC.BIN` to a copy of the
current program pack, and writes `MiniOS-C-paintc.img`. In the guest:

```text
run PAINTC.BIN
```

## Kernel fix exposed by C Paint

`task_create()` used to map the 4 KiB small-task stack at `0x00402000` while
setting the initial ESP to `0x00402000`; the first push landed in the unmapped
`0x00401000` page. The stack page is now mapped at `0x00401000`, with ESP still
`0x00402000`. Existing asm demos did not push early enough to expose this.

C Paint itself uses the Canvas `minios_canvas_cmd` layout from
`c/mpy_port/syscall.h`; all drawing goes through `SysCanvasDraw`.
