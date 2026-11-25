import sys
import minios

args = sys.argv[1:]
if args and args[0] == "xxd":
    args = args[1:]
if not args:
    print("usage: xxd FILE [OFFSET] [LEN]")
else:
    name = args[0]
    off = 0
    length = -1
    try:
        if len(args) > 1:
            off = int(args[1], 0)
        if len(args) > 2:
            length = int(args[2], 0)
    except Exception:
        print("bad offset/length")
        off = -1
    if off >= 0:
        try:
            f = open(name, "rb")
            data = f.read()
            f.close()
            width = 16 if minios.video()[0] >= 640 else 8
            if off > len(data):
                off = len(data)
            end = len(data) if length < 0 else min(off + length, len(data))
            pos = off
            while pos < end:
                chunk = data[pos:min(pos + width, end)]
                hx = ""
                for b in chunk:
                    hx += "%02x " % b
                hx += " " * (width * 3 - len(hx))
                asc = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
                print("%04x: %s|%s" % (pos, hx, asc))
                pos += width
        except Exception as e:
            print("xxd error:", e)
