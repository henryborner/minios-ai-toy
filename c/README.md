# MiniOS 2.0 — C kernel, GUI, Ring 3, preemptive tasks, program pack and MiniFS

This branch (`v2-protected-c`) is the protected-mode / C rewrite. It does not replace
`main` (MiniOS 1.0, the real-mode assembly OS) yet.

## What works

- 16-bit boot sector loads up to **128 kernel sectors (64 KB)** to `0x10000`,
  the program pack (64 sectors at LBA 129) to `0x70000`, enables A20, flat
  protected mode and VGA mode 13h
- Freestanding C kernel, clang + `ld.lld`
- VGA 320×200×256 GUI, custom palette, ROM font
- IDT/PIC/PIT: IRQ0 timer 100 Hz, IRQ1 keyboard, IRQ12 PS/2 mouse
- Mouse cursor, Start menu, taskbar, draggable windows, close/OK buttons
- **Keyboard**: full US Shift symbols, Ctrl combinations (`Ctrl-C`/`Ctrl-D`/
  `Ctrl-L`/`Ctrl-Z`, `Ctrl-[`), extended arrow/Home/End/Delete keys mapped to
  ANSI sequences for `getchar`
- **Terminal**: CR/LF, backspace, TAB expansion and ANSI CSI swallowing
- **CPU exceptions**: `#DE`, `#UD`, `#GP`, `#PF` kill the current Ring 3 task
  and return to the Shell; kernel-mode faults still halt
- **Runtime desktop**: while a task/module runs or blocks in `getchar`, IRQ0
  services mouse packets and per-console keyboard queues; windows/taskbar remain
  usable and a non-owning console can run safe shell commands (`ver`, `mem`,
  `help`, `clear`, `sysinfo`, ...). Dangerous commands return `[busy]`.
- **Stage 0 fast paths**: dirty-batched 50 Hz rendering, RTC cached for 0.5 s,
  pure cursor motion restores only its 12×18 pixels, and FDC reads an entire
  18-sector track into the 0x8000 DMA cache.
- **Console-bound tasks**: each task remembers the console it was launched
  from; its keyboard input and output stay on that console. F1–F6 still switch
  windows while a task runs.
- **COM1 serial**: shell `serial`, `upload NAME` (Ctrl-Z terminates) and
  `download NAME` for host file exchange
- **Window manager**: focus, Z-order, active/inactive title bars, taskbar state
- **Stage 11 complete (ANSI clipboard)**: per-cell 16-color attributes, ANSI SGR rendering, light/dark themes, colored ls/errors, mouse drag-select, and Console Copy/Paste.\n- **Stage 10 complete (WM 2.0)**: title-bar maximize/restore and double-click
  toggle with saved geometry; 8-way border/corner resize with dynamic Console
  columns/rows and scrollbar geometry; resize-shape cursors; Canvas preallocated
  at 600×420 and resized in place with a fixed row stride. Snap, negative-
  coordinate dragging, snap landing and taskbar resource meter remain; taskbar
  click-to-minimize is implemented.
- **Stage 9 input polish**: i8042 keyboard/mouse source demultiplexing,
  IntelliMouse 4-byte packets with wheel scrolling, per-console 128-byte
  command buffers with wrapped-line rendering, and 8-entry Up/Down history.
  `Ctrl-U` clears the line, `Ctrl-L` clears the screen while preserving it.
- **Three independent consoles** with per-console shell history and input focus
- **F4 / taskbar Wins window list**: Tab cycles, Enter activates, mouse click,
  hidden-window markers
- **Button press feedback**: buttons are drawn sunken while held and activate on
  release (Start, taskbar, menu items, close, OK)
- Console shell, System Information window, About dialog
- 4 KB paging; kernel pages supervisor, user pages U/S
- GDT with DPL3 user code/data descriptors and a TSS
- Ring 3 entry via `iret`; DPL3 `int 0x80` syscall gate; CPL=3 verified
- **Dynamic task table with 8 slots**, task creation/reuse and per-task
  page directories/user page tables/kernel stacks
- **Stage 1 ABI**: big Ring 3 code window 512 KB, `run_user_module_args()`
  argc/argv, syscall 4 long writes, `stat`/`readdir`, and `ps`/`kill`
