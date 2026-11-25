import sys
import time
import minios

name = sys.argv[1] if len(sys.argv) > 1 else "ART.PIC"
if name in ("paint",):
    name = "ART.PIC"

c = minios.Canvas("Paint")
W, H = c.size()
pix = bytearray(W * H)
for _i in range(W * H):
    pix[_i] = 15
tool = "P"
color = 0
running = True
drag = None
base = None

TOOLS = [("P", 2, "Pencil"), ("L", 16, "Line"), ("R", 32, "Rect"),
         ("C", 50, "Circle"), ("F", 68, "Fill"), ("S", 86, "Save"), ("Q", 104, "Quit")]
COLORS = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]
PALY = H - 16
PW = max(6, W // 16)


def put(x, y, c):
    if 22 <= x < W and 4 <= y < 182:
        pix[y * W + x] = c


def line(x0, y0, x1, y1, c):
    dx = abs(x1 - x0)
    sx = 1 if x0 < x1 else -1
    dy = -abs(y1 - y0)
    sy = 1 if y0 < y1 else -1
    err = dx + dy
    while True:
        put(x0, y0, c)
        if x0 == x1 and y0 == y1:
            break
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def rect(x0, y0, x1, y1, c):
    if x0 > x1:
        x0, x1 = x1, x0
    if y0 > y1:
        y0, y1 = y1, y0
    for x in range(x0, x1 + 1):
        put(x, y0, c)
        put(x, y1, c)
    for y in range(y0, y1 + 1):
        put(x0, y, c)
        put(x1, y, c)


def circle(cx, cy, r, c):
    x = r
    y = 0
    while x >= y:
        for sx, sy in ((x, y), (y, x), (-x, y), (-y, x),
                       (-x, -y), (-y, -x), (x, -y), (y, -x)):
            put(cx + sx, cy + sy, c)
        y += 1
        if x * x + y * y > r * r:
            x -= 1


def fill(x, y, c):
    if not (22 <= x < W and 4 <= y < 182):
        return
    old = pix[y * W + x]
    if old == c:
        return
    stack = [(x, y)]
    while stack:
        cx, cy = stack.pop()
        if cx < 22 or cx >= W or cy < 4 or cy >= 182:
            continue
        i = cy * W + cx
        if pix[i] != old:
            continue
        pix[i] = c
        stack.append((cx + 1, cy))
        stack.append((cx - 1, cy))
        stack.append((cx, cy + 1))
        stack.append((cx, cy - 1))


def draw():
    c.clear(0)
    c.blit(0, 0, W, H, bytes(pix))
    for label, y, _ in TOOLS:
        c.text(3, y, label, 15)
    for i, col in enumerate(COLORS):
        x = i * PW + 2
        c.rect(x, PALY, PW - 2, 14, col)
    c.text(22, PALY - 12, "tool", 15)
    c.text(58, PALY - 12, tool, 14)
    c.text(80, PALY - 12, "color", 15)
    c.rect(120, PALY - 12, 12, 8, color)
    c.present()


def load():
    try:
        f = open(name, "r")
        text = f.read()
        f.close()
        parts = text.split()
        if len(parts) < 2:
            return False
        w = int(parts[0])
        h = int(parts[1])
        if w != W or h != H:
            return False
        pos = 0
        for tok in parts[2:]:
            a = tok.split(":")
            if len(a) != 2:
                continue
            n = int(a[0])
            c = int(a[1])
            for _ in range(n):
                if pos < W * H:
                    pix[pos] = c
                    pos += 1
        return pos == W * H
    except Exception:
        return False


def save():
    parts = ["%d %d\n" % (W, H)]
    last = pix[0]
    run = 0
    for v in pix:
        if v == last and run < 255:
            run += 1
        else:
            parts.append("%d:%d " % (run, last))
            last = v
            run = 1
    parts.append("%d:%d\n" % (run, last))
    f = open(name, "w")
    f.write("".join(parts))
    f.close()


loaded = load()
c.capture(True)
draw()

while running:
    ev = c.event()
    if ev:
        typ, x, y, buttons, key = ev[0], ev[1], ev[2], ev[3], ev[4]
        if typ == 4:
            k = key
            if k in (ord("q"), ord("Q")):
                running = False
            elif k in (ord("s"), ord("S")):
                save()
            elif k in (ord("c"), ord("C")):
                for i in range(W * H):
                    pix[i] = 15
                draw()
            elif k in (ord("p"), ord("P")):
                tool = "P"
                draw()
            continue
        if typ == 5:
            running = False
            continue
        if typ == 1:
            if x >= 22 and y >= PALY:
                idx = (x - 2) // PW
                if 0 <= idx < 16:
                    color = COLORS[idx]
                    draw()
                    continue
            if x < 22:
                for label, ty, _ in TOOLS:
                    if ty <= y < ty + 12:
                        if label == "S":
                            save()
                        elif label == "Q":
                            running = False
                        elif label in ("P", "L", "R", "C", "F"):
                            tool = label
                            draw()
                        break
                continue
            if tool == "F":
                fill(x, y, color)
                draw()
            elif tool == "P":
                drag = (x, y)
                line(x, y, x, y, color)
            else:
                drag = (x, y)
                base = bytearray(pix)
        elif typ == 3 and (buttons & 1):
            if tool == "P" and drag:
                line(drag[0], drag[1], x, y, color)
                drag = (x, y)
                draw()
            elif base and drag:
                pix[:] = base
                if tool == "L":
                    line(drag[0], drag[1], x, y, color)
                elif tool == "R":
                    rect(drag[0], drag[1], x, y, color)
                elif tool == "C":
                    dx = x - drag[0]
                    dy = y - drag[1]
                    r = int((dx * dx + dy * dy) ** 0.5)
                    circle(drag[0], drag[1], r, color)
                draw()
        elif typ == 2 and base and drag:
            if tool == "L":
                line(drag[0], drag[1], x, y, color)
            elif tool == "R":
                rect(drag[0], drag[1], x, y, color)
            elif tool == "C":
                dx = x - drag[0]
                dy = y - drag[1]
                r = int((dx * dx + dy * dy) ** 0.5)
                circle(drag[0], drag[1], r, color)
            base = None
            drag = None
            draw()
    time.sleep_ms(20)

c.close()
