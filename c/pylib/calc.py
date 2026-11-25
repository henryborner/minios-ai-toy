import sys

ARGV = sys.argv[1:]
if ARGV and ARGV[0] == "calc":
    ARGV = ARGV[1:]


def _signed(v, bits):
    v &= (1 << bits) - 1
    if v & (1 << (bits - 1)):
        v -= (1 << bits)
    return v


def _hex32(u):
    d = "0123456789ABCDEF"
    s = ""
    for i in range(7, -1, -1):
        s += d[(u >> (i * 4)) & 15]
    return s


def _bin32(u):
    s = ""
    for i in range(0, 32, 4):
        if s:
            s += " "
        for b in range(3, -1, -1):
            s += "1" if (u >> (31 - i - (3 - b))) & 1 else "0"
    return s


def _show(v):
    if isinstance(v, bool):
        v = int(v)
    if isinstance(v, int):
        u = v & 0xFFFFFFFF
        bits = []
        for i in range(32):
            if u & (1 << i):
                bits.append(str(i))
        print("DEC  " + str(v))
        print("HEX  0x" + _hex32(u))
        print("BIN  " + _bin32(u))
        print("SET  " + (" ".join(bits) if bits else "-"))
        print("U8/U16/U32  %u / %u / %u" % (u & 0xFF, u & 0xFFFF, u))
        print("S8/S16/S32  %d / %d / %d" %
              (_signed(u, 8), _signed(u, 16), _signed(u, 32)))
    elif isinstance(v, float):
        print("FLOAT " + repr(v))
    else:
        print(v)


def _eval(expr, last):
    ns = {"__builtins__": {}, "_": last}
    return eval(expr, ns, ns)


expr = " ".join(ARGV).strip()
if expr:
    try:
        _show(_eval(expr, 0))
    except Exception as e:
        print("calc error:", e)
else:
    print("calc multi-base workbench; type quit")
    last = 0
    while True:
        try:
            line = input("calc> ").strip()
        except EOFError:
            print()
            break
        if not line:
            continue
        if line in ("quit", "exit", "q"):
            break
        try:
            last = _eval(line, last)
            _show(last)
        except Exception as e:
            print("calc error:", e)
