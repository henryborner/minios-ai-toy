#!/usr/bin/env python3
"""Build MiniOS: assemble boot sector + kernel, pad to an exact 1.44 MB floppy."""
from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"
IMAGE = ROOT / "MiniOS.img"
FLOPPY_SIZE = 1_474_560  # 80 * 2 * 18 * 512 = 1.44 MB exactly
BOOT_SECTORS = 143      # must match TRACKS in boot.asm


def find_nasm() -> str:
    candidates = [
        ROOT / "tools" / "nasm-2.16.03" / "nasm.exe",
        ROOT / "tools" / "nasm" / "nasm.exe",
    ]
    for candidate in candidates:
        if candidate.exists():
            return str(candidate)
    found = shutil.which("nasm")
    if found:
        return found
    raise SystemExit("nasm not found: put it in tools/nasm-2.16.03/ or on PATH")


def assemble(nasm: str, source: str, output: Path) -> None:
    print(f"[nasm] {source} -> {output.relative_to(ROOT)}")
    subprocess.run([nasm, "-f", "bin", "-Wall", "-w-reloc-abs-word",
                    "-o", str(output), source], cwd=ROOT, check=True)


def main() -> int:
    BUILD.mkdir(exist_ok=True)
    nasm = find_nasm()
    boot_bin = BUILD / "boot.bin"
    kernel_bin = BUILD / "kernel.bin"
    assemble(nasm, "boot.asm", boot_bin)
    assemble(nasm, "kernel.asm", kernel_bin)

    boot = boot_bin.read_bytes()
    kernel = kernel_bin.read_bytes()
    if len(boot) != 512:
        raise SystemExit(f"boot sector is {len(boot)} bytes, expected 512")
    if len(kernel) > BOOT_SECTORS * 512 - 512:
        raise SystemExit(f"kernel too large: {len(kernel)} bytes")

    image = bytearray(FLOPPY_SIZE)
    image[0:512] = boot
    image[512:512 + len(kernel)] = kernel
    IMAGE.write_bytes(image)

    print(f"[boot]   {len(boot):>6} bytes")
    print(f"[kernel] {len(kernel):>6} bytes")
    print(f"[image]  {IMAGE.name}: {len(image):,} bytes "
          f"({len(image) / 1_048_576:.2f} MiB)")
    print(f"[slack]  {FLOPPY_SIZE - len(boot) - len(kernel):,} bytes free on floppy")
    return 0


if __name__ == "__main__":
    sys.exit(main())
