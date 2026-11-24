# MiniOS 2.0 — C kernel prototype

This branch (`v2-protected-c`) is the protected-mode / C rewrite. It does not replace
`main` (MiniOS 1.0, the real-mode assembly OS) yet.

## What works

- 16-bit boot sector loads 64 sectors to `0x10000`
- A20 enable, flat GDT, 32-bit protected mode
- Freestanding C kernel compiled with clang (`i386-unknown-none-elf`)
- 80x25 VGA text output at `0xB8000`
- CPUID vendor string
- Scancode Set 1 keyboard polling (no IDT yet)
- Exact 1.44 MB image: `MiniOS-C.img`

Screenshot: [`../screenshots/c-kernel-keyboard.png`](../screenshots/c-kernel-keyboard.png)

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

- `c/build/boot.bin`
- `c/build/kernel.bin` (currently ~3.2 KB)
- `MiniOS-C.img` (1,474,560 bytes)

## Run

```powershell
qemu-system-i386 `
  -drive "file=MiniOS-C.img,if=floppy,format=raw,readonly=on" `
  -boot a -m 128
```

## Next step: apps without a compiler

The point of C here is **not** to make every application compile before running.
C is the kernel/interpreter substrate. The planned application layer is an
on-device interpreted language (working name **MiniBASIC**):

- `PRINT`, `LET`, `IF`, `GOTO`, `FOR`, `INPUT`, `CLS`
- drawing/window primitives exposed as built-in statements
- programs are text entered or stored on disk, run directly by the interpreter

So the split is:

- assembly: boot / GDT / IDT entries / context switches
- C: kernel, drivers, filesystem, GUI, interpreter
- MiniBASIC: user programs and demos, no host compile step

## Status / caveats

- IRQs are off; keyboard is polled. No IDT/PIT yet.
- No filesystem, heap, shell or GUI in the C kernel yet.
- This is a bootable prototype, not a replacement for `v1.0.0`.
