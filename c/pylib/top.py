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
    t = minios.tasks()
    return v, s, t


def state_name(st):
    return "R" if st == 2 else ("B" if st == 4 else ("S" if st == 1 else "?"))


def line(v, s, tasks):
    free = s[2]
    total = s[3]
    used = total - free
    pct = (used * 100 // total) if total else 0
    print("\x1b[36mMiniOS top\x1b[0m  %dx%d %s  up=%ds" %
          (v[0], v[1], "VBE" if v[4] else "VGA", s[1]))
    print("ticks=%d  tasks=%d  last_mpy=%dms" % (s[0], s[5], s[6]))
    print("frames: %d/%d used (%d%%)  heap=%d/65536" %
          (used, total, pct, s[4]))
    bar = int(used * 40 // total) if total else 0
    print("[" + "#" * bar + "." * (40 - bar) + "]")
    print("PID CON ST PGS NAME")
    for t in tasks[:6]:
        print("%3d %3d  %s %4d %s" % (t[0], t[1], state_name(t[2]), t[3], t[4]))


if mode == "-w":
    while True:
        print("\x1b[2J\x1b[H", end="")
        v, s, tasks = snapshot()
        line(v, s, tasks)
        k = minios.getchar_nb()
        if k >= 0 and (k == 3 or k == ord("q") or k == ord("Q")):
            break
        time.sleep_ms(500)
elif mode == "-g":
    c = minios.Canvas("top -g")
    c.batch(True)
    c.capture(True)
    while True:
        v, s, tasks = snapshot()
        free, total = s[2], s[3]
        c.clear(0)
        c.text(8, 6, "MiniOS top -g", 15)
        c.text(8, 20, "frames %d/%d" % (free, total), 11)
        usedw = int((total - free) * 284 // total) if total else 0
        c.rect(8, 32, 284, 8, 8)
        c.rect(8, 32, usedw, 8, 2)
        c.text(8, 48, "heap %d/65536" % s[4], 11)
        c.text(8, 62, "tasks %d  ticks %d" % (s[5], s[0]), 14)
        y = 76
        for t in tasks[:5]:
            c.text(8, y, "%d %s %d %s" % (t[0], state_name(t[2]), t[3], t[4]), 10)
            y += 10
        c.present()
        ev = c.event()
        if ev and ev[0] == 4 and (ev[4] == 3 or ev[4] == ord("q") or ev[4] == ord("Q")):
            break
        time.sleep_ms(300)
    c.close()
else:
    v, s, tasks = snapshot()
    line(v, s, tasks)
