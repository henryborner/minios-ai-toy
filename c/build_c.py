#!/usr/bin/env python3
"""Build the experimental 32-bit C kernel (branch v2-protected-c)."""
from __future__ import annotations

import hashlib
import shutil
import struct
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
PACK_SECTORS = 64
PACK_BYTES = PACK_SECTORS * 512
PACK_OFFSET = (1 + KERNEL_SECTORS) * 512
FS_LBA = 200
FS_OFFSET = FS_LBA * 512
FS_MAX_BYTES = 1024 * 1024


def build_minifs(files: list[tuple[str, Path]]) -> bytes:
    dir_bytes = 8 + len(files) * 24
    dir_sectors = (dir_bytes + 511) // 512
    header = bytearray(b"MFS1") + struct.pack("<HH", len(files), 0)
    entries = bytearray()
    data = bytearray()
    next_lba = FS_LBA + dir_sectors
    for name, path in files:
        payload = path.read_bytes()
        raw_name = name.upper().encode("ascii")
        if len(raw_name) > 15:
            raise SystemExit(f"fs name too long: {name}")
        entries += raw_name.ljust(16, b"\0")
        entries += struct.pack("<II", next_lba, len(payload))
        sectors = (len(payload) + 511) // 512
        data += payload
        data += bytes(sectors * 512 - len(payload))
        next_lba += sectors
    fs = header + entries
    fs += bytes(dir_sectors * 512 - len(fs))
    fs += data
    if len(fs) > FS_MAX_BYTES:
        raise SystemExit(f"MiniFS too large: {len(fs)}")
    return bytes(fs)

def build_pack(files: list[tuple[str, Path]]) -> bytes:
    data_base = 8 + len(files) * 24
    entries = bytearray()
    data = bytearray()
    for name, path in files:
        payload = path.read_bytes()
        offset = data_base + len(data)
        raw_name = name.upper().encode("ascii")
        if len(raw_name) > 15:
            raise SystemExit(f"pack name too long: {name}")
        entries += raw_name.ljust(16, b"\0")
        entries += struct.pack("<II", offset, len(payload))
        data += payload
        while len(data) & 3:
            data.append(0)
    pack = bytearray(b"MPK1")
    pack += struct.pack("<HH", len(files), 0)
    pack += entries
    pack += data
    if len(pack) > PACK_BYTES:
        raise SystemExit(f"program pack too large: {len(pack)} > {PACK_BYTES}")
    pack += bytes(PACK_BYTES - len(pack))
    return bytes(pack)


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
    task_c_bin = BUILD / "task_c.bin"
    disk_task_bin = BUILD / "disk_task.bin"
    modtest_bin = BUILD / "modtest.bin"
    umtest_bin = BUILD / "umtest.bin"
    parent_bin = BUILD / "parent.bin"
    parent_block_bin = BUILD / "parent_block.bin"
    fd_test_bin = BUILD / "fd_test.bin"
    child5_bin = BUILD / "child5.bin"
    parent_status_bin = BUILD / "parent_status.bin"
    sched_o = BUILD / "sched.o"
    gui_o = BUILD / "gui.o"
    fdc_o = BUILD / "fdc.o"
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
    run([nasm, "-f", "bin", "-o", str(task_c_bin), "task_c.asm"])
    run([nasm, "-f", "bin", "-o", str(disk_task_bin), "disk_task.asm"])
    run([nasm, "-f", "bin", "-o", str(modtest_bin), "modtest.asm"])
    run([nasm, "-f", "bin", "-o", str(umtest_bin), "umtest.asm"])
    run([nasm, "-f", "bin", "-o", str(parent_bin), "parent_test.asm"])
    run([nasm, "-f", "bin", "-o", str(parent_block_bin), "parent_block.asm"])
    run([nasm, "-f", "bin", "-o", str(fd_test_bin), "fd_test.asm"])
    run([nasm, "-f", "bin", "-o", str(child5_bin), "child5.asm"])
    run([nasm, "-f", "bin", "-o", str(parent_status_bin), "parent_status.asm"])
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
        clang, "--target=i386-unknown-none-elf", "-m32", "-march=i386",
        "-ffreestanding", "-fno-pic", "-fno-pie", "-fno-stack-protector",
        "-fno-builtin", "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
        "-mno-sse", "-mno-mmx", "-mno-80387", "-msoft-float",
        "-Os", "-fno-unroll-loops", "-Wall", "-Wextra", "-c", "fdc.c", "-o", str(fdc_o),
    ])
    run([
        ld, "-m", "elf_i386", "-T", "linker.ld",
        "-o", str(kernel_elf), str(entry), str(interrupts_o),
        str(usermode_o), str(sched_o), str(user_blob_o),
        str(kernel_o), str(gui_o), str(fdc_o),
    ])
    run([objcopy, "-O", "binary", str(kernel_elf), str(kernel_bin)])

    boot_data = boot.read_bytes()
    kernel_data = kernel_bin.read_bytes()
    if len(boot_data) != BOOT_SIZE:
        raise SystemExit(f"boot sector is {len(boot_data)} bytes, expected 512")
    if len(kernel_data) > KERNEL_MAX:
        raise SystemExit(f"kernel too large: {len(kernel_data)} > {KERNEL_MAX}")

    pack = build_pack([
        ("A.BIN", task_a_bin),
        ("B.BIN", task_b_bin),
        ("C.BIN", task_c_bin),
        ("HELLO.BIN", user_bin),
    ])

    image = bytearray(FLOPPY_SIZE)
    image[0:BOOT_SIZE] = boot_data
    image[BOOT_SIZE:BOOT_SIZE + len(kernel_data)] = kernel_data
    fs = build_minifs([
        ("DISK.BIN", disk_task_bin),
        ("HELLO.BIN", user_bin),
        ("MODTEST.BIN", modtest_bin),
        ("UMTEST.BIN", umtest_bin),
        ("PARENT.BIN", parent_bin),
        ("PARENT2.BIN", parent_block_bin),
        ("FD.BIN", fd_test_bin),
        ("CHILD5.BIN", child5_bin),
        ("STATUS.BIN", parent_status_bin),
        ("TEST.TXT", CDIR / "test.txt"),
        ("UMTEST.BIN", umtest_bin),
        ("PARENT.BIN", parent_bin),
        ("PARENT2.BIN", parent_block_bin),
        ("FD.BIN", fd_test_bin),
        ("CHILD5.BIN", child5_bin),
        ("STATUS.BIN", parent_status_bin),
        ("TEST.TXT", CDIR / "test.txt"),
    ])
    image[PACK_OFFSET:PACK_OFFSET + len(pack)] = pack
    image[FS_OFFSET:FS_OFFSET + len(fs)] = fs
    IMAGE.write_bytes(image)

    digest = hashlib.sha256(image).hexdigest()
    print(f"[boot]   {len(boot_data):>6} bytes")
    print(f"[kernel] {len(kernel_data):>6} bytes")
    print(f"[pack]   {len(pack):>6} bytes at offset {PACK_OFFSET}")
    print(f"[minifs] {len(fs):>6} bytes at offset {FS_OFFSET}")
    print(f"[image]  {IMAGE.name}: {len(image):,} bytes")
    print(f"[sha256] {digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())














