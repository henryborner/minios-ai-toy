#!/usr/bin/env python3
"""Build the experimental 32-bit C kernel (branch v2-protected-c)."""
from __future__ import annotations

import hashlib
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CDIR = ROOT / "c"
BUILD = CDIR / "build"
IMAGE = ROOT / "MiniOS-C.img"
FLOPPY_SIZE = 1_474_560
BOOT_SIZE = 512
KERNEL_SECTORS = 64
KERNEL_MAX = KERNEL_SECTORS * 512


def find_tool(names: list[str], extra: list[Path]) -> str:
    for name in names:
        found = shutil.which(name)
        if found:
            return found
    for path in extra:
        if path.exists():
            return str(path)
    raise SystemExit(f"tool not found: {names} / {extra}")


def run(args: list[str], cwd: Path = CDIR) -> None:
    print("$", " ".join(str(a) for a in args))
    subprocess.run(args, cwd=cwd, check=True)


def main() -> int:
    BUILD.mkdir(parents=True, exist_ok=True)

    nasm = find_tool(["nasm"], [ROOT / "tools" / "nasm-2.16.03" / "nasm.exe"])
    clang = find_tool(["clang"], [
        Path("C:/msys64/ucrt64/bin/clang.exe"),
        Path("C:/msys64/usr/bin/clang.exe"),
    ])
    ld = find_tool(["ld.lld", "ld.lld.exe"], [
        Path("C:/msys64/ucrt64/bin/ld.lld.exe"),
    ])
    objcopy = find_tool(["objcopy"], [
        Path("C:/msys64/ucrt64/bin/objcopy.exe"),
        Path("C:/msys64/usr/bin/objcopy.exe"),
    ])

    boot = BUILD / "boot.bin"
    entry = BUILD / "entry.o"
    interrupts_o = BUILD / "interrupts.o"
    user_bin = BUILD / "user.bin"
    user_blob_o = BUILD / "user_blob.o"
    usermode_o = BUILD / "usermode.o"
    task_a_bin = BUILD / "task_a.bin"
    task_b_bin = BUILD / "task_b.bin"
    task_blob_o = BUILD / "task_blob.o"
    sched_o = BUILD / "sched.o"
    gui_o = BUILD / "gui.o"
    kernel_o = BUILD / "kernel.o"
    kernel_elf = BUILD / "kernel.elf"
    kernel_bin = BUILD / "kernel.bin"

    run([nasm, "-f", "bin", "-o", str(boot), "boot.asm"])
    run([nasm, "-f", "elf32", "-o", str(entry), "entry.asm"])
    run([nasm, "-f", "elf32", "-o", str(interrupts_o), "interrupts.asm"])
    run([nasm, "-f", "bin", "-o", str(user_bin), "user.asm"])
    run([nasm, "-f", "elf32", "-o", str(user_blob_o), "user_blob.asm"])
    run([nasm, "-f", "elf32", "-o", str(usermode_o), "usermode.asm"])
    run([nasm, "-f", "bin", "-o", str(task_a_bin), "task_a.asm"])
    run([nasm, "-f", "bin", "-o", str(task_b_bin), "task_b.asm"])
    run([nasm, "-f", "elf32", "-o", str(task_blob_o), "task_blob.asm"])
    run([nasm, "-f", "elf32", "-o", str(sched_o), "sched.asm"])
    run([
        clang, "--target=i386-unknown-none-elf", "-m32", "-march=i386",
        "-ffreestanding", "-fno-pic", "-fno-pie", "-fno-stack-protector",
        "-fno-builtin", "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
        "-mno-sse", "-mno-mmx", "-mno-80387", "-msoft-float",
        "-Os", "-fno-unroll-loops", "-Wall", "-Wextra", "-c", "kernel.c", "-o", str(kernel_o),
    ])
    run([
        clang, "--target=i386-unknown-none-elf", "-m32", "-march=i386",
        "-ffreestanding", "-fno-pic", "-fno-pie", "-fno-stack-protector",
        "-fno-builtin", "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
        "-mno-sse", "-mno-mmx", "-mno-80387", "-msoft-float",
        "-Os", "-fno-unroll-loops", "-Wall", "-Wextra", "-c", "gui.c", "-o", str(gui_o),
    ])
    run([
        ld, "-m", "elf_i386", "-T", "linker.ld",
        "-o", str(kernel_elf), str(entry), str(interrupts_o),
        str(usermode_o), str(sched_o), str(user_blob_o), str(task_blob_o),
        str(kernel_o), str(gui_o),
    ])
    run([objcopy, "-O", "binary", str(kernel_elf), str(kernel_bin)])

    boot_data = boot.read_bytes()
    kernel_data = kernel_bin.read_bytes()
    if len(boot_data) != BOOT_SIZE:
        raise SystemExit(f"boot sector is {len(boot_data)} bytes, expected 512")
    if len(kernel_data) > KERNEL_MAX:
        raise SystemExit(f"kernel too large: {len(kernel_data)} > {KERNEL_MAX}")

    image = bytearray(FLOPPY_SIZE)
    image[0:BOOT_SIZE] = boot_data
    image[BOOT_SIZE:BOOT_SIZE + len(kernel_data)] = kernel_data
    IMAGE.write_bytes(image)

    digest = hashlib.sha256(image).hexdigest()
    print(f"[boot]   {len(boot_data):>6} bytes")
    print(f"[kernel] {len(kernel_data):>6} bytes")
    print(f"[image]  {IMAGE.name}: {len(image):,} bytes")
    print(f"[sha256] {digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())