- **Stage 2 MicroPython**: `MPY.BIN` v1.24.1 Ring 3 REPL, `py`/`python`,
  `py -c/-e`, VFS, GC, JSON/math/re, Ctrl-C/Ctrl-D
- **Stage 3 warm cache**: the first launch reads `MPY.BIN` from the FDC and
  caches it in the frame pool; subsequent launches copy from RAM (0 disk I/O).
  Shell `= EXPR` evaluates a one-shot expression and `pystat` reports last
  cold/warm ticks.
- **Stage 4 VFS/terminal**: Python `open()`/`os`/`import`, `py SCRIPT.PY`,
  96-line per-console scrollback, native clickable/draggable scrollbar,
  PgUp/PgDn page scrolling, and in-line cursor editing
  (Left/Right/Home/End/Delete + ANSI cursor/clear). Scroll position is
  preserved while new output arrives; typing returns to the live bottom.
- **Native desktop controls**: left-click/drag windows, minimize buttons,
  right-click context menus on the desktop and consoles (scroll/clear/
  minimize/close, launch consoles/system/about), usable while a task runs.
- **Stage 5 SaveFS persistence**: `sync` / Python `os.sync()` writes a
  CRC32-checked RAMFS snapshot to LBA 2560–2819; boot restores it
  automatically. MiniFS remains read-only.
- **Stage 6 Canvas/minios**: 256-color palette, draggable 180×120 Canvas
  window, Python `minios.Canvas` with clear/pixel/rect/line/text/event,
  `minios.sysinfo()` and PC-speaker beep; syscalls 24–28.
- **Dynamic Canvas buffer**: the pixel buffer (`300×200` = 15 pages in VBE
  `640×480`, `180×120` = 6 pages in VGA `320×200`) is allocated on demand from
  the frame pool through `kernel_alloc_pages_zero()`.
- **Stage 8 VBE `640×480×8bpp` HD desktop + seamless `320×200` fallback**:
  - `stage2.asm` (loaded at `0x7E00` from LBA 193, outside the `0x8000` FDC DMA
    buffer) negotiates VBE 2.0 `640×480×8bpp` LFB via BIOS `int 0x10`
    (`0x4F00/0x4F01/0x4F02`) and writes a 16-byte handoff descriptor at
    `0x00000500` (`"VBE1"`), or automatically falls back to VGA Mode `0x13`
    (`320×200×256`, `"VGA1"`) if VBE LFB is absent, `Shift` is held at boot, or
    `force_vga=1`.
  - Early `gdt_install()` in `kmain()` locks in the permanent kernel GDT/TSS
    before interrupts/DMA start; `vbe_init()` maps the physical LFB
    (`0xFD000000` on QEMU `stdvga`) via a 4 MB PSE supervisor PDE at
    `0xE0000000` across `sched_safe_pd`, `task_pd[]` and `page_directory`.
  - Native adaptive GUI: `72×26` consoles (`80×96` scrollback), `300×200`
    256-color Canvas window, taskbar `Con3`/`Canvas` buttons, dynamic PS/2 mouse
    bounds, and pure LFB blitting (never touching the `0xA0000` alias window in
    VBE mode). Kernel `.bss` ends at `0x0005EAF3` (~70.9 KB below `0x70000`).
- **Stage 7–8 frozen suite**: `calc`, `top`, `demos` (`plot`), `xxd`, `edit`,
  `futil`, and `bench` are frozen to `.mpy` bytecode with `mpy-cross 1.24.1`,
  linked into `MPY.BIN` (`189,616` bytes), and launched via Shell shortcuts with
  `< 10 ms` (0-tick) warm-cache startup.
- **Dynamic physical frame allocator** for code/stack/heap pages:
  frames are taken from `5–16 MB`, direct-mapped at `16–28 MB`
- **Process lifecycle**: PIDs, parent/child, `spawn`, `spawn_args`, `wait`,
  `sleep`, `exit(status)`, blocking task states, timer wakeups and frame
  reclamation
- **User heap `brk`**: heap pages are mapped/freed on demand at
  `0x500000–0x700000`; the initial big-task heap is 1 MB
