#!/usr/bin/env python3
"""Build the freestanding C Paint raw UMP1-style task and inject it into a
copy of the current program pack. Does not touch c/build_c.py or MiniCC."""
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CDIR = ROOT / "c"
BUILD = CDIR / "build"
CLANG = Path(r"C:\msys64\ucrt64\bin\clang.exe")
LD = Path(r"C:\msys64\ucrt64\bin\ld.lld.exe")
OBJCOPY = Path(r"C:\msys64\ucrt64\bin\objcopy.exe")
PACK_OFFSET = (1 + 256) * 512
PACK_BYTES = 64 * 512


def run(cmd):
    print("$", " ".join(str(x) for x in cmd))
    subprocess.run([str(x) for x in cmd], check=True)


def build():
    obj = BUILD / "paint_c.o"
    elf = BUILD / "paint_c.elf"
    raw = BUILD / "paint_c.bin"
    flags = [
        "--target=i386-unknown-none-elf", "-m32", "-march=i586",
        "-ffreestanding", "-fno-pic", "-fno-pie", "-fno-stack-protector",
        "-fno-builtin", "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
        "-mno-sse", "-mno-mmx", "-mno-80387", "-msoft-float",
        "-Oz", "-ffunction-sections", "-fdata-sections",
        "-fomit-frame-pointer", "-Wall", "-Wextra",
        "-I", str(CDIR / "mpy_port"),
        "-c", str(CDIR / "paint_c.c"), "-o", str(obj),
    ]
    run([CLANG, *flags])
    run([LD, "-m", "elf_i386", "-T", str(CDIR / "paint_c.ld"),
         "-o", str(elf), str(obj)])
    run([OBJCOPY, "-O", "binary", str(elf), str(raw)])
    return raw


def inject(raw):
    # Reuse the existing pack builder without importing c/build_c.py, whose
    # MiniCC integration is still being edited by another agent.
    sys.path.insert(0, str(CDIR))
    from build_c import build_pack

    files = []
    for name, bin_name in (("A.BIN", "task_a.bin"), ("B.BIN", "task_b.bin"),
                           ("C.BIN", "task_c.bin"), ("HELLO.BIN", "user.bin")):
        p = BUILD / bin_name
        if not p.exists():
            raise SystemExit(f"missing {p}; run python c/build_c.py first")
        files.append((name, p))
    files.append(("PAINTC.BIN", raw))
    pack = build_pack(files)
    if len(pack) > PACK_BYTES:
        raise SystemExit("pack too large")

    src = ROOT / "MiniOS-C.img"
    dst = ROOT / "MiniOS-C-paintc.img"
    image = bytearray(src.read_bytes())
    if len(image) != 1_474_560:
        raise SystemExit("unexpected image size")
    image[PACK_OFFSET:PACK_OFFSET + len(pack)] = pack
    # zero any stale tail of the fixed pack area
    for i in range(PACK_OFFSET + len(pack), PACK_OFFSET + PACK_BYTES):
        image[i] = 0
    dst.write_bytes(image)
    print(f"[paintc] {raw.stat().st_size} bytes, pack {len(pack)} bytes")
    print(f"[paintc] wrote {dst}")


if __name__ == "__main__":
    inject(build())
