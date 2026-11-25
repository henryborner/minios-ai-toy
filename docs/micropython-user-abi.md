# MiniOS Ring 3 user ABI (UMP1)

Current branch: `v2-protected-c`.

This document describes the UMP1 module format and the current Ring 3 task
layout. It supersedes the older single-slot / 20-byte-header notes.

## Task model

- Task 0 is the persistent GUI/Shell kernel thread. It is not a user task.
- User slots 1–5 are small Ring 3 tasks.
- User slots 6–8 are big UMP1/MicroPython tasks (up to three concurrent).
- Every user task has:
  - its own page directory and user page table
  - its own kernel stack and `kesp`
  - its own user stack and heap
  - a console id and FPU state

Kernel code/data are supervisor-only. UMP1 read-only pages are mapped user
read-only (`PTE 0x005`); private data, BSS, stack and heap are user read/write
(`PTE 0x007`).

## Virtual layout

| Virtual | Size | Use |
|---:|---:|---|
| `0x400000` | up to 512 KB | UMP1 header + text/rodata + private data/BSS |
| `0x480000` | 64 KB | user stack; initial ESP near `0x490000` |
| `0x500000–0x700000` | 2 MB | brk heap window; initial big heap 1 MB |

`argv` strings are copied into the task's user stack by
`build_args_on_stack()` before the task is made READY.

## Physical layout

| Physical | Use |
|---:|---|
| `0x10000` | boot staging for the kernel image |
| `0x300000` | linked kernel image |
| `0x500000` | frame pool start |
| `0x1000000` | frame pool end |
| `0x1000000–0x1C00000` | supervisor direct map of frames |

`0x00100000` remains the temporary staging area for a UMP1 image. The kernel
module heap area `0x00200000–0x00300000` is separate from the kernel at
`0x00300000`.

## UMP1 header

The image starts with a 24-byte header:

```c
struct module_header {
    u8  magic[4];       /* "UMP1" */
    u32 entry_offset;   /* from 0x400000 */
    u32 image_size;     /* includes header, ends before BSS */
    u32 bss_size;
    u32 heap_size;      /* hint; current user heap is 1 MB */
    u32 ro_size;        /* bytes of header+text+rodata; 4 KB aligned */
} __attribute__((packed));
```

`.data` is linked at a 4 KB boundary. `ro_size` is the end of the page-aligned
read-only area:

- first task: copies `ro_size` bytes to a shared frame block
- subsequent tasks: map the same shared pages `0x005`
- each task: copies only `image_size - ro_size` bytes into private data pages
  and zeroes BSS

Shared read-only pages are reference-counted and freed with the last user.
A different UMP1 image while shared users exist is rejected.

## Syscalls

`int 0x80`: number in EAX, arguments in EBX/ECX/EDX (the kernel uses saved
frame slots, not the live syscall registers). Return in saved EAX.

| EAX | Call | Arguments | Return |
|---:|---|---|---|
| 1 | write C string | EBX = pointer | — |
| 2 | exit | — | does not return |
| 3 | yield | — | continues later |
| 4 | write bytes | EBX = buf, ECX = len | len |
| 5 | blocking getchar | — | byte; blocks on the task's console |
| 6 | ticks_ms | — | PIT milliseconds |
| 7 | read whole file | EBX = name, ECX = dst, EDX = max | size or 0 |
| 8 | spawn | EBX = name | child PID or 0 |
| 9 | wait | EBX = PID (0 = any child) | exit status or -1 |
| 10 | sleep ms | EBX = ms | 0; blocks and wakes on IRQ0 |
| 11 | exit(status) | EBX = status | does not return |
| 12 | open read-only | EBX = name | fd or -1 |
| 13 | read | EBX = fd, ECX = buf, EDX = len | bytes or -1 |
| 14 | close | EBX = fd | 0 or -1 |
| 24 | canvas open | EBX = title | 0 / -1 |
| 25 | canvas cmd | EBX = command pointer | result |
| 26 | canvas present | — | 0 |
| 27 | canvas close | — | 0 |
| 28 | beep / serial | op + args | result |
| 30 | deferred run py | EBX = script | 0 / -1 |
| 31 | non-blocking getchar | — | byte or -1 |
| 32 | task info | EBX = buffer, ECX = max | count |
| 33 | pipe op | packed op/id, buf, len | result |

`getchar` blocks with a saved kernel stack (`sched_yield`); the Shell and other
consoles continue running. `sleep` uses the same mechanism with a timer wake.
`wait` stores the child exit status in `wait_status`.

## User-visible behavior

- Console output is routed by the task's console id.
- A task can block indefinitely on input without freezing the desktop.
- Task exit frees code/data, stack, heap and the shared-RO reference.
- `selftest` emits `[selftest] PASS` on COM1.
- The smoke test warms caches before comparing free frames.

## Limits

- No demand paging.
- No copy-on-write.
- No general fork; spawn creates a fresh task.
- Shared RO only for the same UMP1 image.
- One Canvas window currently; multi-Canvas is not implemented.
