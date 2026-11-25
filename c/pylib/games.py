import sys
import time
import minios

mode = sys.argv[1] if len(sys.argv) > 1 else "snake"
if mode == "games":
    mode = "snake"
if mode not in ("snake", "life"):
    mode = "snake"

seed = minios.sysinfo()[0] | 1


def rnd(n):
    global seed
    seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
    return seed % n


UP, DOWN, LEFT, RIGHT = 0xF7, 0xF8, 0xF9, 0xFA
CELL = 8
COLS = 300 // CELL
ROWS = 180 // CELL


def load_hs():
    try:
        f = open("SNAKE.HS", "r")
        v = int(f.read().strip())
        f.close()
        return v
    except Exception:
        return 0


def save_hs(v):
    try:
        f = open("SNAKE.HS", "w")
        f.write(str(v))
        f.close()
    except Exception:
        pass


def run_snake():
    c = minios.Canvas("Snake")
    c.capture(True)
    snake = [(10, 10), (9, 10), (8, 10)]
    d = RIGHT
    food = (rnd(COLS), rnd(ROWS))
    score = 0
    hs = load_hs()
    alive = True
    tick = 0
    last = 0
    while True:
        if alive:
            tick += 1
        ev = c.event()
        if ev:
            if ev[0] == 5:
                break
            if ev[0] == 4:
                k = ev[4]
                if k in (ord("q"), ord("Q")):
                    break
                if k in (ord("r"), ord("R")):
                    snake = [(10, 10), (9, 10), (8, 10)]
                    d = RIGHT
                    food = (rnd(COLS), rnd(ROWS))
                    score = 0
                    alive = True
                elif k in (UP, ord("w"), ord("W")) and d != DOWN:
                    d = UP
                elif k in (DOWN, ord("s"), ord("S")) and d != UP:
                    d = DOWN
                elif k in (LEFT, ord("a"), ord("A")) and d != RIGHT:
                    d = LEFT
                elif k in (RIGHT, ord("d"), ord("D")) and d != LEFT:
                    d = RIGHT
        now = time.ticks_ms()
        if alive and now - last >= 140:
            last = now
            hx, hy = snake[0]
            if d == UP:
                hy -= 1
            elif d == DOWN:
                hy += 1
            elif d == LEFT:
                hx -= 1
            else:
                hx += 1
            if hx < 0 or hx >= COLS or hy < 0 or hy >= ROWS or (hx, hy) in snake:
                alive = False
            else:
                snake.insert(0, (hx, hy))
                if (hx, hy) == food:
                    score += 1
                    minios.beep(880, 25)
                    if score > hs:
                        hs = score
                        save_hs(hs)
                    while food in snake:
                        food = (rnd(COLS), rnd(ROWS))
                else:
                    snake.pop()
        c.clear(16)
        c.text(4, 2, "SNAKE score=%d hi=%d  arrows/WASD  r restart  q quit" % (score, hs), 15)
        c.rect(food[0] * CELL + 2, 14 + food[1] * CELL + 2, CELL - 4, CELL - 4, 4)
        for i, (x, y) in enumerate(snake):
            c.rect(x * CELL + 1, 14 + y * CELL + 1, CELL - 2, CELL - 2,
                   15 if i == 0 else 10)
        if not alive:
            c.text(100, 100, "GAME OVER", 4)
            c.text(92, 112, "r restart / q quit", 15)
        c.present()
        time.sleep_ms(20)
    c.close()


def run_life():
    c = minios.Canvas("Life")
    c.capture(True)
    w, h = 60, 36
    cells = [[0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            cells[y][x] = 1 if rnd(100) < 24 else 0

    def step():
        nxt = [[0] * w for _ in range(h)]
        for y in range(h):
            for x in range(w):
                n = 0
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        if dx or dy:
                            n += cells[(y + dy) % h][(x + dx) % w]
                nxt[y][x] = 1 if (cells[y][x] and n in (2, 3)) or (not cells[y][x] and n == 3) else 0
        for y in range(h):
            cells[y][:] = nxt[y]

    while True:
        c.clear(16)
        c.text(4, 2, "LIFE  space step  r random  q quit", 15)
        for y in range(h):
            for x in range(w):
                if cells[y][x]:
                    c.rect(4 + x * 4, 16 + y * 4, 4, 4, 10)
        c.present()
        ev = c.event()
        if ev:
            if ev[0] == 5:
                break
            if ev[0] == 1 and 4 <= ev[1] < 4 + w * 4 and 16 <= ev[2] < 16 + h * 4:
                gx = (ev[1] - 4) // 4
                gy = (ev[2] - 16) // 4
                cells[gy][gx] = 0 if cells[gy][gx] else 1
                continue
            if ev[0] == 4:
                k = ev[4]
                if k in (ord("q"), ord("Q")):
                    break
                if k == 32:
                    step()
                elif k in (ord("r"), ord("R")):
                    for y in range(h):
                        for x in range(w):
                            cells[y][x] = 1 if rnd(100) < 24 else 0
        time.sleep_ms(50)
    c.close()


if mode == "life":
    run_life()
else:
    run_snake()