- **Per-task file descriptors**: `open/read/close/seek/write`, 8 FDs per task
- **Writable RAMFS**: 8 in-memory files (16 KB each), `create/write/unlink`,
  in addition to the read-only MiniFS; RAMFS is non-persistent
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
  - `UMP1` code at `0x400000` (512 KB), stack at `0x480000` (64 KB),
    heap at `0x500000` (1 MB initial, `brk` up to 2 MB)
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
- [`../screenshots/c-wm-con2.png`](../screenshots/c-wm-con2.png)
- [`../screenshots/c-wm-list-sel.png`](../screenshots/c-wm-list-sel.png)
- [`../screenshots/c-wm-about.png`](../screenshots/c-wm-about.png)
- [`../screenshots/c-wm-start-pressed2.png`](../screenshots/c-wm-start-pressed2.png)
- [`../screenshots/c-wm-close-pressed-zoom.png`](../screenshots/c-wm-close-pressed-zoom.png)
- [`../screenshots/c-frames-tasks.png`](../screenshots/c-frames-tasks.png)
- [`../screenshots/c-proc-parent.png`](../screenshots/c-proc-parent.png)
- [`../screenshots/c-proc-wait.png`](../screenshots/c-proc-wait.png)
- [`../screenshots/c-proc-status.png`](../screenshots/c-proc-status.png)
- [`../screenshots/c-proc-utest.png`](../screenshots/c-proc-utest.png)
- [`../screenshots/c-fd-test.png`](../screenshots/c-fd-test.png)
- [`../screenshots/c-ramfs-test.png`](../screenshots/c-ramfs-test.png)
- [`../screenshots/c-ramfs-ls.png`](../screenshots/c-ramfs-ls.png)
- [`../screenshots/c-argv-test.png`](../screenshots/c-argv-test.png)
- [`../screenshots/c-brk-test.png`](../screenshots/c-brk-test.png)
- [`../screenshots/c-term-test.png`](../screenshots/c-term-test.png)
- [`../screenshots/c-dma-fd.png`](../screenshots/c-dma-fd.png)
- [`../screenshots/c-keyboard-symbols.png`](../screenshots/c-keyboard-symbols.png)
- [`../screenshots/c-loader-progs.png`](../screenshots/c-loader-progs.png)
- [`../screenshots/c-exc-pf.png`](../screenshots/c-exc-pf.png)
- [`../screenshots/c-exc-div0.png`](../screenshots/c-exc-div0.png)
- [`../screenshots/c-runtime-menu.png`](../screenshots/c-runtime-menu.png)
- [`../screenshots/c-runtime-shell.png`](../screenshots/c-runtime-shell.png)
- [`../screenshots/c-runtime-tasks-mem.png`](../screenshots/c-runtime-tasks-mem.png)
- [`../screenshots/c-kbdroute-console2.png`](../screenshots/c-kbdroute-console2.png)
- [`../screenshots/c-stage0-time.png`](../screenshots/c-stage0-time.png)
- [`../screenshots/c-stage0-cursor-fresh.png`](../screenshots/c-stage0-cursor-fresh.png)
- [`../screenshots/c-stage0-track-utest.png`](../screenshots/c-stage0-track-utest.png)
- [`../screenshots/c-stage1-uargs.png`](../screenshots/c-stage1-uargs.png)
- [`../screenshots/c-stage1-bigwrite.png`](../screenshots/c-stage1-bigwrite.png)
- [`../screenshots/c-stage1-dir2.png`](../screenshots/c-stage1-dir2.png)
- [`../screenshots/c-stage1-ps.png`](../screenshots/c-stage1-ps.png)
- [`../screenshots/c-stage1-kill-con1.png`](../screenshots/c-stage1-kill-con1.png)
- [`../screenshots/c-stage1-big512.png`](../screenshots/c-stage1-big512.png)
- [`../screenshots/c-stage3-cold.png`](../screenshots/c-stage3-cold.png)
- [`../screenshots/c-stage3-warm.png`](../screenshots/c-stage3-warm.png)
- [`../screenshots/c-stage3-equals.png`](../screenshots/c-stage3-equals.png)
- [`../screenshots/c-verify-ctrlc-fixed.png`](../screenshots/c-verify-ctrlc-fixed.png)
- [`../screenshots/c-stage4-vfs.png`](../screenshots/c-stage4-vfs.png)
- [`../screenshots/c-stage4-import.png`](../screenshots/c-stage4-import.png)
- [`../screenshots/c-stage4-scroll-up2.png`](../screenshots/c-stage4-scroll-up2.png)
- [`../screenshots/c-stage4-lineedit2.png`](../screenshots/c-stage4-lineedit2.png)
- [`../screenshots/c-stage4-pytrace-scroll.png`](../screenshots/c-stage4-pytrace-scroll.png)
- [`../screenshots/c-scrollbar-click-top.png`](../screenshots/c-scrollbar-click-top.png)
- [`../screenshots/c-scrollbar-drag-bottom.png`](../screenshots/c-scrollbar-drag-bottom.png)
- [`../screenshots/c-scrollbar-task.png`](../screenshots/c-scrollbar-task.png)
- [`../screenshots/c-gui-context-wide.png`](../screenshots/c-gui-context-wide.png)
- [`../screenshots/c-gui-context-desktop.png`](../screenshots/c-gui-context-desktop.png)
- [`../screenshots/c-gui-context-task.png`](../screenshots/c-gui-context-task.png)
- [`../screenshots/c-gui-minimized.png`](../screenshots/c-gui-minimized.png)
- [`../screenshots/c-gui-minimized-list.png`](../screenshots/c-gui-minimized-list.png)
- [`../screenshots/c-savefs-osync.png`](../screenshots/c-savefs-osync.png)
- [`../screenshots/c-savefs-sync.png`](../screenshots/c-savefs-sync.png)
- [`../screenshots/c-savefs-restored2.png`](../screenshots/c-savefs-restored2.png)
- [`../screenshots/c-savefs-persisted.png`](../screenshots/c-savefs-persisted.png)
- [`../screenshots/c-stage6-canvas3.png`](../screenshots/c-stage6-canvas3.png)
- [`../screenshots/c-stage6-event2.png`](../screenshots/c-stage6-event2.png)
- [`../screenshots/c-stage6-sysinfo.png`](../screenshots/c-stage6-sysinfo.png)
- [`../screenshots/c-stage7-frozen2-calc.png`](../screenshots/c-stage7-frozen2-calc.png)
- [`../screenshots/c-stage7-frozen2-top.png`](../screenshots/c-stage7-frozen2-top.png)
- [`../screenshots/c-stage7-frozen2-xxd.png`](../screenshots/c-stage7-frozen2-xxd.png)
- [`../screenshots/c-stage7-frozen2-demos.png`](../screenshots/c-stage7-frozen2-demos.png)
- [`../screenshots/c-stage7-frozen2-edfile.png`](../screenshots/c-stage7-frozen2-edfile.png)
- [`../screenshots/c-serial-upload.png`](../screenshots/c-serial-upload.png)
- [`../screenshots/c-serial-download.png`](../screenshots/c-serial-download.png)
- [`../screenshots/c-stage8-desktop-640x480.png`](../screenshots/c-stage8-desktop-640x480.png)
- [`../screenshots/c-stage8-bench.png`](../screenshots/c-stage8-bench.png)
- [`../screenshots/c-stage8-fallback-320x200.png`](../screenshots/c-stage8-fallback-320x200.png)

