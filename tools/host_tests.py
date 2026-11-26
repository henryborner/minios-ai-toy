#!/usr/bin/env python3
"""Host-side tests for the frame allocator and E820 normalizer."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CC = r"C:\msys64\ucrt64\bin\clang.exe"
OUT = ROOT / "c" / "build"
TESTS = [
    ("frame_alloc_test", ROOT / "c/tests/frame_alloc_test.c"),
    ("memmap_test", ROOT / "c/tests/memmap_test.c"),
]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name, src in TESTS:
        exe = OUT / (name + ".exe")
        cmd = [CC, "-std=c99", "-O2", "-I", str(ROOT / "c"),
               str(src), "-o", str(exe)]
        r = subprocess.run(cmd)
        if r.returncode:
            raise SystemExit("compile failed: " + name)
        r = subprocess.run([str(exe)])
        if r.returncode:
            raise SystemExit("test failed: " + name)
    print("HOST TESTS PASS")


if __name__ == "__main__":
    main()
