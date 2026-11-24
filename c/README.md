# MiniOS 2.0 — C kernel, GUI, Ring 3, preemptive tasks, program pack and MiniFS

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
- **Runtime FDC floppy driver**: IRQ6 on the master PIC (`0x26`), 8237 DMA
  channel 2, CHS reads of 512-byte sectors from the 1.44 MB image
- **Read-only MiniFS at LBA 200**: `ls` reads the directory from floppy,
  `exec NAME` reads file sectors through the FDC and creates a Ring 3 task
- `disktest` reads the boot sector at runtime and verifies `0x55AA`
- **Kernel-module ABI v1 for MicroPython bring-up**:
  - MiniFS module loaded at `0x00100000` (up to 1 MB)
  - fixed 1 MB GC heap at `0x00200000`
  - `MPM1` header (entry/image/BSS/heap), BSS zeroing
  - API: puts, putc, write(len), getchar, ticks_ms, heap
  - test command `modtest` / `mod NAME`
- **Ring 3 large-module layout**:
  - `UMP1` code at `0x400000` (256 KB), stack at `0x480000` (64 KB),
    heap at `0x500000` (1 MB)
  - one big task slot (slot 3)
  - syscalls: write bytes (4), blocking getchar (5), ticks_ms (6),
    whole-file read (7), plus write-string/exit/yield
  - test `utest` loads `UMTEST.BIN` from MiniFS and verifies multi-page code,
    heap, console input, time and file read

Screenshots:

- [`../screenshots/c-gui-boot.png`](../screenshots/c-gui-boot.png)
- [`../screenshots/c-preempt-final.png`](../screenshots/c-preempt-final.png)
- [`../screenshots/c-pack3-progs.png`](../screenshots/c-pack3-progs.png)
- [`../screenshots/c-pack3-tasks.png`](../screenshots/c-pack3-tasks.png)
- [`../screenshots/c-pack-run.png`](../screenshots/c-pack-run.png)
- [`../screenshots/c-fdc-test3.png`](../screenshots/c-fdc-test3.png)
- [`../screenshots/c-minifs-ls.png`](../screenshots/c-minifs-ls.png)
- [`../screenshots/c-minifs-exec.png`](../screenshots/c-minifs-exec.png)
- [`../screenshots/c-minifs-clean.png`](../screenshots/c-minifs-clean.png)
- [`../screenshots/c-ring3-cpl.png`](../screenshots/c-ring3-cpl.png)
- [`../screenshots/c-ring3-return.png`](../screenshots/c-ring3-return.png)
- [`../screenshots/c-modtest2.png`](../screenshots/c-modtest2.png)
- [`../screenshots/c-utest2-done.png`](../screenshots/c-utest2-done.png)
- [`../screenshots/c-user-abi-regression.png`](../screenshots/c-user-abi-regression.png)

## Program pack

The image layout is:

| Offset | Contents |
|---:|---|
| `0` | 512-byte boot sector |
| `512` | kernel, up to 64 sectors (32 KB) |
| `33280` | MPK1 program pack, 64 sectors (32 KB) |
| `102400` | read-only MiniFS (LBA 200), directory + data sectors |

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

The program pack is **boot-loaded**. The separate MiniFS below is read from
the floppy at runtime.

## Runtime MiniFS

At LBA 200 the image contains a tiny read-only filesystem:

```text
magic "MFS1" | u16 count | u16 reserved
count × { char name[16]; u32 start_lba; u32 size; }
file data sectors
```

`c/build_c.py` packs `DISK.BIN` (a Ring 3 program) and `HELLO.BIN`.
`ls` reads the directory sector through the FDC; `exec NAME` reads the file
sectors, copies the flat program into a clean task code page and starts it.

Shell:

```text
disktest          # read LBA 0, verify 0x55AA
ls                # list MiniFS files from floppy
exec DISK.BIN     # load and run a Ring 3 task from floppy
exec HELLO.BIN
```

This is a custom read-only filesystem, not FAT12. There is no write support.

## MicroPython bring-up ABI

The first MicroPython target is a **kernel-mode module**, not a Ring 3 task:

- module linked at `0x00100000`, loaded from MiniFS via the FDC at runtime
- module header `MPM1` + BSS size + heap request
- API table passed to `entry(struct minios_api *)`
- 1 MB static heap at `0x00200000`
- console input/output and PIT milliseconds through the API
- test module: `MODTEST.BIN`, command `modtest`

