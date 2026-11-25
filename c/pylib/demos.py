import math
import sys
import minios

args = sys.argv[1:]
if args and args[0] in ("demos", "plot"):
    args = args[1:]
expr = " ".join(args).strip()

title = ("Plot: " + expr[:20]) if expr else "MiniOS Plot"
c = minios.Canvas(title)
w, h = c.size()
c.capture(True)

top = 24
bot = h - 18
left = 12
right = w - 12

xmin = -2.0 * math.pi
xmax = 2.0 * math.pi
ymin = -1.2
ymax = 1.2
hover_x = None
hover_y = None


def f(x):
    try:
        if expr:
            return float(eval(expr, {"x": x, "math": math, "sin": math.sin,
                                     "cos": math.cos, "tan": math.tan,
                                     "sqrt": math.sqrt, "abs": abs}))
        return math.sin(x)
    except Exception:
        return 0.0


def sx(x):
    return left + int((x - xmin) * (right - left) / (xmax - xmin))


def sy(y):
    return bot - int((y - ymin) * (bot - top) / (ymax - ymin))


def redraw():
    c.clear(16)
    for gx in range(left, right + 1, 28):
        c.line(gx, top, gx, bot, 235)
    for gy in range(top, bot + 1, max(16, (bot - top) // 4)):
        c.line(left, gy, right, gy, 235)
    ax = sx(0)
    ay = sy(0)
    if left <= ax <= right:
        c.line(ax, top, ax, bot, 244)
    if top <= ay <= bot:
        c.line(left, ay, right, ay, 244)
    px = None
    py = None
    for x in range(left, right + 1):
        xv = xmin + (x - left) * (xmax - xmin) / (right - left)
        yv = f(xv)
        if yv > ymax:
            yv = ymax
        elif yv < ymin:
            yv = ymin
        yy = sy(yv)
        if px is not None:
            c.line(px, py, x, yy, 51)
        px = x
        py = yy
    if hover_x is not None:
        hx = sx(hover_x)
        hy = sy(hover_y)
        c.line(hx, top, hx, bot, 214)
        c.line(left, hy, right, hy, 214)
        c.text(8, 6, "x=%.3f y=%.3f  arrows pan  +/- zoom  q quit" %
               (hover_x, hover_y), 15)
    else:
        c.text(8, 6, "%s  arrows pan  +/- zoom  q quit" %
               (expr if expr else "sin(x)"), 15)
    c.text(8, h - 10, "view x[%.2f,%.2f] y[%.2f,%.2f]" %
           (xmin, xmax, ymin, ymax), 11)
    c.present()


redraw()
while True:
    ev = c.event()
    if not ev:
        continue
    typ, x, y, buttons, key = ev[0], ev[1], ev[2], ev[3], ev[4]
    if typ == 5:
        break
    if typ == 4:
        if key in (ord("q"), ord("Q")):
            break
        dx = (xmax - xmin) * 0.1
        dy = (ymax - ymin) * 0.1
        if key in (ord("a"), ord("A"), 0xF9):
            xmin -= dx
            xmax -= dx
        elif key in (ord("d"), ord("D"), 0xFA):
            xmin += dx
            xmax += dx
        elif key in (ord("w"), ord("W"), 0xF7):
            ymin += dy
            ymax += dy
        elif key in (ord("s"), ord("S"), 0xF8):
            ymin -= dy
            ymax -= dy
        elif key == ord("+") or key == ord("="):
            mid = (xmin + xmax) / 2
            span = (xmax - xmin) * 0.8
            xmin = mid - span / 2
            xmax = mid + span / 2
        elif key == ord("-"):
            mid = (xmin + xmax) / 2
            span = (xmax - xmin) * 1.25
            xmin = mid - span / 2
            xmax = mid + span / 2
        redraw()
        continue
    if typ == 3 and left <= x <= right:
        hover_x = xmin + (x - left) * (xmax - xmin) / (right - left)
        hover_y = f(hover_x)
        if hover_y > ymax:
            hover_y = ymax
        elif hover_y < ymin:
            hover_y = ymin
        redraw()

c.close()
