#!/usr/bin/env python3
"""Build and run the isolated in-kernel GUI damage test.

The normal image and normal objects are never modified.  A temporary GUI
object (the test translation unit) replaces gui.o, a temporary kernel image
is linked with the same flags/order as c/build_c.py, and two temporary
floppy images are booted in private QEMU processes:

  * 640x480 VBE (normal stage 2)
  * 320x200 VGA (stage 2 force_vga byte at LBA 321 + 0x1fd patched to 1)

Only the QEMU processes started here are terminated, after COM1 reports
PASS or FAIL.  No monitor or serial TCP port is used.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CDIR = ROOT / "c"
TEST_C = CDIR / "tests" / "gui_damage_test.c"
LINKER = CDIR / "linker.ld"
IMAGE = ROOT / "MiniOS-C.img"

FLOPPY_SIZE = 1_474_560
KERNEL_OFFSET = 512
KERNEL_SECTORS = 256
KERNEL_MAX = KERNEL_SECTORS * 512
STAGE2_LBA = 321
STAGE2_MARK = STAGE2_LBA * 512 + 0x1FD
BSS_LIMIT = 0x400000
BOOT_TIMEOUT = 120.0

CFLAGS = [
    "--target=i386-unknown-none-elf", "-m32", "-march=i586",
    "-ffreestanding", "-fno-pic", "-fno-pie", "-fno-stack-protector",
    "-fno-builtin", "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
    "-mno-sse", "-mno-mmx", "-mno-80387", "-msoft-float",
    "-Oz", "-ffunction-sections", "-fdata-sections", "-fomit-frame-pointer",
    "-fno-unroll-loops", "-Wall", "-Wextra",
]

OBJECTS = [
    "entry.o", "interrupts.o", "usermode.o", "sched.o", "user_blob.o",
    "kernel.o", "fdc.o",
]


def find_tool(names: list[str], extra: list[Path] | None = None) -> str:
    for name in names:
        found = shutil.which(name)
        if found:
            return found
    for path in extra or []:
        if path.exists():
            return str(path)
    raise SystemExit(f"tool not found: {names}")


def find_build_dir() -> Path:
    env = os.environ.get("MINIOS_C_BUILD")
    candidates: list[Path] = []
    if env:
        candidates.append(Path(env))
    candidates += [
        CDIR / "build",
        ROOT.parent / "c" / "build",
        ROOT.parent.parent / "c" / "build",
        ROOT / "build",
    ]
    for candidate in candidates:
        if all((candidate / name).exists() for name in OBJECTS):
            return candidate
    raise SystemExit(
        "c/build objects not found; build the normal image first "
        "(python c/build_c.py) or set MINIOS_C_BUILD"
    )


def find_image() -> Path:
    if IMAGE.exists():
        return IMAGE
    for candidate in [ROOT.parent / "MiniOS-C.img",
                      ROOT.parent.parent / "MiniOS-C.img"]:
        if candidate.exists():
            return candidate
    raise SystemExit(f"image not found: {IMAGE}")


def read_bytes(path: Path) -> bytes:
    try:
        return path.read_bytes()
    except (FileNotFoundError, PermissionError, OSError):
        return b""


def classify(label: str, text: str) -> str:
    fail_line = next(
        (line for line in text.splitlines() if "GUI_DAMAGE_TEST FAIL" in line),
        None,
    )
    if fail_line:
        raise SystemExit(f"[{label}] test failed: {fail_line.strip()}")
    pass_line = next(
        (line for line in text.splitlines() if "GUI_DAMAGE_TEST PASS" in line),
        None,
    )
    if not pass_line:
        tail = "\n".join(text.splitlines()[-20:])
        raise SystemExit(f"[{label}] no PASS on COM1; last serial output:\n{tail}")
    want_mode, want_res = ("VBE", "640x480") if label == "vbe" else ("VGA", "320x200")
    if f"mode={want_mode}" not in pass_line or f"resolution={want_res}" not in pass_line:
        raise SystemExit(
            f"[{label}] wrong video mode: {pass_line.strip()} "
            f"(wanted mode={want_mode} resolution={want_res})"
        )
    return pass_line.strip()


def run_qemu(qemu: str, image: Path, serial: Path, label: str) -> str:
    serial.unlink(missing_ok=True)
    stderr_path = serial.with_suffix(".stderr")
    cmd = [
        qemu,
        "-accel", "tcg",
        "-m", "64",
        "-drive", f"file={image.as_posix()},if=floppy,format=raw,index=0",
        "-boot", "a",
        "-vga", "std",
        "-monitor", "none",
        "-display", "none",
        "-serial", f"file:{serial.as_posix()}",
        "-no-reboot",
    ]
    creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    with open(stderr_path, "wb") as err:
        proc = subprocess.Popen(
            cmd, stdout=subprocess.DEVNULL, stderr=err,
            creationflags=creationflags,
        )
        deadline = time.time() + BOOT_TIMEOUT
        text = ""
        try:
            while time.time() < deadline:
                data = read_bytes(serial)
                if data:
                    text = data.decode("ascii", errors="replace")
                    if ("GUI_DAMAGE_TEST PASS" in text or
                            "GUI_DAMAGE_TEST FAIL" in text):
                        time.sleep(0.2)
                        text = read_bytes(serial).decode("ascii", errors="replace")
                        break
                if proc.poll() is not None:
                    text = read_bytes(serial).decode("ascii", errors="replace")
                    break
                time.sleep(0.1)
            else:
                text = read_bytes(serial).decode("ascii", errors="replace")
                raise SystemExit(f"[{label}] QEMU timeout after {BOOT_TIMEOUT:.0f}s")
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
    if not text:
        err = read_bytes(stderr_path).decode("ascii", errors="replace")
        raise SystemExit(f"[{label}] QEMU produced no serial output\n{err[-2000:]}")
    return classify(label, text)


def main() -> int:
    if not TEST_C.exists():
        raise SystemExit(f"missing test source: {TEST_C}")
    if not LINKER.exists():
        raise SystemExit(f"missing linker script: {LINKER}")

    build = find_build_dir()
    image_src = find_image()
    clang = find_tool(["clang"], [
        Path("C:/msys64/ucrt64/bin/clang.exe"),
        Path("C:/msys64/usr/bin/clang.exe"),
    ])
    ld = find_tool(["ld.lld", "ld.lld.exe"], [
        Path("C:/msys64/ucrt64/bin/ld.lld.exe"),
    ])
    objdump = find_tool(["objdump"], [
        Path("C:/msys64/ucrt64/bin/objdump.exe"),
        Path("C:/msys64/usr/bin/objdump.exe"),
    ])
    objcopy = find_tool(["objcopy"], [
        Path("C:/msys64/ucrt64/bin/objcopy.exe"),
        Path("C:/msys64/usr/bin/objcopy.exe"),
    ])
    qemu = find_tool(["qemu-system-i386", "qemu-system-i386w"], [
        Path("C:/msys64/ucrt64/bin/qemu-system-i386.exe"),
        Path("C:/Program Files/qemu/qemu-system-i386.exe"),
    ])

    temp = Path(tempfile.mkdtemp(prefix="gui-damage-"))
    try:
        test_obj = temp / "gui_damage_test.o"
        elf = temp / "kernel_damage.elf"
        kernel_bin = temp / "kernel_damage.bin"
        map_file = temp / "kernel_damage.map"

        subprocess.run(
            [clang, *CFLAGS, "-c", str(TEST_C), "-o", str(test_obj)],
            cwd=CDIR, check=True,
        )

        link_cmd = [
            ld, "-m", "elf_i386", "-T", str(LINKER),
            "--gc-sections", "-Map", str(map_file), "-o", str(elf),
        ]
        link_cmd += [str(build / name) for name in OBJECTS]
        link_cmd.append(str(test_obj))
        subprocess.run(link_cmd, cwd=CDIR, check=True)

        syms = subprocess.check_output([objdump, "-t", str(elf)], text=True)
        bss_end = None
        for line in syms.splitlines():
            if line.rstrip().endswith("__bss_end"):
                m = re.match(r"^([0-9a-fA-F]+)", line)
                if m:
                    bss_end = int(m.group(1), 16)
                break
        if bss_end is None:
            raise SystemExit("linker output has no __bss_end")

        subprocess.run(
            [objcopy, "-O", "binary", str(elf), str(kernel_bin)],
            check=True,
        )
        kernel_data = kernel_bin.read_bytes()
        if len(kernel_data) > KERNEL_MAX:
            raise SystemExit(
                f"test kernel code overflow: {len(kernel_data)} > {KERNEL_MAX}"
            )
        if bss_end >= BSS_LIMIT:
            raise SystemExit(f"test kernel BSS overflow: 0x{bss_end:x} >= 0x{BSS_LIMIT:x}")

        base = bytearray(image_src.read_bytes())
        if len(base) < FLOPPY_SIZE:
            base += bytes(FLOPPY_SIZE - len(base))
        if len(base) != FLOPPY_SIZE:
            raise SystemExit(f"unexpected floppy image size: {len(base)}")
        base[KERNEL_OFFSET:KERNEL_OFFSET + KERNEL_MAX] = bytes(KERNEL_MAX)
        base[KERNEL_OFFSET:KERNEL_OFFSET + len(kernel_data)] = kernel_data

        vbe_img = temp / "vbe.img"
        vga_img = temp / "vga.img"
        vbe_img.write_bytes(base)
        vga = bytearray(base)
        vga[STAGE2_MARK] = 1
        vga_img.write_bytes(vga)

        print(f"[build] code {len(kernel_data):,} / {KERNEL_MAX:,} B, "
              f"BSS end 0x{bss_end:x} / 0x{BSS_LIMIT:x}")
        print(f"[image] VBE {vbe_img.name}, VGA {vga_img.name} "
              f"(force_vga byte at offset {STAGE2_MARK})")

        results: dict[str, str] = {}
        errors: dict[str, str] = {}
        for label, img in (("vbe", vbe_img), ("vga", vga_img)):
            try:
                results[label] = run_qemu(
                    qemu, img, temp / f"{label}.serial", label
                )
            except SystemExit as exc:
                errors[label] = str(exc)
        for label in ("vbe", "vga"):
            if label in results:
                print(f"[{label}] {results[label]}")
            else:
                print(f"[{label}] {errors.get(label, 'not run')}")
        if errors:
            raise SystemExit(1)
        return 0
    finally:
        shutil.rmtree(temp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
