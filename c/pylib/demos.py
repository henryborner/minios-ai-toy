import math
import sys
import minios

args = sys.argv[1:]
if args and args[0] in ("demos", "plot"):
    args = args[1:]
expr = " ".join(args).strip()

title = ("Plot: " + expr[:20]) if expr else "MiniOS Canvas Plot"
c = minios.Canvas(title)
w, h = c.size()
c.clear(16)

# Outer frame & subtle grid
plot_top = 22
plot_bot = h - 22
plot_h = plot_bot - plot_top
mid_y = plot_top + plot_h // 2

for gx in range(20, w - 8, 28):
    c.line(gx, plot_top, gx, plot_bot, 235)
for gy in range(plot_top, plot_bot + 1, max(16, plot_h // 4)):
    c.line(8, gy, w - 8, gy, 235)

c.line(8, mid_y, w - 8, mid_y, 244)
c.line(w // 2, plot_top, w // 2, plot_bot, 244)
c.rect(7, plot_top, w - 14, 1, 248)
c.rect(7, plot_bot, w - 14, 1, 248)

c.text(8, 6, "%dx%d 256-Color Canvas" % (w, h), 15)

amp = (plot_h // 2) - 6
px_prev = None
py_prev = None
py2_prev = None
span = w - 18

for i in range(span):
    px = 9 + i
    x = (i / float(max(1, span - 1))) * 4.0 * math.pi - 2.0 * math.pi
    try:
        y1 = float(eval(expr, {"x": x, "math": math, "sin": math.sin, "cos": math.cos})) if expr else math.sin(x)
    except Exception:
        y1 = 0.0
    if y1 > 1.2:
        y1 = 1.2
    elif y1 < -1.2:
        y1 = -1.2
    py = int(mid_y - y1 * amp)
    if px_prev is not None:
        c.line(px_prev, py_prev, px, py, 51)
    if not expr:
        y2 = 0.65 * math.cos(x * 1.5)
        py2 = int(mid_y - y2 * amp)
        if px_prev is not None:
            c.line(px_prev, py2_prev, px, py2, 214)
        py2_prev = py2
    px_prev = px
    py_prev = py

# 216-color cube spectrum ribbon at bottom
bar_w = w - 16
for i in range(bar_w):
    col = 16 + (i * 215) // max(1, bar_w - 1)
    c.rect(8 + i, h - 14, 1, 8, col)

print("Canvas %dx%d rendered: %s" % (w, h, expr if expr else "sin(x) & 0.65*cos(1.5x)"))
