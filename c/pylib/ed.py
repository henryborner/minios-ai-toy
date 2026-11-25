import sys
import minios

name = sys.argv[1] if len(sys.argv) > 1 else "NOTES.TXT"
if name in ("ed", "edit"):
    name = "NOTES.TXT"


def load():
    try:
        f = open(name, "r")
        text = f.read()
        f.close()
        lines = text.split("\n")
        if lines and lines[-1] == "":
            lines.pop()
        return lines if lines else [""]
    except Exception:
        return [""]


def save(lines):
    f = open(name, "w")
    f.write("\n".join(lines) + "\n")
    f.close()


def key():
    ch = chr(minios.getchar())
    if ch == "\x1b":
        a = chr(minios.getchar())
        if a == "[":
            b = chr(minios.getchar())
            if b == "A":
                return "UP"
            if b == "B":
                return "DOWN"
            if b == "C":
                return "RIGHT"
            if b == "D":
                return "LEFT"
            if b == "3":
                minios.getchar()
                return "DEL"
            return ""
        return ""
    return ch


lines = load()
row = 0
col = 0
mod = 0
msg = "Ctrl-S save  Ctrl-W sync  Ctrl-R save+run  Ctrl-Q quit"


def render():
    print("\x1b[2J\x1b[H", end="")
    print("\x1b[36mED %s%s  row %d col %d\x1b[0m" %
          (name, " [Modified]" if mod else "", row + 1, col + 1))
    print("\x1b[90m" + "-" * 70 + "\x1b[0m")
    for i, ln in enumerate(lines):
        mark = ">" if i == row else " "
        num = "%3d " % (i + 1)
        if i == row:
            print("\x1b[1m" + mark + num + ln + "\x1b[0m")
        else:
            print(mark + num + ln)
    print("\x1b[90m" + "-" * 70 + "\x1b[0m")
    print(msg)


render()
while True:
    ch = key()
    if ch == "UP":
        if row > 0:
            row -= 1
            if col > len(lines[row]):
                col = len(lines[row])
    elif ch == "DOWN":
        if row + 1 < len(lines):
            row += 1
            if col > len(lines[row]):
                col = len(lines[row])
    elif ch == "LEFT":
        if col > 0:
            col -= 1
        elif row > 0:
            row -= 1
            col = len(lines[row])
    elif ch == "RIGHT":
        if col < len(lines[row]):
            col += 1
        elif row + 1 < len(lines):
            row += 1
            col = 0
    elif ch == "\n" or ch == "\r":
        head = lines[row][:col]
        tail = lines[row][col:]
        lines[row] = head
        lines.insert(row + 1, tail)
        row += 1
        col = 0
        mod = 1
    elif ch == "\b" or ch == "\x7f":
        if col > 0:
            lines[row] = lines[row][:col - 1] + lines[row][col:]
            col -= 1
            mod = 1
        elif row > 0:
            up = len(lines[row - 1])
            lines[row - 1] += lines[row]
            del lines[row]
            row -= 1
            col = up
            mod = 1
    elif ch == "DEL":
        if col < len(lines[row]):
            lines[row] = lines[row][:col] + lines[row][col + 1:]
            mod = 1
        elif row + 1 < len(lines):
            lines[row] += lines[row + 1]
            del lines[row + 1]
            mod = 1
    elif ch == "\x13":
        save(lines)
        mod = 0
        msg = "saved " + name
    elif ch == "\x17":
        save(lines)
        mod = 0
        try:
            import os
            os.sync()
        except Exception:
            pass
        msg = "saved + synced " + name
    elif ch == "\x12":
        save(lines)
        mod = 0
        msg = "saved. run with: py " + name
    elif ch == "\x11":
        if mod:
            save(lines)
        print("\x1b[2J\x1b[H", end="")
        print("left", name)
        break
    elif ch and ord(ch) >= 32:
        lines[row] = lines[row][:col] + ch + lines[row][col:]
        col += 1
        mod = 1
    render()

