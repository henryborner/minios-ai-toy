import sys
import time
import minios

argv = sys.argv[1:]
if argv and argv[0] == "top":
    argv = argv[1:]
mode = argv[0] if argv else ""


def snapshot():
    v = minios.video()
    s = minios.sysinfo()
    return v, s


def line(v, s):
    free = s[2]
    total = s[3]
    used = total - free
    pct = (used * 100 // total) if total else 0
    print("\x1b[36mMiniOS top\x1b[0m  %dx%d %s  up=%ds" %
          (v[0], v[1], "VBE" if v[4] else "VGA", s[1]))
    print("ticks=%d  tasks=%d  last_mpy=%dms" % (s[0], s[5], s[6]))
    print("frames: %d/%d used (%d%%)  heap=%d/65536" %
          (used, total, pct, s[4]))
    bar = int((total - free) * 40 // total) if total else 0
    print("[" + "#" * bar + "." * (40 - bar) + "]")


if mode == "-w":
    for i in range(8):
        print("\x1b[2J\x1b[H", end="")
        v, s = snapshot()
        line(v, s)
        time.sleep_ms(500)
elif mode == "-g":
    c = minios.Canvas("top -g")
    c.batch(True)
    for i in range(8):
        v, s = snapshot()
        free, total = s[2], s[3]
        c.clear(0)
        c.text(8, 6, "MiniOS top -g", 15)
        c.text(8, 20, "frames %d/%d" % (free, total), 11)
        usedw = int((total - free) * 284 // total) if total else 0
        c.rect(8, 32, 284, 8, 8)
        c.rect(8, 32, usedw, 8, 2)
        c.text(8, 48, "heap %d/65536" % s[4], 11)
        c.text(8, 62, "tasks %d  ticks %d" % (s[5], s[0]), 14)
        c.text(8, 84, "uptime %ds" % s[1], 10)
        c.present()
        time.sleep_ms(300)
else:
    v, s = snapshot()
    line(v, s)
