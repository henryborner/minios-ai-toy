# MiniOS kernel-module ABI v1 (MicroPython bring-up)

This is the interface a kernel-mode module (for example a MicroPython build)
uses on the current `v2-protected-c` branch. It is intentionally a **bring-up
ABI**, not the final Ring 3 ABI.

## Memory map

| Address | Use |
|---:|---|
| `0x00010000` | kernel image |
| `0x00070000` | boot-loaded MPK1 pack |
| `0x00080000` | TSS ring0 stack top |
| `0x00090000` | kernel stack top |
| `0x00100000` | **module image base** (max 1 MB) |
| `0x00200000` | **module heap base** (default 1 MB) |
| `0x00300000` | reserved |
| `0x00400000` | user virtual code base (Ring 3 tasks) |
| `0xA0000` | VGA mode 13h framebuffer |

The module is linked at `0x100000`, loaded flat from MiniFS, then entered.
The module image and heap are below 4 MB, so they are mapped by every page
directory in the current kernel.

## Module file header

The file in MiniFS starts with this 20-byte header:

```c
struct module_header {
    u8  magic[4];       // "MPM1"
    u32 entry_offset;   // from 0x100000
    u32 image_size;     // header + code + rodata + data
    u32 bss_size;       // zeroed by loader after image_size
    u32 heap_size;      // 0 or <= 1 MB; 0 means default 1 MB
} __attribute__((packed));
```

Constraints:

- `image_size <= file_size`
- `entry_offset < image_size`
- `image_size + bss_size <= 1 MB`
- heap is fixed at `0x00200000`, size `0x00100000`

The loader copies the file to `0x100000`, zeroes BSS, fills the API table and
calls:

```c
void entry(struct minios_api *api);
```

with the C calling convention (argument at `[esp+4]` on entry). Return returns
to the kernel and the GUI shell continues.

## API table

Layout is fixed; append-only for future versions.

| Offset | Field |
|---:|---|
| 0 | `u32 version` (currently 1) |
| 4 | `void (*puts)(const char *s)` |
| 8 | `void (*putc)(char c)` |
| 12 | `u32 (*ticks_ms)(void)` |
| 16 | `int (*getchar)(void)` |
| 20 | `u8 *heap` |
| 24 | `u32 heap_size` |
| 28 | `void (*write)(const char *s, u32 len)` |

C declaration matching this layout is in `c/module_api.h`.

### Console semantics

- `puts` and `write` append to the GUI console and redraw.
- `getchar` blocks until a keyboard byte is available. It returns ASCII bytes;
  F1-F3 are skipped. It uses `sti; hlt` while waiting.
- There is no line editing or echo in the API; the module/REPL handles it.
- Output is synchronous and may redraw the whole GUI.

### Time

- `ticks_ms` returns `PIT ticks * 10` (100 Hz), wrapping at 32 bits.

### Heap

- `heap` is a fixed 1 MB region at `0x00200000` unless the header requests less.
- MicroPython should call `gc_init(api->heap, api->heap + api->heap_size)`.
- There is no general kernel allocator guarantee beyond this region.

## Loading and running

MiniFS file (current test): `MODTEST.BIN`.

```text
mod MODTEST.BIN
```

The shell command is hardwired in `gui.c`; a port can add another name or
replace the test file. The loader reads from the floppy at runtime via the FDC
driver, so the module does not need to be linked into the kernel image.

## What the module must provide

A MicroPython port needs to supply:

- `mp_hal_stdin_rx_chr` -> `api->getchar()`
- `mp_hal_stdout_tx_strn` -> `api->write(str, len)`
- `mp_hal_ticks_ms` -> `api->ticks_ms()`
- `gc_init(api->heap, api->heap + api->heap_size)`
- minimal libc: `memcpy`, `memset`, `memmove`, `strlen`, `strcmp`, `strcpy`,
  `alloca` (compiler builtin), `printf`/`snprintf` only if asserts/debug use it
- x86 `nlr` support (`py/nlrx86.c`)
- a linker script placing the image at `0x100000` and the header at the start
- BSS reported through the header (or included explicitly in the file)

No filesystem is required for a minimal REPL; frozen modules can be used.
File imports/VFS are a later feature.

## Limitations

- The module runs in **Ring 0**: a crash takes down the OS. This is deliberate
  for the first MicroPython bring-up; a Ring 3 version needs user heap, syscall
  read/write/time, and large multi-page loading.
- No user-mode isolation.
- No module file API/VFS yet; only frozen modules or data embedded in the module.
- Kernel stack is shared; a deep MicroPython recursion can overflow it.
- FPU/SSE should be disabled or handled by the port; current kernel uses
  `-mno-sse -mno-80387 -msoft-float`.
- The module is loaded by the shell and the GUI main loop is suspended while it
  runs; console I/O is synchronous.

## Test

Current repository test module:

```text
modtest
```

Expected:

```text
Kernel module ABI works: hello from 0x100000!
Module API ticks_ms called.
[mod] returned to kernel.
```
