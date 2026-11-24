# MiniOS 2.0 — C kernel, GUI and Ring 3

This branch (`v2-protected-c`) is the protected-mode / C rewrite. It does not replace
`main` (MiniOS 1.0, the real-mode assembly OS) yet.

## What works

- 16-bit boot sector loads the kernel to `0x10000`, enables A20, flat protected mode
- Freestanding C kernel, clang + `ld.lld`
- VGA mode 13h 320×200×256 GUI, custom palette, ROM font
- IDT/PIC/PIT: IRQ0 timer 100 Hz, IRQ1 keyboard, IRQ12 PS/2 mouse
- Mouse cursor, Start menu, taskbar, draggable windows, close/OK buttons
- Console shell, System Information window, About dialog
- **4 KB paging**: identity-mapped first 8 MB; kernel pages supervisor,
  pages at `0x400000` marked user
- **GDT with user code/data descriptors and a TSS**
- **Ring 3 user program** entered via `iret`
- **`int 0x80` syscall gate (DPL3)** with a kernel syscall handler
- Kernel verifies the saved CS of the syscall frame and prints `CPL=3`
- HLT idle loop

Screenshots:

- [`../screenshots/c-gui-boot.png`](../screenshots/c-gui-boot.png)
- [`../screenshots/c-gui-help.png`](../screenshots/c-gui-help.png)
- [`../screenshots/c-gui-startmenu.png`](../screenshots/c-gui-startmenu.png)
- [`../screenshots/c-gui-system.png`](../screenshots/c-gui-system.png)
- [`../screenshots/c-gui-about.png`](../screenshots/c-gui-about.png)
- [`../screenshots/c-ring3-cpl.png`](../screenshots/c-ring3-cpl.png)\r\n- [`../screenshots/c-ring3-return.png`](../screenshots/c-ring3-return.png)

## Ring 3 demo

At the shell prompt type:

```text
ring3
```

The kernel then:

1. installs a new GDT and TSS (`ss0=0x10`, `esp0=0x80000`);
2. enables 4 KB paging, mapping the first 8 MB; kernel is supervisor-only,
   `0x400000–0x403FFF` is user-accessible;
3. installs the DPL3 `int 0x80` gate;
4. copies the embedded user binary to `0x400000` and `iret`s to CPL3.

The user program calls syscall 1 (write string) and syscall 2 (exit). The syscall
stub passes the saved CS to C; the kernel prints `syscall from CPL=3 (CS=0x1B)`.
Then the demo halts the machine.

This is a **demo exit**, not a scheduler yet: returning to the GUI shell after a
user program exits requires a saved kernel context/scheduler.

## Controls

| Input | Action |
|---|---|
| Mouse | click, drag window titles, close buttons, Start menu |
| `F1` / `F2` / `F3` | About / System / Console |
| `Esc` | close About, System, menu |
| Keyboard | Console shell input |

Shell commands: `help`, `ver`, `cpu`, `mem`, `time`, `date`, `uptime`, `clear`,
`echo`, `sysinfo`, `about`, `ring3`, `reboot`, `halt`.

## Build

Requirements:

- NASM (`tools/nasm-2.16.03/` or PATH)
- clang with i386 target
- `ld.lld` (MSYS2: `pacman -S mingw-w64-ucrt-x86_64-lld`)
- `objcopy`

```powershell
python c/build_c.py
```

Outputs:

- `c/build/boot.bin` — 512 bytes
- `c/build/kernel.bin` — currently ~11 KB
- `c/build/user.bin` — Ring 3 demo
- `MiniOS-C.img` — 1,474,560 bytes

## Run

```powershell
qemu-system-i386 `
  -drive "file=MiniOS-C.img,if=floppy,format=raw,readonly=on" `
  -boot a -m 128 -rtc base=localtime
```

## Source layout

| File | Role |
|---|---|
| `boot.asm` | real-mode loader, A20, flat GDT, protected mode, VGA mode 13h |
| `entry.asm` | 32-bit entry, stack, BSS zero, `kmain` |
| `interrupts.asm` | IRQ0/IRQ1/IRQ12 stubs + `int 0x80` syscall stub |
| `usermode.asm` | `enter_user`: build the Ring 3 IRET frame |
| `user.asm` | the Ring 3 demo program, linked at `0x400000` |
| `user_blob.asm` | embeds `user.bin` into the kernel image |
| `kernel.c` | IDT/PIC/PIT, keyboard/mouse, GDT/TSS, paging, syscalls, main loop |
| `gui.c` | VGA framebuffer, font, windows, shell, mouse UI |
| `gui.h` | GUI API and kernel/GUI globals |
| `linker.ld` | kernel at `0x10000`, BSS NOLOAD |

## Status / caveats

- Ring 1/2 unused (normal; only Ring 0/3 matter here).
- Paging is identity-only and one address space; no per-process page directories yet.
- User syscall 2 returns to the GUI; cooperative multi-task scheduler is next.
- No filesystem; user program is embedded in the kernel image.
- Static 64 KB heap; no general allocator.
- Boot loader loads 64 floppy sectors (32 KB); kernel file is ~11 KB.

## Next milestones

1. User exit back to kernel / GUI and a minimal scheduler.
2. Per-process page directories and isolated user address spaces.
3. Filesystem + loadable user programs.
4. More syscalls (VGA/window drawing, file I/O, keyboard input).
5. On-device interpreter for applications.

