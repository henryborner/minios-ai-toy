import sys

if len(sys.argv) < 2:
    print("usage: xxd <file>")
else:
    try:
        f = open(sys.argv[1], "rb")
        data = f.read()
        f.close()
        for off in range(0, len(data), 8):
            chunk = data[off:off + 8]
            hx = "".join("%02x" % b for b in chunk)
            asc = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
            print("%04x %s %s" % (off, hx, asc))
    except Exception as e:
        print("xxd error:", e)
