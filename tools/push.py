#!/usr/bin/env python3
"""Push a file into MiniOS RAMFS over the QEMU serial TCP port.

Guest side:
    upload NAME.PY

Host side:
    python tools/push.py my_script.py

QEMU must be started with:
    -serial tcp:127.0.0.1:5555,server,nowait
"""

import argparse
import socket
import sys
import time


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=5555)
    ap.add_argument("--run", action="store_true",
                    help="print the guest run command after upload")
    args = ap.parse_args()
    data = open(args.file, "rb").read()
    s = socket.create_connection((args.host, args.port), timeout=5)
    s.sendall(data)
    s.sendall(b"\x1a")
    time.sleep(0.5)
    s.close()
    print("pushed %d bytes to %s" % (len(data), args.file))
    if args.run:
        print("now type in guest: py %s" % args.file)


if __name__ == "__main__":
    main()
