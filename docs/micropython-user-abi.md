# MiniOS Ring 3 user ABI v1 (MicroPython bring-up)

This is the user-mode ABI for a large Ring 3 module on `v2-protected-c`.
It is used by `utest` and is the intended target for a MicroPython user-mode
port.

## Virtual address layout (single big task slot)

| Virtual | Size | Use |
|---:|---:|---|
| `0x00400000` | 256 KB max | module code/rodata/data + `UMP1` header |
| `0x00480000` | 64 KB | user stack (`ESP` starts at `0x00490000`) |
| `0x00500000` | 1 MB | user heap for MicroPython GC |
| `0x00400000–0x007FFFFF` | 4 MB | user page-table window (U/S=1 for mapped pages) |

All pages in the window are mapped with U/S=1 only where needed.

## Physical backing (current implementation)

Code, stack and heap pages are allocated dynamically from a frame allocator:

| Physical | Use |
|---:|---|
| `0x00500000–0x00FFFFFF` | 11 MB frame pool |
| `0x01000000–0x01BFFFFF` | kernel direct map of physical 4–16 MB |

The kernel direct map is supervisor-only. The user page table maps virtual
`0x400000+` to allocated frames with U/S=1; the kernel copies through the
direct map.

The old fixed `0x00100000/0x00140000/0x00150000` layout is no longer used for
task memory. `0x00100000` remains the temporary load address for the kernel
module / large user image.

Only **one big task slot** exists (slot 7). Small tasks use slots 0–6.

## Module file header

The image linked at `0x400000` starts with the same 20-byte header shape as the
kernel module, but the magic is `UMP1`:

```c
struct user_module_header {
    u8  magic[4];       /* "UMP1" */
    u32 entry_offset;   /* from 0x400000 */
    u32 image_size;     /* includes header */
    u32 bss_size;
    u32 heap_size;      /* currently informational; heap is 1 MB */
} __attribute__((packed));
```

The loader reads `UMTEST.BIN` from MiniFS through the FDC, copies it to
`0x100000`, zeroes BSS/stack/heap, builds the page directory and starts the task
with:

- EIP = `0x400000 + entry_offset`
- ESP = `0x490000`
- heap = `0x500000`, 1 MB

## Syscalls

`int 0x80`; number in EAX, arguments in EBX/ECX/EDX. The kernel returns a value
by replacing the saved EAX, so the user sees it in EAX after `int 0x80`.

| EAX | Call | Arguments | Return |
|---:|---|---|---|
| 1 | write C string | EBX = pointer | — |
| 2 | exit task | — | does not return |
| 3 | yield | — | continues later |
| 4 | write bytes | EBX = buf, ECX = len | EAX = len |
| 5 | blocking read char | — | EAX = byte |
| 6 | ticks_ms | — | EAX = PIT ms |
| 7 | read whole file | EBX = name, ECX = dst, EDX = max | EAX = size or 0 |
| 8 | spawn | EBX = name | EAX = child PID or 0 |
| 9 | wait | EBX = PID (0 = any child) | EAX = exit status or -1 |
| 10 | sleep ms | EBX = ms | EAX = 0 |
| 11 | exit(status) | EBX = status | does not return |
| 12 | open | EBX = name | EAX = fd (1..8) or -1 |
| 13 | read | EBX = fd, ECX = buf, EDX = len | EAX = bytes or -1 |
| 14 | close | EBX = fd | EAX = 0 or -1 |
| 15 | seek | EBX = fd, ECX = offset, EDX = whence | EAX = new offset or -1 |

Notes:

- Syscalls 4/5/6/7/12/13/14/15 require pointers in the user window
  `0x400000–0x7FFFFF`.
- Syscall 5 enables interrupts while waiting; keyboard IRQs feed it.
- Syscall 7 reads a whole MiniFS file into the user buffer.
- Syscalls 12–15 are the read-only FD API. FDs are per-task (8 per task),
  backed by MiniFS extents; `read` copies sectors through the FDC.
- `spawn` loads a flat MiniFS program ≤4 KB, assigns a PID and parent PID,
  and returns immediately. `wait` blocks until a child exits.
- `sleep` blocks the task; IRQ0 wakes it. If no other task is ready, the
  syscall busy-waits with `sti; hlt`.
- Syscall 2 exits with status 0; syscall 11 exits with EBX.
- Syscall 5 skips F1–F6 keyboard events; ASCII bytes are returned.

## HAL mapping for MicroPython

- `mp_hal_stdout_tx_strn(s, n)` -> syscall 4
- `mp_hal_stdin_rx_chr()` -> syscall 5
- `mp_hal_ticks_ms()` -> syscall 6
- `gc_init((void *)0x500000, (void *)0x600000)`
- console output uses syscall 4; no stdio needed
- script import can use syscall 7 for whole files, or frozen modules

## Build and test

Current repository test module:

```text
utest
```

It verifies:

- multi-page code image
- 1 MB heap write/read
- syscall 4 (write bytes)
- syscall 6 (ticks)
- syscall 7 (`HELLO.BIN` from MiniFS into user heap)
- syscall 5 (blocking key)
- syscall 2 (exit back to shell)

Process/FD tests:

```text
exec PARENT.BIN    # spawn + sleep + wait
exec PARENT2.BIN   # spawn + blocking wait
exec FD.BIN        # open/read/close TEST.TXT
```

Expected output includes:

```text
Ring3 large module loaded: multi-page code + 1MB heap.
read_file syscall loaded HELLO.BIN into user heap.
Type one key for blocking getchar: x
Heap, read_file, getchar and ticks_ms OK.
[task] exit; scheduler.
```

## Limitations

- One big task only (slot 7).
- Physical frames are dynamically allocated, but there is no demand paging,
  `mmap` or `brk`; the big heap is a fixed 1 MB region.
- FDs are read-only; no file writes, no writable VFS.
- `spawn` supports flat programs ≤4 KB; no argv/fork/exec.
- Kernel syscall stack is 8 KB for the big task; deep MicroPython C recursion
  needs auditing.
- `0x00100000` remains a temporary image load address; don't run the kernel
  module and a large user image at the same instant.
- Ring 3 isolation is real for the user task; the kernel/user boundary is
  syscalls 1–15.