## 1.44 MB Floppy Space & Speed Ledger

Exact sector/byte accounting for `MiniOS-C.img` (`1,474,560` bytes = `2,880` sectors):

| Region | LBA Range | Sectors | Bytes | Notes |
|---|---|---:|---:|---|
| Boot sector (`boot.bin`) | `0` | `1` | `512` | Loads kernel, `MPK1` pack, and `stage2` |
| 32-bit C kernel (`kernel.bin`) | `1–128` | `128` | `59,120` / `65,536` | `6.4 KB` loader headroom; `.bss` ends at `0x60C7B` |
| Program pack (`MPK1`) | `129–192` | `64` | `32,768` | Boot-loaded at `0x70000` (`A/B/C/HELLO.BIN`) |
| Stage 2 VBE/PM (`stage2.bin`) | `193` (`194–199` pad) | `1` (+`6`) | `512` (+`3,072`) | Loaded at `0x7E00`; VBE `640×480×8` LFB + `320×200` fallback |
| Read-only `MiniFS` (`MFS1`) | `200–1203` | `1,004` | `514,048` | `MPY.BIN` (`189,616 B`, 7 frozen tools) + Ring 3/0 test suite |
| `MiniFS` free expansion space | `1204–2559` | `1,356` | `694,272` | Unused `MiniFS` capacity (`678.0 KB`) |
| `SaveFS` snapshot (`MSV1`) | `2560–2819` | `260` | `133,120` | CRC32-verified persistent RAMFS (`sync` / `os.sync()`) |
| Tail slack | `2820–2879` | `60` | `30,720` | Reserved tail slack |
| **Total** | **`0–2879`** | **`2,880`** | **`1,474,560`** | **`1,416` free sectors (`724,992 B`, `49.2%` free)** |