Docs:

- [`../docs/micropython-feasibility.md`](../docs/micropython-feasibility.md)
- [`../docs/micropython-module-abi.md`](../docs/micropython-module-abi.md)
- [`../docs/micropython-user-abi.md`](../docs/micropython-user-abi.md)
- [`../c/module_api.h`](../c/module_api.h)

The kernel-module path is Ring 0: a crash kills the OS. The Ring 3 big-task
path (`utest`) has a 1 MB user heap and test syscalls; it is the target for the
user-mode MicroPython build.

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
`echo`, `sysinfo`, `about`, `progs`, `run NAME`, `tasks`, `ls`, `exec NAME`,
`disktest`, `ring3`, `modtest`, `mod NAME`, `utest`, `reboot`, `halt`.

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
- `c/build/kernel.bin` — currently ~18.7 KB
- `c/build/user.bin` — single Ring 3 demo
- `c/build/task_a.bin`, `task_b.bin`, `task_c.bin` — pack programs
- `c/build/disk_task.bin` — MiniFS disk task
- `c/build/modtest.bin` — kernel-module ABI test
- `c/build/umtest.bin` — large Ring 3 user-module test
- `MiniOS-C.img` — 1,474,560 bytes (pack at 33280, MiniFS at 102400)

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
| `interrupts.asm` | IRQ0/IRQ1/IRQ6/IRQ12 stubs + `int 0x80` syscall stub |
| `usermode.asm` | Ring 3 entry and exit-to-kernel context restore |
| `sched.asm` | scheduler start/exit, per-task `switch_to` |
| `user.asm` | single Ring 3 demo, linked at `0x400000` |
| `user_blob.asm` | embeds `user.bin` for the `ring3` command |
| `task_a.asm`, `task_b.asm`, `task_c.asm` | pack task programs |
| `disk_task.asm` | Ring 3 task stored in MiniFS |
| `modtest.asm` | kernel-module ABI test at `0x100000` |
| `umtest.asm` | large Ring 3 user-module test at `0x400000` |
| `module_api.h` | MicroPython module header/API layout |
| `../docs/micropython-user-abi.md` | Ring 3 memory layout and syscall ABI |
| `fdc.c`, `fdc.h` | IRQ6/DMA/CHS floppy sector driver |
| `kernel.c` | IDT/PIC/PIT, keyboard/mouse, GDT/TSS, paging, tasks, pack, MiniFS, module loader, syscalls |
| `../docs/micropython-feasibility.md` | MicroPython feasibility, gaps and handoff checklist |
| `../docs/micropython-module-abi.md` | exact module/API ABI |
| `gui.c` | VGA framebuffer, font, windows, shell, mouse UI |
| `gui.h` | GUI API and kernel/GUI globals |
| `linker.ld` | kernel at `0x10000`, BSS NOLOAD |

## Status / caveats

- Ring 1/2 unused (normal; only Ring 0/3 matter here).
- Dynamic table has **4 slots**; `tasks` uses three, `run` uses one.
- Scheduler is timer-preemptive with a fixed 10 ms slice; no priorities/sleep/wait.
- Task code/stack physical pages are statically reserved per slot.
- Program pack is read-only and boot-loaded; MiniFS is read-only at runtime.
- MiniFS is custom, not FAT12; no write support or directory updates.
- FDC driver reads one 512-byte sector per command (seeks between tracks).
- `exec` supports flat programs ≤4 KB; no arguments, fork/exec or per-task heap.
- Kernel module loader runs in **Ring 0**; big Ring 3 task has real user isolation.
- Big Ring 3 task: one slot, fixed 256 KB code / 64 KB stack / 1 MB heap.
- File API is whole-file read only (syscall 7); no open/read/seek/write.
- Kernel module (`mod`) and big user task (`utest`) share physical `0x100000`;
  run one at a time.
- Single `ring3` demo still uses a separate page-directory scheme.
- Boot loader loads 64 kernel + 64 pack sectors; MiniFS is read on demand.

## Next milestones

1. Build the minimal i386 MicroPython port against `micropython-user-abi.md`
   (or the kernel-module ABI for a faster Ring 0 bring-up).
2. VFS/import API and writable temp (optional).
3. More than four tasks / more big-task slots, priorities and blocking.
4. Per-task frame allocator and demand paging.
5. Write support / FAT12 compatibility.
