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

| Physical | Size | Use |
|---:|---:|---|
| `0x00100000` | 256 KB | code/image |
| `0x00140000` | 64 KB | stack |
| `0x00150000` | 1 MB | heap |

The kernel identity-maps 0–4 MB, so it can load/copy/zero these frames.

**Conflict:** the kernel-mode module loader also uses `0x00100000` and
`0x00200000`. Do not run `mod NAME` and `utest` at the same time; the big user
task owns `0x00100000`+ while active.

Only **task slot 3** is used for this big task. Small tasks use their own
frames at `0x00300000`.

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

Notes:

- Syscalls 4/5/6/7 require pointers in the user window `0x400000–0x7FFFFF`.
- Syscall 5 enables interrupts while waiting; keyboard IRQs feed it.
- Syscall 7 reads a MiniFS file (whole file, read-only) into the user buffer.
- There is no open/read/seek/close or write-to-disk API yet.
- Syscall 5 skips F1–F3 keyboard events; ASCII bytes are returned.

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

Expected output includes:

```text
Ring3 large module loaded: multi-page code + 1MB heap.
read_file syscall loaded HELLO.BIN into user heap.
Type one key for blocking getchar: x
Heap, read_file, getchar and ticks_ms OK.
[task] exit; scheduler.
```

## Limitations

- One big task only (slot 3).
- Physical frames are fixed; no demand paging or per-task frame allocator.
- No user-mode file writes, mmap/brk, open/read/seek.
- Kernel syscall stack is 8 KB for the big task; deep MicroPython C recursion
  needs auditing.
- The kernel module ABI at `0x100000` and this user task contend for the same
  physical pages; run one at a time.
- Ring 3 isolation is real for the user task, but the kernel/user boundary is
  only these seven syscalls.