Measured performance (`bench` / `pystat` on QEMU i386):

- **Cold `MPY.BIN` load (FDC track cache)**: `20 ms` (`2 ticks`)
- **Warm `MPY.BIN` launch (RAM-resident, 0 disk I/O)**: **`< 10 ms` (`0 ticks`)**
- **Integer loop (`60,000` iterations)**: `~290–350 ms` (`~171–206 Kops/s`)
- **Hardware x87 float (`10,000` `sin * sqrt`)**: `~160–180 ms` (`~55–62 Kops/s`)
- **GC allocation (`12,000` tuples on `1.5 MB` user heap)**: `~580 ms` (`~20 K/s`)
- **Arbitrary-precision `mpz`**: `2**1000` (`302` digits) `< 10 ms`

## Program pack

The image layout is:

| Offset | Contents |
|---:|---|
| `0` | 512-byte boot sector |
| `512` | kernel, up to 128 sectors (64 KB), loaded at `0x10000` |
| `66048` | MPK1 program pack, 64 sectors (32 KB), LBA 129 |
| `102400` | read-only MiniFS (LBA 200), directory + data sectors |

Pack format:

```text
magic "MPK1" | u16 count | u16 reserved
count × { char name[16]; u32 offset; u32 size; }
data...
```

`c/build_c.py` creates the pack from `task_a.bin`, `task_b.bin`,
`task_c.bin` and `user.bin`, then writes it at offset 66048. The boot sector
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
| Mouse | click/focus/raise windows, drag title bars, buttons activate on release |
| `F1` / `F2` / `F3` | About / System / Console 1 |
| `F4` | open/close the Windows selection list |
| `F5` / `F6` | Console 2 / Console 3 |
| `Tab` (in console) | open the Windows list |
| `Tab` / `Enter` (list) | cycle selection / activate window |
| `Esc` | close list, menu, About, System, or reopen Console 1 |
| Keyboard | focused console shell input |

Shell commands: `help`, `ver`, `cpu`, `mem`, `time`, `date`, `uptime`, `clear`,
`echo`, `console 1|2|3`, `windows`, `sync`, `serial`, `upload NAME`,
`download NAME`, `sysinfo`, `about`, `progs`, `run NAME`, `tasks`, `ls`,
`exec NAME`, `disktest`, `ring3`, `modtest`, `mod NAME`, `utest`, `reboot`,
`halt`.

Persistence test (QEMU drive must be writable — omit `readonly=on`):

```powershell
python c/build_c.py
qemu-system-i386 -drive "file=MiniOS-C.img,if=floppy,format=raw" -boot a -m 128
# in MiniOS:
py
f=open('SAVED.PY','w'); f.write('print(42)'); f.close()
import os; os.sync()
# Ctrl-D, then restart QEMU and run:
py SAVED.PY
```

Serial QEMU example:

```text
-serial tcp:127.0.0.1:5555,server,nowait
```

Then connect a TCP client to port 5555. `upload TMP.TXT` waits for raw bytes and
terminates on Ctrl-Z; `download TMP.TXT` emits a `<<<FILE ...>>>` header, raw
bytes and `<<<END>>>`.

Process/FD test programs in MiniFS:

