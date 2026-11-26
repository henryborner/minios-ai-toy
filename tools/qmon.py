#!/usr/bin/env python3
"""Tiny QEMU HMP client used to drive and screenshot the headless MiniOS VM."""
from __future__ import annotations

import os
import socket
import sys
import time
from pathlib import Path

from PIL import Image

HOST = "127.0.0.1"
PORT = int(os.environ.get("MINIOS_MON_PORT", "4444"))


class Monitor:
    def __init__(self) -> None:
        self.sock = socket.create_connection((HOST, PORT), timeout=3)
        self.sock.settimeout(0.4)
        self._drain()

    def _drain(self) -> str:
        data = b""
        while True:
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                break
            if not chunk:
                break
            data += chunk
        return data.decode("utf-8", "replace")

    def cmd(self, command: str, settle: float = 0.15) -> str:
        self.sock.sendall(command.encode("latin-1") + b"\n")
        time.sleep(settle)
        return self._drain()

    def key(self, *keys: str, settle: float = 0.05) -> None:
        for key in keys:
            self.cmd(f"sendkey {key}", settle=settle)

    def type_text(self, text: str) -> None:
        keymap = {
            " ": "spc", "-": "minus", "=": "equal", ".": "dot", ",": "comma",
            "/": "slash", "\\": "backslash", ";": "semicolon", "'": "apostrophe",
            "[": "bracket_left", "]": "bracket_right", "\n": "ret",
            "\t": "tab",
            "!": "shift-1", "@": "shift-2", "#": "shift-3", "$": "shift-4",
            "%": "shift-5", "^": "shift-6", "&": "shift-7", "*": "shift-8",
            "(": "shift-9", ")": "shift-0", "_": "shift-minus",
            "+": "shift-equal", "{": "shift-bracket_left",
            "}": "shift-bracket_right", "|": "shift-backslash",
            ":": "shift-semicolon", '"': "shift-apostrophe",
            "~": "shift-grave_accent", "<": "shift-comma",
            ">": "shift-dot", "?": "shift-slash",
        }
        for ch in text:
            if ch.isalpha() and ch.isupper():
                self.key(f"shift-{ch.lower()}")
            elif ch.isdigit() or ch.isalpha():
                self.key(ch.lower())
            elif ch in keymap:
                self.key(keymap[ch])
            else:
                raise ValueError(f"unmapped character {ch!r}")

    def screenshot(self, path: str | Path, settle: float = 0.35) -> Path:
        path = Path(path).resolve()
        path.parent.mkdir(parents=True, exist_ok=True)
        ppm = path.with_suffix(".ppm")
        self.cmd(f'screendump "{ppm.as_posix()}"', settle=settle)
        if not ppm.exists():
            raise RuntimeError(f"screendump failed: {ppm}")
        with Image.open(ppm) as image:
            image.convert("RGB").save(path)
        ppm.unlink(missing_ok=True)
        return path


def main() -> int:
    m = Monitor()
    if len(sys.argv) < 2:
        print("usage: qmon.py shot <file> | key <key>... | type <text> | raw <hmp>")
        return 2
    action = sys.argv[1]
    if action == "shot":
        print(m.screenshot(sys.argv[2]))
    elif action == "key":
        m.key(*sys.argv[2:])
    elif action == "type":
        m.type_text(sys.argv[2])
    elif action == "raw":
        print(m.cmd(sys.argv[2], settle=0.5))
    else:
        print(f"unknown action {action}")
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
