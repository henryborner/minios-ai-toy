# MiniOS 2.0 — C kernel notes

Branch: `v2-protected-c`.

MiniOS is a 32-bit protected-mode hobby OS. It boots from one
**1,474,560-byte** floppy image and runs a VBE desktop, a persistent kernel
Task 0, and Ring 3 MicroPython processes.

This document describes the current kernel, not the older 64 KB / LBA 200
layout.

## Boot and image layout

The boot sector loads data with BIOS `int 13h` into low memory, then stage2
sets up VBE and enters protected mode.

| Region | LBA | Size / notes |
|---|---:|---|
| Boot sector | `0` | 512 B |
| Kernel staging | `1..256` | 128 KB max, loaded at `0x10000` |
| Program pack | `257..320` | 64 sectors, boot-loaded at `0x70000` |
| Stage 2 | `321` | VBE setup + PM entry; loaded at `0x7E00` |
| MiniFS | `328..2559` | Read-only image files |
| SaveFS A | header `2560`, data `2561..2689` | MSV3 |
| SaveFS B | header `2690`, data `2691..2819` | MSV3 |
| Tail slack | `2820..2879` | Reserved |

`entry.asm` starts at the low staging address, copies 128 KB with
`rep movsd` to `0x300000`, then jumps into the image linked at 3 MB. The
kernel is linked at `0x300000`; the low copy is only a staging trampoline.

The build enforces:

- image exactly `1,474,560` bytes
- `kernel.bin` ≤ `131,072` bytes
- BSS end < `0x400000`
- MPY `.data` page-aligned with a valid `ro_size`

Current build:

```text
kernel:  100,256 B / 131,072 B
BSS end: 0x3a844e
MPY:     208,912 B
image:   1,474,560 B
```

## Kernel and memory map

- 4 KB paging; 32-bit flat segments, DPL3 user descriptors and a TSS.
- Kernel at `0x300000`; low staging at `0x10000`.
- User code at `0x400000`, stack at `0x480000`, heap at `0x500000`.
- Frame pool `5..16 MB`, direct-mapped at `16..28 MB`.
- VBE LFB mapped at `0xE0000000`.
- `sched_safe_pd` and per-task page directories keep the kernel mapped
  supervisor-only.
- Kernel code/data are supervisor; user code/stack/heap user-accessible.
- FPU state is 108 bytes per task and saved/restored with `fsave/frstor`.
  New tasks must not run `fninit`; the MPY entry owns its FPU setup.

## Tasks and scheduler

Task 0 is the persistent GUI/Shell kernel thread. User tasks occupy slots
1–8:

- slots 1–5: small Ring 3 tasks (`task_create`)
- slots 6–8: big MPY-class tasks (`task_create_big`)

Each task owns a page directory, user page table, kernel stack, user
code/data mapping, stack, heap, FPU state and console id.

The scheduler is a kernel-stack context switch, not the old batch
`scheduler_start` path:

```c
void swtch(u32 *old_kesp, u32 new_kesp);
```

- `swtch` saves callee-saved registers and EFLAGS, switches kernel stacks,
  restores them and returns.
- A new task is initialized with a fake switch frame whose return target is
  `task_first_entry`; that function sets user segments, `popa`s the user trap
  frame and `iretd`s.
- `sched_switch(next)` saves/restores FPU, CR3, TSS `esp0` and calls `swtch`.
- Task 0 has its own kstack top (`0x90000`) and uses `sched_safe_pd`.
- The timer time slice switches back to Task 0.
- `next_ready()` uses a round-robin cursor for user slots so earlier big-task
  slots cannot starve later ones.
- IRQ0 no longer re-enters GUI while Task 0 owns it; `runtime_busy` remains a
  special fallback for synchronous module/serial work.

### Blocking input and sleep

`module_getchar()` loops on its console queue and, when empty:

```c
tasks[current].wait_kbd = cid + 1;
tasks[current].state = TASK_BLOCKED;
sched_yield();
```

The kernel stack is preserved by `swtch`. IRQ1 calls `wake_kbd_tasks(cid)`,
which marks matching blocked tasks READY. When the task resumes it continues
inside `module_getchar()` and returns the key through the normal syscall.

`sleep` uses the same blocking path with `sleep_until`; IRQ0 wakes it. There
is no `hlt` busy-wait in either path.

`wait` uses `wait_status`: `wake_parent()` stores the child's exit code and
marks the parent READY; `task_wait_block()` returns it after resuming.

### Console routing

`kernel_task_console()` returns the current user task's console. Task 0
returns -1 so Shell/GUI output follows the focused console. Task output goes
to its own console even while another console has focus.

## GUI and shell

