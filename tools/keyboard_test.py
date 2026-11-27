#!/usr/bin/env python3
"""Headless keyboard mapping regression for MiniOS."""
from __future__ import annotations

import socket
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
IMAGE = ROOT / "MiniOS-C.img"
QEMU = Path(r"C:\msys64\ucrt64\bin\qemu-system-i386.exe")
CREATE_NO_WINDOW = 0x08000000


def free_port() -> int:
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


class Hmp:
    def __init__(self, port: int):
        deadline = time.time() + 10
        while True:
            try:
                self.s = socket.create_connection(("127.0.0.1", port), timeout=0.5)
                break
            except OSError:
                if time.time() > deadline:
                    raise
                time.sleep(0.1)
        self.s.settimeout(0.15)
        self.drain()

    def drain(self) -> str:
        out = b""
        while True:
            try:
                chunk = self.s.recv(65536)
            except socket.timeout:
                break
            if not chunk:
                break
            out += chunk
        return out.decode("latin1", "replace")

    def cmd(self, line: str) -> str:
        self.s.sendall((line + "\n").encode())
        time.sleep(0.08)
        return self.drain()

    def key(self, name: str) -> None:
        self.cmd("sendkey " + name)

    def type_text(self, text: str) -> None:
        for ch in text:
            if ch == "\n":
                self.key("ret")
            elif ch == " ":
                self.key("spc")
            elif ch.isalpha():
                self.key(ch.lower())
            elif ch.isdigit():
                self.key(ch)
            else:
                raise ValueError(ch)
            time.sleep(0.03)

    def close(self) -> None:
        try:
            self.s.close()
        except OSError:
            pass


def wait_for(path: Path, needle: str, timeout: float = 2.0) -> str:
    deadline = time.time() + timeout
    text = ""
    while time.time() < deadline:
        try:
            text = path.read_text(errors="replace")
        except FileNotFoundError:
            text = ""
        if needle in text:
            return text
        time.sleep(0.03)
    raise SystemExit(f"missing {needle!r}; serial tail:\n{text[-2000:]}")


def expect(h: Hmp, path: Path, key: str, needle: str) -> None:
    h.key(key)
    time.sleep(0.12)
    wait_for(path, needle)
    print(f"PASS key={key:14s} {needle}")


def main() -> int:
    if not IMAGE.exists():
        raise SystemExit("missing MiniOS-C.img")
    td = Path(tempfile.mkdtemp(prefix="minios-kbd-"))
    serial = td / "serial.log"
    port = free_port()
    proc = subprocess.Popen(
        [str(QEMU), "-name", "MiniOS-KeyHeadless", "-fda", str(IMAGE),
         "-boot", "a", "-m", "128", "-vga", "std", "-display", "none",
         "-monitor", f"tcp:127.0.0.1:{port},server,nowait",
         "-serial", f"file:{serial}", "-rtc", "base=localtime"],
        creationflags=CREATE_NO_WINDOW,
    )
    h = None
    try:
        time.sleep(5.5)
        h = Hmp(port)
        h.type_text("keydebug\n")
        time.sleep(0.3)
        wait_for(serial, "KEYDEBUG on")
        print("PASS keydebug enabled")

        expect(h, serial, "shift_r-a", "KBD CH=41")
        expect(h, serial, "caps_lock", "KBD SC=3A")
        expect(h, serial, "a", "KBD CH=41")
        expect(h, serial, "caps_lock", "KBD SC=3A")
        expect(h, serial, "a", "KBD CH=61")

        expect(h, serial, "kp_7", "KBD CH=37")
        expect(h, serial, "num_lock", "KBD SC=45")
        expect(h, serial, "kp_7", "KBD CH=FB")
        expect(h, serial, "num_lock", "KBD SC=45")
        expect(h, serial, "kp_7", "KBD CH=37")

        for key, code in (("kp_0", "30"), ("kp_5", "35"),
                          ("kp_add", "2B"), ("kp_subtract", "2D"),
                          ("kp_multiply", "2A"), ("kp_divide", "2F"),
                          ("kp_decimal", "2E"), ("kp_enter", "0A")):
            expect(h, serial, key, "KBD CH=" + code)

        for key, code in (("up", "F7"), ("down", "F8"), ("left", "F9"),
                          ("right", "FA"), ("home", "FB"), ("end", "FC"),
                          ("insert", "F6"), ("delete", "FD"),
                          ("pgup", "FE"), ("pgdn", "FF")):
            expect(h, serial, key, "KBD CH=" + code)

        expect(h, serial, "f1", "KBD SC=3B")
        expect(h, serial, "f12", "KBD SC=58")
        print("KEYBOARD TEST PASS")
        return 0
    finally:
        if h:
            try:
                h.cmd("quit")
            except OSError:
                pass
            h.close()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
        # keep the temp directory for diagnosis; the OS cleans it later


if __name__ == "__main__":
    raise SystemExit(main())