```text
exec PARENT.BIN    # spawn + sleep + wait
exec PARENT2.BIN   # spawn + blocking wait
exec STATUS.BIN    # exit(5) -> wait() == 5
exec FD.BIN        # open/read/close TEST.TXT
exec RAMFS.BIN     # create/write/seek/read a RAMFS file
exec ARGS.BIN      # spawn ARG.BIN with argv
exec HEAP.BIN      # brk heap grow/write/read/shrink
exec BIGWRITE.BIN  # 5000-byte syscall 4 write
exec DIR.BIN       # stat/readdir merged RAMFS + MiniFS
uargs              # large UMP1 task argc/argv
big512             # 300 KB UMP1 payload (> old 256 KB limit)
ps                 # list active tasks
kill PID           # terminate a task, including a runaway one
py                 # MicroPython REPL
py -e 2**64        # one-shot expression
py TOOL.PY         # execute a RAMFS/MiniFS script
= 2**100           # warm-cache one-shot expression
pystat             # last MPY load: cold/warm, ticks, bytes
```

Canvas example:

```python
import minios
c = minios.Canvas("Demo")
c.clear(16)                    # 6x6x6 palette index
c.rect(20, 20, 140, 80, 196)
c.line(10, 110, 170, 10, 226)
c.text(30, 30, "Canvas API", 15)
print(c.event())               # (x, y, buttons, closed) or None
print(minios.sysinfo())
```

Tools:

```text
calc 2**10        # eval expression
top               # sysinfo + video mode
bench             # cold/warm MPY load + CPU/FPU/GC benchmark
demos             # 256-color Canvas curve & palette demo
plot sin(x)*x     # plot custom expression on Canvas
xxd TEST.TXT      # hex/ASCII dump
edit NOTES.TXT    # line editor to RAMFS
futil             # list/stat files
```

## Window manager

- 3 console windows, Console 1 visible at boot; `F5`/`F6`, Start menu, taskbar,
  and the Windows list can show/focus them
- the clicked/focused window is raised to the top of the Z-order
- active title bar is blue; inactive title bar is gray
- `F4` or taskbar `Wins` opens the window list; `*` marks focus, `-` marks hidden
- taskbar buttons show the active window and have pressed/sunken feedback
- close/OK/buttons execute on mouse release only if the pointer is still over
  the same target
- title bars drag their windows; console text coordinates follow the window

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

- `c/build/boot.bin` — 512 bytes (LBA 0)
- `c/build/stage2.bin` — 512 bytes (LBA 193, loaded at `0x7E00`)
- `c/build/kernel.bin` — 52,888 bytes (~51.6 KB, 64 KB loader limit; `.bss` ends at `0x5EAF3`)
- `c/build/mpy.bin` — **191,056 bytes** (186.6 KB, image+BSS 191,616 B, 8 frozen tools)
- Stage 0–8 master roadmap: [`../docs/python-os-roadmap.md`](../docs/python-os-roadmap.md)
- `c/build/user.bin` — single Ring 3 demo
- `c/build/task_a.bin`, `task_b.bin`, `task_c.bin` — pack programs
- `c/build/disk_task.bin` — MiniFS disk task
- `c/build/modtest.bin` — kernel-module ABI test
- `c/build/umtest.bin` — large Ring 3 user-module test
- `MiniOS-C.img` — 1,474,560 bytes (pack at LBA 129, stage2 at LBA 193, MiniFS at LBA 200, SaveFS at LBA 2560)

## Run

```powershell
qemu-system-i386 `
  -drive "file=MiniOS-C.img,if=floppy,format=raw" `
  -boot a -m 128 -rtc base=localtime
```

(Hold `Shift` during boot to force the `320×200` VGA Mode `0x13` fallback path.)

## Source layout

