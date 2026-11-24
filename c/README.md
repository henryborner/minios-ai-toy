# MiniOS 2.0 — C kernel, GUI, Ring 3, preemptive tasks and program pack

This branch (`v2-protected-c`) is the protected-mode / C rewrite. It does not replace
`main` (MiniOS 1.0, the real-mode assembly OS) yet.

## What works

- 16-bit boot sector loads the kernel to `0x10000`, the program pack to `0x70000`,
  enables A20, flat protected mode and VGA mode 13h
- Freestanding C kernel, clang + `ld.lld`
- VGA 320×200×256 GUI, custom palette, ROM font
- IDT/PIC/PIT: IRQ0 timer 100 Hz, IRQ1 keyboard, IRQ12 PS/2 mouse
- Mouse cursor, Start menu, taskbar, draggable windows, close/OK buttons
- Console shell, System Information window, About dialog
- 4 KB paging; kernel pages supervisor, user pages U/S
- GDT with DPL3 user code/data descriptors and a TSS
- Ring 3 entry via `iret`; DPL3 `int 0x80` syscall gate; CPL=3 verified
- **Dynamic task table with 4 slots**, task creation/reuse and per-task
  page directories/user page tables/kernel stacks
- **Timer-preemptive round-robin** scheduling: IRQ0 every 10 ms saves the user
  trap frame, switches CR3/TSS `esp0` and `iretd`s into the next task
- **Read-only program pack (MPK1)** loaded by the boot sector:
  `A.BIN`, `B.BIN`, `C.BIN`, `HELLO.BIN`
- Shell `progs` lists pack programs; `run NAME` creates and runs one task;
  `tasks` creates and preempts three tasks from the pack

Screenshots:

- [`../screenshots/c-gui-boot.png`](../screenshots/c-gui-boot.png)
- [`../screenshots/c-preempt-final.png`](../screenshots/c-preempt-final.png)
- [`../screenshots/c-pack3-progs.png`](../screenshots/c-pack3-progs.png)
- [`../screenshots/c-pack3-tasks.png`](../screenshots/c-pack3-tasks.png)
- [`../screenshots/c-pack-run.png`](../screenshots/c-pack-run.png)
- [`../screenshots/c-ring3-cpl.png`](../screenshots/c-ring3-cpl.png)
- [`../screenshots/c-ring3-return.png`](../screenshots/c-ring3-return.png)

## Program pack

The image layout is:

| Offset | Contents |
|---:|---|
| `0` | 512-byte boot sector |
| `512` | kernel, up to 64 sectors (32 KB) |
| `33280` | MPK1 program pack, 64 sectors (32 KB) |

Pack format:

```text
magic "MPK1" | u16 count | u16 reserved
count × { char name[16]; u32 offset; u32 size; }
data...
```

`c/build_c.py` creates the pack from `task_a.bin`, `task_b.bin`,
`task_c.bin` and `user.bin`, then writes it at offset 33280. The boot sector
loads it to `0x70000`; the kernel parses and copies programs into task pages.

Shell examples:

```text
progs
run HELLO.BIN
tasks
```

This is a **boot-loaded read-only pack**, not a runtime floppy filesystem yet.
There is no FDC/FAT12 driver; programs are loaded from the in-memory pack.

## Preemptive task demo

`tasks` creates A, B and C from the pack, each in its own page directory.
All three run at virtual `0x400000` but map to different physical code pages.
The timer interrupt switches between them without any `yield`:

```text
Task C: step 1
Task A: step 1
Task B: step 1
Task C: step 2
Task B: step 2
Task C: step 3
Task A: step 3
Task C: step 4
Task A: step 4
Task B: step 4
[task] exit; scheduler.
[task] exit; scheduler.
[task] exit; scheduler.
```

When all tasks exit, the scheduler restores the kernel stack and returns to
the Shell. `tasks` and `run` can be repeated; slots are reused.

## Ring 3 demo

```text
ring3
```

The single Ring 3 demo still uses an embedded `user.bin`, a separate page
directory and a saved kernel ESP. It exits back to the GUI shell.

## Controls

| Input | Action |
|---|---|
| Mouse | click, drag window titles, close buttons, Start menu |
| `F1` / `F2` / `F3` | About / System / Console |
| `Esc` | close About, System, menu |
| Keyboard | Console shell input |

Shell commands: `help`, `ver`, `cpu`, `mem`, `time`, `date`, `uptime`, `clear`,
`echo`, `sysinfo`, `about`, `progs`, `run NAME`, `tasks`, `ring3`, `reboot`, `halt`.

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
- `c/build/kernel.bin` — currently ~13.8 KB
- `c/build/user.bin` — single Ring 3 demo
- `c/build/task_a.bin`, `task_b.bin`, `task_c.bin` — pack programs
- `MiniOS-C.img` — 1,474,560 bytes (pack at offset 33280)

## Run

```powershell
qemu-system-i386 `
  -drive "file=MiniOS-C.img,if=floppy,format=raw,readonly=on" `
  -boot a -m 128 -rtc base=localtime
```

## Source layout

| File | Role |
|---|---|
| `boot.asm` | real-mode loader: kernel + pack, A20, flat GDT, protected mode, mode 13h |
| `entry.asm` | 32-bit entry, stack, BSS zero, `kmain` |
| `interrupts.asm` | IRQ0/IRQ1/IRQ12 stubs + `int 0x80` syscall stub |
| `usermode.asm` | Ring 3 entry and exit-to-kernel context restore |
| `sched.asm` | scheduler start/exit, per-task `switch_to` |
| `user.asm` | single Ring 3 demo, linked at `0x400000` |
| `user_blob.asm` | embeds `user.bin` for the `ring3` command |
| `task_a.asm`, `task_b.asm`, `task_c.asm` | pack task programs |
| `kernel.c` | IDT/PIC/PIT, keyboard/mouse, GDT/TSS, paging, tasks, pack, syscalls |
| `gui.c` | VGA framebuffer, font, windows, shell, mouse UI |
| `gui.h` | GUI API and kernel/GUI globals |
| `linker.ld` | kernel at `0x10000`, BSS NOLOAD |

## Status / caveats

- Ring 1/2 unused (normal; only Ring 0/3 matter here).
- Dynamic table has **4 slots**; `tasks` uses three, `run` uses one.
- Scheduler is timer-preemptive with a fixed 10 ms slice; no priorities/sleep/wait.
- Task code/stack physical pages are statically reserved per slot.
- Program pack is read-only and boot-loaded; no runtime filesystem/FDC driver.
- No fork/exec, arguments, or per-task heap.
- Single `ring3` demo still uses a separate page-directory scheme.
- Boot loader loads 64 kernel sectors + 64 pack sectors; all within 1.44 MB.

## Next milestones

1. Runtime floppy filesystem (FDC/FAT12 or MiniFS) and dynamic `exec`.
2. More than four tasks, priorities and blocking.
3. More syscalls (keyboard input, window/VGA drawing, file I/O).
4. Per-task heap and cleanup.
5. On-device interpreter for applications.
