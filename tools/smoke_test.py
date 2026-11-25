#!/usr/bin/env python3
"""MiniOS smoke test: boot both video modes and exercise basic commands.

The long-term 0-frame-leak assertion needs a guest metrics channel; this
version verifies boot, command injection, and non-blank screenshots.
"""
import re
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
IMAGE = ROOT / "MiniOS-C.img"
QEMU = r"C:\msys64\ucrt64\bin\qemu-system-i386.exe"
MON = 4466
KEYMAP = {
    " ": "spc", "\n": "ret", "-": "minus", ".": "dot", "/": "slash",
    "=": "equal", "*": "shift-8", "(": "shift-9", ")": "shift-0",
    "+": "shift-equal", ":": "shift-semicolon", "_": "shift-minus",
}


class Hmp:
    def __init__(self, port):
        self.s = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.s.settimeout(0.2)
        time.sleep(0.2)
        self._drain()

    def _drain(self):
        try:
            while self.s.recv(4096):
                pass
        except Exception:
            pass

    def cmd(self, line):
        self.s.sendall((line + "\n").encode())
        time.sleep(0.03)
        self._drain()

    def key(self, k):
        self.cmd("sendkey " + k)

    def type(self, text):
        for c in text:
            if c == "\n":
                self.key("ret")
            else:
                self.key(KEYMAP.get(c, c))
            time.sleep(0.02)


def image_nonblank(path):
    data = Path(path).read_bytes()
    if not data.startswith(b"P6"):
        return False
    parts = data.split(maxsplit=4)
    if len(parts) < 5:
        return False
    w, h = int(parts[1]), int(parts[2])
    pix = parts[4]
    if len(pix) < w * h * 3:
        return False
    return len(set(pix[: w * h * 3])) > 2


def parse_metrics(log):
    try:
        vals = re.findall(rb"\[metrics\] free=(\d+) total=(\d+)", log.read_bytes())
        return vals[-1] if vals else None
    except Exception:
        return None


def boot(img, serial_log, display_name):
    mon = f"tcp:127.0.0.1:{MON},server,nowait"
    p = subprocess.Popen([
        QEMU, "-name", display_name,
        "-drive", f"file={img},if=floppy,format=raw,readonly=on",
        "-boot", "a", "-m", "128", "-vga", "std",
        "-display", "none", "-monitor", mon,
        "-serial", f"file:{serial_log}", "-rtc", "base=localtime",
    ], stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    time.sleep(4.0)
    return p, Hmp(MON)


def exercise(prefix, img, serial):
    shot1 = ROOT / f"screenshots/smoke-{prefix}-boot.ppm"
    shot2 = ROOT / f"screenshots/smoke-{prefix}-cmd.ppm"
    p, h = boot(img, serial, "MiniOS-smoke-" + prefix)
    try:
        h.cmd(f"screendump {shot1.as_posix()}")
        time.sleep(0.3)
        if not image_nonblank(shot1):
            raise AssertionError("blank boot screen: " + prefix)
        h.type("calc 2**10\n")
        time.sleep(1.2)
        h.type("selftest\n")
        time.sleep(0.8)
        if b"[selftest] PASS" not in Path(serial).read_bytes():
            raise AssertionError("guest selftest failed: " + prefix)
        h.type("metrics\n")
        time.sleep(0.5)
        before = parse_metrics(Path(serial))
        h.type("calc 2**10\n")
        time.sleep(1.0)
        h.type("selftest\n")
        time.sleep(0.8)
        h.type("metrics\n")
        time.sleep(0.5)
        after = parse_metrics(Path(serial))
        if before is None or after is None:
            raise AssertionError("missing metrics: " + prefix)
        if before != after:
            raise AssertionError("frame leak %s -> %s (%s)" % (before, after, prefix))
        h.cmd(f"screendump {shot2.as_posix()}")
        time.sleep(0.3)
        if not image_nonblank(shot2):
            raise AssertionError("blank command screen: " + prefix)
    finally:
        h.cmd("quit")
        try:
            p.wait(timeout=5)
        except Exception:
            p.kill()
    return shot1, shot2


def main():
    if not IMAGE.exists():
        raise SystemExit("missing MiniOS-C.img")
    if IMAGE.stat().st_size != 1474560:
        raise SystemExit("bad image size %d" % IMAGE.stat().st_size)
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        serial = td / "serial.log"
        # 640x480 normal boot
        exercise("640", IMAGE, serial)
        # force VGA fallback
        vga = td / "MiniOS-320.img"
        shutil.copyfile(IMAGE, vga)
        b = bytearray(vga.read_bytes())
        b[321 * 512 + 0x1FD] = 1
        vga.write_bytes(b)
        exercise("320", vga, td / "serial320.log")
    print("SMOKE PASS: 640x480 + forced 320x200 boot, command, screenshot and warmed 0-leak checks")


if __name__ == "__main__":
    main()

