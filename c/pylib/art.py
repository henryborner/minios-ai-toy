import minios

def C(r, g, b):
    return 16 + r * 36 + g * 6 + b

def px(x, y, col):
    if 0 <= x < 300 and 0 <= y < 200:
        minios.pixel(x, y, col)

c = minios.Canvas("Aurora")
c.clear(0)

for y in range(150):
    r = y * 3 // 149
    g = (120 - y) // 45
    if g < 0:
        g = 0
    b = 4 - (y * 2) // 149
    c.rect(0, y, 300, 1, C(r, g, b))

for y in range(150, 200):
    c.rect(0, y, 300, 1, C(0, 2, 2))

stars = [(25, 30), (45, 60), (70, 25), (95, 50), (120, 20),
         (270, 70), (285, 105), (250, 120), (205, 25),
         (190, 60), (160, 35), (140, 80), (110, 100)]

for p in stars:
    x = p[0]
    y = p[1]
    px(x, y, 15)
    px(x - 1, y, 7)
    px(x + 1, y, 7)
    px(x, y - 1, 7)
    px(x, y + 1, 7)

rad = [0, 4, 7, 9, 11, 12, 13, 14, 14, 14, 13, 12, 11, 9, 7, 4, 0]
for i in range(17):
    r = rad[i]
    c.rect(235 - r, 30 + i, 2 * r + 1, 1, C(4, 4, 3))

for x in range(20, 280, 4):
    top = 35 + (x * 7 // 11) % 18
    c.line(x, top, x + 3, 115, C(1, 5, 3))
    c.line(x + 2, top + 3, x + 5, 112, C(0, 4, 5))
    c.line(x + 4, top + 7, x + 7, 108, C(2, 2, 5))

for y in range(95, 160):
    half = (y - 95) // 2
    if half > 70:
        half = 70
    c.rect(80 - half, y, 2 * half + 1, 1, C(0, 1, 2))

for y in range(110, 160):
    half = (y - 110) // 3
    if half > 55:
        half = 55
    c.rect(215 - half, y, 2 * half + 1, 1, C(0, 1, 1))

for y in range(95, 112):
    width = (y - 94) // 2
    c.rect(80 - width, y, 2 * width + 1, 1, 15)

for x in range(20, 280, 10):
    c.rect(x, 160 + (x // 10) % 12, 6, 1, C(1, 3, 4))

c.rect(0, 0, 300, 2, C(2, 4, 5))
c.rect(0, 198, 300, 2, C(2, 4, 5))
c.text(14, 12, "AURORA", 15)
c.text(14, 24, "MINIOS / 256 COLORS", 11)
c.text(14, 184, "PYTHON CANVAS", 15)
