# MiniOS 2.0 — C kernel, GUI, Ring 3 and cooperative tasks

This branch (`v2-protected-c`) is the protected-mode / C rewrite. It does not replace
`main` (MiniOS 1.0, the real-mode assembly OS) yet.

## What works

- 16-bit boot sector loads the kernel to `0x10000`, enables A20, flat protected mode
- Freestanding C kernel, clang + `ld.lld`
- VGA mode 13h 320×200×256 GUI, custom palette, ROM font
- IDT/PIC/PIT: IRQ0 timer 100 Hz, IRQ1 keyboard, IRQ12 PS/2 mouse
- Mouse cursor, Start menu, taskbar, draggable windows, close/OK buttons
- Console shell, System Information window, About dialog
- 4 KB paging; kernel pages supervisor, user pages U/S
- GDT with DPL3 user code/data descriptors and a TSS
- Ring 3 programs entered via `iret`; `int 0x80` DPL3 syscall gate
- Kernel verifies the saved CS of the syscall frame and prints `CPL=3`
- Syscall 2 returns from Ring 3 to the GUI shell (saved kernel ESP)
- **Two cooperative Ring 3 tasks**
  - each task has its own page directory and user page tables
  - virtual `0x400000` maps to a different physical code page per task
  - each task has its own kernel stack; TSS `esp0` is switched per task
  - syscall 3 = yield, syscall 2 = exit; round-robin on yield
- HLT idle loop

Screenshots:

- [`../screenshots/c-gui-boot.png`](../screenshots/c-gui-boot.png)
- [`../screenshots/c-gui-startmenu.png`](../screenshots/c-gui-startmenu.png)
- [`../screenshots/c-gui-system.png`](../screenshots/c-gui-system.png)
- [`../screenshots/c-gui-about.png`](../screenshots/c-gui-about.png)
- [`../screenshots/c-ring3-cpl.png`](../screenshots/c-ring3-cpl.png)
- [`../screenshots/c-ring3-return.png`](../screenshots/c-ring3-return.png)
- [`../screenshots/c-tasks.png`](../screenshots/c-tasks.png)
- [`../screenshots/c-tasks-twice.png`](../screenshots/c-tasks-twice.png)
- [`../screenshots/c-tasks-then-ver.png`](../screenshots/c-tasks-then-ver.png)

## Ring 3 demo

At the shell prompt type:

```text
ring3
```

The kernel installs the GDT/TSS, enables paging, installs the DPL3
`int 0x80` gate, copies the embedded user binary and `iret`s to CPL3.
The user program prints a message and exits; the saved kernel ESP is restored
and the GUI shell continues.

## Cooperative task demo

At the shell prompt type:

```text
tasks
```

Two user programs run round-robin:

```text
Task A: step 1
Task B: step 1
Task A: step 2
Task B: step 2
Task A: step 3
[task] exit; scheduler.
Task B: step 3
[task] exit; scheduler.
```

Each task has a separate `task_pd[]`/`task_user_pt[]`. Both run at virtual
`0x400000`, but map to different physical code pages. Context switching saves
the syscall trap frame on the task's kernel stack, switches CR3 and TSS `esp0`,
and `iretd`s into the next task. When both exit, the scheduler restores the
kernel stack and returns to the shell.

`tasks` can be run repeatedly; `ring3` also works afterwards.

## Controls

| Input | Action |
|---|---|
| Mouse | click, drag window titles, close buttons, Start menu |
| `F1` / `F2` / `F3` | About / System / Console |
| `Esc` | close About, System, menu |
| Keyboard | Console shell input |

Shell commands: `help`, `ver`, `cpu`, `mem`, `time`, `date`, `uptime`, `clear`,
`echo`, `sysinfo`, `about`, `ring3`, `tasks`, `reboot`, `halt`.

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
- `c/build/kernel.bin` — currently ~12.5 KB
- `c/build/user.bin` — single Ring 3 demo
- `c/build/task_a.bin`, `c/build/task_b.bin` — cooperative tasks
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
| `usermode.asm` | Ring 3 entry and exit-to-kernel context restore |
| `sched.asm` | scheduler start/exit, per-task `switch_to` |
| `user.asm` | single Ring 3 demo, linked at `0x400000` |
| `user_blob.asm` | embeds `user.bin` into the kernel image |
| `task_a.asm`, `task_b.asm` | the two cooperative user programs |
| `task_blob.asm` | embeds both task binaries |
| `kernel.c` | IDT/PIC/PIT, keyboard/mouse, GDT/TSS, paging, tasks, syscalls |
| `gui.c` | VGA framebuffer, font, windows, shell, mouse UI |
| `gui.h` | GUI API and kernel/GUI globals |
| `linker.ld` | kernel at `0x10000`, BSS NOLOAD |

## Status / caveats

- Ring 1/2 unused (normal; only Ring 0/3 matter here).
- Each task has its own page directory; the kernel 0–4 MB mapping is shared.
- The scheduler is **cooperative** (yield on syscall 3), not timer-preemptive.
- Two static tasks; no fork/exec, no blocking/sleep, no priority.
- User programs are embedded in the kernel image; no filesystem yet.
- Single `ring3` demo uses a different page directory from the task system.
- Static 64 KB heap; no general allocator.
- Boot loader loads 64 floppy sectors (32 KB); kernel file is ~12.5 KB.

## Next milestones

1. Timer-preemptive scheduling and a proper task table.
2. Filesystem + loadable user programs.
3. Per-task heap and more syscalls (VGA/window drawing, file I/O, keyboard input).
4. Process exit/wait semantics and task cleanup.
5. On-device interpreter for applications.
