import os
import sys
import minios

argv = sys.argv[1:]
if argv and argv[0] == "futil":
    argv = argv[1:]

if not argv:
    argv = ["help"]


def _read(name):
    f = open(name, "rb")
    data = f.read()
    f.close()
    return data


def _text(name):
    try:
        return _read(name).decode()
    except Exception:
        return _read(name).decode("latin-1")


def _kind(name):
    try:
        return os.stat(name)[2]
    except Exception:
        return 0


def _size(name):
    try:
        return os.stat(name)[6]
    except Exception:
        return 0


def _tag(kind):
    if kind == 2:
        return "\x1b[32m[RAM]\x1b[0m"
    if kind == 1:
        return "\x1b[36m[DSK]\x1b[0m"
    return "[???]"


def cmd_ls():
    total = 0
    print("KIND NAME             SIZE")
    for name in os.listdir():
        k = _kind(name)
        s = _size(name)
        total += s
        print("%s %-16s %6d" % (_tag(k), name, s))
    print("TOTAL %d bytes" % total)


def cmd_cat(args):
    numbered = False
    if args and args[0] == "-n":
        numbered = True
        args = args[1:]
    if not args:
        print("usage: cat [-n] FILE")
        return
    data = _text(args[0])
    if numbered:
        ln = 1
        for line in data.splitlines():
            print("%4d  %s" % (ln, line))
            ln += 1
    else:
        print(data)


def cmd_cp(args):
    if len(args) < 2:
        print("usage: cp SRC DST")
        return
    data = _read(args[0])
    f = open(args[1], "wb")
    f.write(data)
    f.close()
    print("copied %d bytes -> %s" % (len(data), args[1]))


def cmd_rm(args):
    if not args:
        print("usage: rm FILE")
        return
    os.remove(args[0])
    print("removed", args[0])


def cmd_grep(args):
    if not args:
        print("usage: grep PATTERN [FILE]")
        return
    pat = args[0]
    if len(args) == 1:
        data = b""
        while True:
            chunk = minios.pipe_read(3, 512)
            if not chunk:
                break
            data += chunk
        hit = 0
        for line in data.decode().splitlines():
            if pat in line:
                hi = line.replace(pat, "\x1b[31m" + pat + "\x1b[0m")
                print(hi)
                hit += 1
        print("matches:", hit)
        return
    names = args[1:]
    hit = 0
    for name in names:
        try:
            data = _text(name)
        except Exception:
            continue
        ln = 1
        for line in data.splitlines():
            if pat in line:
                hi = line.replace(pat, "\x1b[31m" + pat + "\x1b[0m")
                print("%s:%d: %s" % (name, ln, hi))
                hit += 1
            ln += 1
    print("matches:", hit)


def cmd_df():
    si = minios.sysinfo()
    free = si[2] * 4096
    total = si[3] * 4096
    used = total - free
    print("RAMFS/frame pool: %d/%d bytes used" % (used, total))
    print("SaveFS: 260 sectors (133120 bytes) reserved")
    print("free frames:", si[2], "/", si[3])


def cmd_touch(args):
    if not args:
        print("usage: touch FILE")
        return
    f = open(args[0], "wb")
    f.close()


def cmd_write(args):
    if len(args) < 2:
        print("usage: write FILE TEXT...")
        return
    f = open(args[0], "w")
    f.write(" ".join(args[1:]))
    f.close()


def cmd_help():
    print("futil ls | cat [-n] F | cp A B | rm F | grep P [F]")
    print("      df | touch F | write F TEXT | sync")


cmd = argv[0]
rest = argv[1:]
try:
    if cmd == "ls":
        cmd_ls()
    elif cmd == "cat":
        cmd_cat(rest)
    elif cmd == "cp":
        cmd_cp(rest)
    elif cmd == "rm":
        cmd_rm(rest)
    elif cmd == "grep":
        cmd_grep(rest)
    elif cmd == "df":
        cmd_df()
    elif cmd == "touch":
        cmd_touch(rest)
    elif cmd == "write":
        cmd_write(rest)
    elif cmd == "sync":
        os.sync()
        print("synced")
    else:
        cmd_help()
except Exception as e:
    print("futil error:", e)