- VBE 640×480×8bpp, VGA 320×200 fallback.\n- Unified content clip rectangle: console/Canvas/Files/System/About contents\n  cannot paint outside their window; menus/cursor reset to full-screen clip.
- Three consoles, ANSI 16-color cells, scrollback, history, line editing,
  clipboard.
- Windows: drag, 8-way resize, minimize/maximize, snap, tile/cascade.
- Desktop icons and Classic/Midnight/Amber themes.
- Task 0 runs the Shell command loop and GUI event loop.\n- Alt+Tab cycles visible windows forward by window id; Alt+Shift+Tab cycles backward without z-order ping-pong. Right Alt 0xE0/0x38 is handled.\n- The taskbar iterates the window table: consoles, Files, Canvas and other\n  visible/minimized windows get dynamic buttons. Overflow is clipped at the clock;\n  the F4 list remains the full window inventory.\n  The keyboard IRQ only queues a special event; GUI focus changes happen in\n  Task 0 context.
- Shell commands that create tasks return after the task is READY; Task 0
  continues processing input and yields again when a user task is ready.
- A pending prompt is deferred while a console has a live task; `task_exit`
  restores `C:\>` when the task ends.
- `py2 A.PY B.PY` and `py3 A.PY B.PY C.PY` create multiple MPY tasks.\n- `jobs` lists PID/console/state/pages/name; `fg PID` focuses/resumes the owning console; `stop PID` stops a task and `bg PID` resumes it, preserving the original blocked reason; `longjob` creates a long-running MPY job for job-control tests; `pybg SCRIPT`/`spawn SCRIPT` creates a new console and launches Python there.\n- `pyn A.PY B.PY ...` grows consoles as needed and launches up to eight MPY tasks.
- `selftest` runs three Canvas open/draw/present/close cycles and writes
  `[selftest] PASS` or FAIL to COM1.

## MiniFS, RAMFS and SaveFS

- MiniFS lives at LBA 328 and is read-only at runtime.
- RAMFS files are dynamic: `struct ramfile` names are 16 bytes; data pages
  are allocated from the frame pool on write.
- SaveFS MSV3 uses A/B slots:
  - A: header `2560`, data `2561..2689`
  - B: header `2690`, data `2691..2819`
  - 129 data sectors per slot (~66 KB)
- Commit order: write inactive data, write target header with generation +
  header CRC, then switch active slot in memory.
- Restore validates header CRC, entry bounds and per-file CRC; it chooses the
  valid slot with the greater generation.
- MSV1/MSV2 single-slot snapshots are imported read-only. The first migration
  to MSV3 is not fully atomic; back up a writable image before upgrading.

`sync` writes a snapshot. `metrics` emits frame/heap counters on COM1.

## MicroPython / UMP1

`MPY.BIN` is linked at `0x400000` and exported as UMP1:

```text
magic "UMP1"
u32 entry_offset
u32 image_size
u32 bss_size
u32 heap_hint
u32 ro_size
```

`ro_size` ends at the page-aligned `.data` start.

At task creation:

- the first task copies the MPY read-only pages into a shared frame block and
  maps them `0x005` (user + read-only);
- later MPY tasks reuse those pages;
- each task allocates only its private `.data/.bss` page(s), 16-page stack and
  heap;
- shared pages are reference-counted and freed with the last user.

Shared pages are valid only for the same image; creating a different image
while users exist is rejected.

## Syscalls and devices

- DPL3 `int 0x80`; CPL=3 is verified.
- Console, file, RAMFS, fd, spawn/wait/exit, serial, canvas, pipe, task-info
  and metrics calls.
- COM1: `serial`, `upload NAME` (Ctrl-Z ends), `download NAME`.
- `tools/push.py` streams a host file to `upload NAME`.
- Four 4 KB kernel pipes; `ls | grep BIN` uses pipe 3 and `futil` reads it.
- PIT 100 Hz, IRQ0 timer, IRQ1 keyboard, IRQ6 FDC, IRQ12 mouse.
- PS/2 IntelliMouse wheel and per-console keyboard queues.

## Build and test

```bash
python c/build_c.py
python tools/smoke_test.py
```

The smoke test:

- boots 640×480 and forced 320×200;
- triggers guest `selftest` and checks COM1 `[selftest] PASS`;
- takes non-blank PPM screendumps;
- patches stage2 at LBA 321 for VGA;
- compares warmed free-frame counts.

It is a regression test, not a cold-start absolute leak proof.

## Known gaps

- Task 0 is kernel C, not a Ring 3 Shell.
- `py2`/`py3` are batch-created prototypes; no general background `&` API.
- Canvas windows are memory-admitted from a growable table; Python big-task slots remain capped at three.
- No ATA/FAT data disk.
- No networking, sound, USB or real-hardware target.
- SaveFS legacy migration has a one-time window.
- Not a secure or production OS.















































































