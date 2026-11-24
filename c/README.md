# MiniOS 2.0 — C kernel prototype

This branch (`v2-protected-c`) is the protected-mode / C rewrite. It does not replace
`main` (MiniOS 1.0, the real-mode assembly OS) yet.

## What works

- 16-bit boot sector loads 64 sectors to `0x10000`
- A20 enable, flat GDT, 32-bit protected mode
- Freestanding C kernel compiled with clang (`i386-unknown-none-elf`)
- 80x25 VGA text output at `0xB8000`, hardware cursor
- IDT with 256 entries and dedicated IRQ0/IRQ1 stubs
- PIC remapped to vectors `0x20` / `0x21`
- PIT channel 0 at 100 Hz, uptime counter
- PS/2 keyboard on IRQ1, Shift handling, 64-byte ring buffer
- HLT idle loop (no busy waiting)
- Shell commands: `HELP`, `VER`, `CPU`, `MEM`, `UPTIME`, `CLEAR`, `ECHO`,
  `ABOUT`, `REBOOT`, `HALT`
- CPUID vendor string, 64 KB static kernel heap
- Exact 1.44 MB image: `MiniOS-C.img`

Screenshots:

- [`../screenshots/c-kernel-irq-shell-boot.png`](../screenshots/c-kernel-irq-shell-boot.png)
- [`../screenshots/c-kernel-irq-shell-help.png`](../screenshots/c-kernel-irq-shell-help.png)
- [`../screenshots/c-kernel-irq-shell-mem.png`](../screenshots/c-kernel-irq-shell-mem.png)

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
- `c/build/kernel.bin` — currently ~3.9 KB
- `MiniOS-C.img` — 1,474,560 bytes

## Run

```powershell
qemu-system-i386 `
  -drive "file=MiniOS-C.img,if=floppy,format=raw,readonly=on" `
  -boot a -m 128
```

## Language split

- assembly: `boot.asm`, `entry.asm`, `interrupts.asm` (boot, BSS, IRQ stubs)
- C: IDT/PIC/PIT, keyboard, VGA, shell, heap, future filesystem/GUI
- later: on-device interpreter for applications

## Next milestones

1. VGA mode 13h graphics + double buffer
2. PS/2 mouse on IRQ12 + cursor + window system
3. Memory map/heap allocator and simple filesystem
4. Port the v1 GUI and shell commands to C
5. On-device MiniBASIC / Python-subset interpreter

## Status / caveats

- Text-mode only; no GUI in the C kernel yet.
- No paging; flat 4 GB segments, static 64 KB heap.
- 16-bit boot loader currently loads 64 floppy sectors (32 KB max).
- This is a bootable prototype, not a replacement for `v1.0.0`.