| File | Role |
|---|---|
| `boot.asm` | real-mode sector 0 loader: kernel + pack + `stage2` |
| `stage2.asm` | real-mode VBE `640×480×8` LFB setup (`0x500` handoff), VGA fallback, A20, GDT, 32-bit PM |
| `entry.asm` | 32-bit entry, stack, BSS zero, `kmain` |
| `interrupts.asm` | IRQ0/IRQ1/IRQ6/IRQ12 stubs + `int 0x80` syscall stub |
| `usermode.asm` | Ring 3 entry and exit-to-kernel context restore |
| `sched.asm` | scheduler start/exit, per-task `switch_to` |
| `user.asm` | single Ring 3 demo, linked at `0x400000` |
| `user_blob.asm` | embeds `user.bin` for the `ring3` command |
| `task_a.asm`, `task_b.asm`, `task_c.asm` | pack task programs |
| `disk_task.asm` | Ring 3 task stored in MiniFS |
| `parent_test.asm`, `parent_block.asm`, `parent_status.asm` | spawn/sleep/wait/status tests |
| `fd_test.asm`, `test.txt` | MiniFS open/read/close test |
| `ramfs_test.asm` | RAMFS create/write/seek/read test |
| `parent_args.asm`, `arg_child.asm` | argv spawn/stack test |
| `heap_test.asm` | brk heap test |
| `modtest.asm` | kernel-module ABI test at `0x100000` |
| `umtest.asm` | large Ring 3 user-module test at `0x400000` |
| `module_api.h` | MicroPython module header/API layout |
| `../docs/micropython-user-abi.md` | Ring 3 memory layout and syscall ABI |
| `fdc.c`, `fdc.h` | IRQ6/DMA/CHS floppy sector driver |
| `kernel.c` | IDT/PIC/PIT, keyboard/mouse, GDT/TSS, paging, tasks, pack, MiniFS, module loader, syscalls |
| `../docs/micropython-feasibility.md` | MicroPython feasibility, gaps and handoff checklist |
| `../docs/micropython-module-abi.md` | exact module/API ABI |
| `gui.c` | VGA framebuffer, focus/Z-order window manager, 3 consoles, window list, shell, mouse UI |
| `gui.h` | GUI API and kernel/GUI globals |
| `linker.ld` | kernel at `0x10000`, BSS NOLOAD |

## Status / caveats

- Ring 1/2 unused (normal; only Ring 0/3 matter here).
- Dynamic table has **8 slots**; 7 small slots + 1 big Ring 3 slot.
- Scheduler is timer-preemptive with a fixed 10 ms slice; no priorities.
- Task code/stack/heap pages come from a frame allocator (5–16 MB).
- `spawn`/`spawn_args` support flat MiniFS programs ≤4 KB; big UMP1 tasks
  support up to 512 KB code and argv, but no fork/replace-exec.
- `wait` is parent/child only; exit status is available through syscall 11.
- `kill` terminates the target; killing the current task is deferred to the
  scheduler. There is still no foreground control UI.
- FDs can write only RAMFS; MiniFS remains read-only.
- RAMFS is 8 files × 16 KB and non-persistent.
- Program pack is read-only and boot-loaded; MiniFS is read-only at runtime.
- MiniFS is custom, not FAT12; no write support or directory updates.
- FDC driver reads one 512-byte sector per command (seeks between tracks).
- `exec` supports flat programs ≤4 KB; no arguments, fork/exec or per-task heap.
- GUI focus/Z-order is fixed-list (5 windows), not a general compositor.
- Taskbar shows Console 1/2; Console 3 is via Start menu or the Windows list.
- FDC DMA buffer is at `0x8000`; kernel BSS ends at `0x5ea68`, so it is safe.
- During a running task, only safe shell commands are accepted in another
  console; task/FDC/serial/reboot commands are refused with `[busy]`.
- There is no kernel task-kill/foreground-control UI yet; Python must exit via
  `Ctrl-C`/`Ctrl-D` or its own prompt.
- RAMFS is persistent only after `sync`/`os.sync()`; the SaveFS snapshot is
  CRC32-checked and restored at boot. The whole sync is blocking and has no
  power-loss journal.
- Serial upload/download is a host bridge, independent of SaveFS.
- FDC is still read-only; there is no writable MiniFS/FAT12.
- The 64 KB loader is enough for current growth, but the next large subsystem
  will need another loader/memory-layout revision.
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
2. `fork`/`exec` with argv, user heap `brk/mmap`, demand paging.
3. Writable VFS and real `open/read/write/seek`.
4. Per-process windows / graphics syscalls.
5. More big-task slots, priorities, SMP.






















