# MicroPython on MiniOS — feasibility and bring-up report

Status: **minimal kernel-mode bring-up is feasible**; a full CPython port is not
the same project and remains out of scope.

## 1. What MicroPython actually needs

MicroPython is a separate Python 3 implementation for constrained/bare-metal
targets. The official `ports/minimal` is a reference port that runs on a host or
an STM32F405 with:

- the MicroPython core (`py/`)
- a `main.c`
- console HAL: `mp_hal_stdin_rx_chr`, `mp_hal_stdout_tx_strn`
- time HAL: `mp_hal_ticks_ms` (can be a stub)
- GC heap via `gc_init(heap, heap + size)`
- `nlr` for exceptions (`py/nlrx86.c` exists for 32-bit x86)
- a tiny libc layer: `string0.c`, `printf.c`, `alloca`, `stdint.h`

The minimal port does **not** require a filesystem: `mp_import_stat` returns
`NO_EXIST`, and only frozen modules are available. That is enough for a REPL.

Official size/memory data:

| Configuration | Approximate |
|---|---:|
| Minimal ARM Thumb2 code | under 80 KB |
| Minimal x86 host code | roughly 100–250 KB (must measure the actual build) |
| Unix port | around 280 KB |
| Recommended minimum board | 128 KB ROM / 8 KB RAM |
| Minimal heap in `ports/minimal` | 25.6 KB on Linux; 2 KB on ARM |
| Compiler disabled saving | ~20 KB Thumb2, ~40 KB 32-bit x86 |

The x86 numbers are estimates until we build the actual i386 port.

Sources:

- <https://github.com/micropython/micropython/wiki/FAQ>
- <https://github.com/micropython/micropython/tree/master/ports/minimal>
- <https://docs.micropython.org/en/latest/develop/porting.html>
- `ports/minimal/main.c`, `mpconfigport.h`, `Makefile` (fetched from upstream)
- `py/nlrx86.c` (x86 NLR)

## 2. What we built (this repository)

A **kernel-mode module ABI and loader** for the first MicroPython bring-up:

- module file loaded from MiniFS at runtime via the FDC driver
- module linked/loaded at `0x00100000`, max 1 MB
- fixed 1 MB heap at `0x00200000`
- 20-byte `MPM1` header with entry/image/BSS/heap size
- loader copies the image, zeroes BSS, fills an API table and calls
  `entry(api)`
- API v1: `puts`, `putc`, `ticks_ms`, `getchar`, `heap`, `heap_size`,
  `write(s,len)`
- test module `MODTEST.BIN`, shell command `modtest` / `mod NAME`

The ABI is documented in [`micropython-module-abi.md`](micropython-module-abi.md)
with C declarations in `c/module_api.h`.

This is deliberately **Ring 0** for the first bring-up. A crash in the module
takes down MiniOS. Ring 3 isolation is a later hardening step.

## 3. Current gaps

### Blocking for a minimal MicroPython REPL

| Gap | Status |
|---|---|
| Loader for >4 KB module | **done** (up to 1 MB) |
| Fixed GC heap | **done** (1 MB at 0x200000) |
| Console output (length-aware) | **done** (API `write`) |
| Blocking console input | **done** (API `getchar`) |
| Millisecond time | **done** (PIT ticks × 10) |
| Module BSS handling | **done** (header + loader) |
| i386 `nlr` | available upstream, not yet compiled into a port |
| Minimal libc/headers | **not integrated**; port must provide `string0.c`/`printf.c`/`alloca`/headers |
| MicroPython i386 port config + linker | **not done** — this is the adaptation task |
| Actual binary size measurement | **not done**; official estimates only |
| Frozen module(s) | optional; can start with REPL only |

### Needed for a useful (not just REPL) MicroPython

- read-only VFS/imports from MiniFS (current MiniFS has no file API exposed to
  modules)
- writable temp/cache (optional if `__pycache__` disabled)
- more stack (kernel stack is shared; deep recursion needs auditing)
- float policy (disable float or use soft-float; current kernel is
  `-mno-sse -mno-80387 -msoft-float`)
- larger/streamed module loading if the final MicroPython core exceeds 1 MB
- more heap for larger scripts/data

### Needed for isolation / production

- Ring 3 MicroPython task instead of Ring 0
- per-task user heap and multi-page code/data/stack mappings
- console read/write/time syscalls for user mode
- file read syscalls / VFS
- fault handling and cleanup

## 4. Recommended bring-up order

1. Build `ports/minimal`-style MicroPython for **i386 bare metal** at
   `0x00100000`, with:
   - `MICROPY_ENABLE_GC=1`
   - `MICROPY_ENABLE_COMPILER=1`
   - `MICROPY_HELPER_REPL=1`
   - frozen modules, no external import
   - floats disabled initially
2. Link a flat image with the `MPM1` header at `0x100000`; report BSS through
   the header.
3. Implement the HAL in terms of `minios_api`:
   - `mp_hal_stdin_rx_chr` -> `api->getchar()`
   - `mp_hal_stdout_tx_strn` -> `api->write(s, len)`
   - `mp_hal_ticks_ms` -> `api->ticks_ms()`
4. Package the binary as `MPY.BIN` in MiniFS (increase the file size limit as
   needed) and run `mod MPY.BIN`.
5. Verify REPL, `print`, loops, functions, exceptions, GC.
6. Only then move to Ring 3 and replace the synchronous API with syscalls.

## 5. Handoff checklist for the adaptation model

- [ ] get MicroPython source and a working i386 build (clang or GCC)
- [ ] add an i386 bare-metal port/linker script at `0x100000`
- [ ] provide minimal `string.h`/`stdint.h`/`stddef.h`/`stdio.h`/`alloca.h`
- [ ] include `shared/libc/string0.c`, `shared/libc/printf.c`
- [ ] compile `py/nlrx86.c` (or select the x86 NLR)
- [ ] make the `MPM1` header the first 20 bytes of the image
- [ ] report image/BSS/entry offsets correctly
- [ ] set `gc_init(api->heap, api->heap + api->heap_size)`
- [ ] wire the HAL to `api->getchar/write/ticks_ms`
- [ ] build a `MPY.BIN` and place it in MiniFS
- [ ] test `mod MPY.BIN` in QEMU
- [ ] measure final image size and kernel-stack usage

## 6. Verdict

- **Minimal MicroPython REPL: plausible and now has the required OS-side
  loader/heap/console/time foundation.**
- **MicroPython with filesystem imports, large stdlib and hardware modules:
  more porting, but still far more realistic than CPython.**
- **Full CPython: no** on this architecture without building a POSIX-like OS.

The next concrete step is the i386 MicroPython build itself, not more floppy
capacity.
