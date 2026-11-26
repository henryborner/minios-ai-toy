# MiniOS Backlog

## Deferred: Kernel-level asynchronous signals

**Status:** Deferred until after Stage 4 and only when native Ring3 C user
programs or POSIX-compatible signal handlers become a real requirement.

Current Stage 3 implements a lightweight cooperative signal model:

- per-task `sig_pending` / `sig_ignored`
- SIGINT -> MicroPython `KeyboardInterrupt` via `sys_poll_ctrl_c()`
- SIGTERM / SIGKILL -> immediate task reclamation
- SIGSTOP / SIGCONT -> `task_stop_pid()` / `task_bg_pid()`
- SIGCHLD -> pending bit
- `signal_ignore()` / `signal_pending()` user API

This is intentionally not a complete POSIX signal implementation.

### Missing for kernel-level async SIGINT

1. Per-task signal disposition table (`sigaction`-like).
2. Per-task user signal stack.
3. A stable signal-frame ABI on the user stack.
4. Rewriting user EIP/ESP at an arbitrary interrupt point.
5. `sigreturn` syscall restoring the saved user context.
6. Nested-signal policy, user-stack guard pages and overflow handling.
7. MicroPython exception/GC state integration for injected exceptions.
8. Tests for signals arriving in Ring3 CPU loops, blocking syscalls,
   sleep, Canvas, and native C user programs.

### Prerequisites

- A native Ring3 C user ABI that does not depend on the MPY runtime.
- A per-task user exception context and unwind contract.
- User stack guard-page support (current guard pages protect kernel stacks).
- A deliberate decision on signal delivery points and blocked-signal masks.
- A regression harness for stack corruption, nested signals and Triple Fault.

### Explicit non-goal

Do not implement a kernel signal frame/trampoline just to make SIGINT look
asynchronous. The current cooperative model is preferred for the 1.44 MB target
unless the prerequisites above are met.

## Other deferred items

- Stage 4: asynchronous FDC I/O without kernel busy-waiting.
- Optional A: shared-address-space user threads.
- Optional B: Ring3 user-space shell.
- Pipe fd reference counting / dup / more than one reader-writer.
- User signal handlers (the subject above); keep the current lightweight
  `signal_ignore`/`signal_pending` model until then.
