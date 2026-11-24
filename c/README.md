# MiniOS 2.0 — C kernel + GUI prototype

This branch (`v2-protected-c`) is the protected-mode / C rewrite. It does not replace
`main` (MiniOS 1.0, the real-mode assembly OS) yet.

## What works

- 16-bit boot sector loads the kernel to `0x10000`, enables A20 and enters
  flat 32-bit protected mode
- Freestanding C kernel compiled with clang (`i386-unknown-none-elf`)
- VGA mode 13h, 320×200×256, custom 16-colour palette
- VGA ROM 8×8 font, 1× and 2× scaling
- IDT/PIC/PIT: IRQ0 timer at 100 Hz, IRQ1 keyboard, IRQ12 PS/2 mouse
- PS/2 mouse: 3-byte packets, cursor, click, title dragging, close buttons
- Graphical desktop, taskbar, Start menu, clock
- Console window with C-shell commands
- System Information window, About dialog (OK button)
- HLT idle loop
- Exact 1.44 MB image: `MiniOS-C.img`

Screenshots:

- [`../screenshots/c-gui-boot.png`](../screenshots/c-gui-boot.png)
- [`../screenshots/c-gui-help.png`](../screenshots/c-gui-help.png)
- [`../screenshots/c-gui-startmenu.png`](../screenshots/c-gui-startmenu.png)
- [`../screenshots/c-gui-system.png`](../screenshots/c-gui-system.png)
- [`../screenshots/c-gui-about.png`](../screenshots/c-gui-about.png)

## Controls

| Input | Action |
|---|---|
| Mouse click `Start` | open/close the Start menu |
| Click menu items | System / Console / About / Reboot |
| Click taskbar `Console` / `System` | reopen window |
| Drag a title bar | move window |
| Click `x` | close window |
| Click `OK` in About | close dialog |
| `F1` / `F2` / `F3` | About / System / Console |
| `Esc` | close About, then System, then menu |
| Keyboard | Console shell input |

## Shell commands

`help`, `ver`, `cpu`, `mem`, `time`, `date`, `uptime`, `clear`, `echo`,
`sysinfo`, `about`, `reboot`, `halt`.

## Build

Requirements:

- NASM (`tools/nasm-2.16.03/` or on PATH)
- clang with the i386 target
- `ld.lld` (MSYS2: `pacman -S mingw-w64-ucrt-x86_64-lld`)
- `objcopy`

```powershell
python c/build_c.py
```

Outputs:

- `c/build/boot.bin` — 512 bytes
- `c/build/kernel.bin` — currently ~10 KB
- `MiniOS-C.img` — 1,474,560 bytes

## Run

```powershell
qemu-system-i386 `
  -drive "file=MiniOS-C.img,if=floppy,format=raw,readonly=on" `
  -boot a -m 128 -rtc base=localtime
```

## Language split

- assembly: `boot.asm`, `entry.asm`, `interrupts.asm`
- C: kernel, IDT/PIC/PIT, keyboard/mouse, VGA framebuffer, GUI, shell
- later: on-device interpreter for applications

## Next milestones

1. Window z-order/focus and taskbar state
2. Filesystem and loadable programs
3. Memory map/heap allocator beyond the static 64 KB
4. Port remaining v1 OS behaviours and polish the GUI
5. On-device MiniBASIC / Python-subset interpreter

## Status / caveats

- No paging; flat 4 GB segments, static 64 KB heap.
- Boot loader loads 64 floppy sectors (32 KB max); the C kernel is currently
  around 10 KB, so there is room.
- No filesystem yet; shell commands are built-in.
- This is a prototype, not a replacement for `v1.0.0`.
