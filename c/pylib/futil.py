import os

print("files:")
for name in os.listdir():
    try:
        st = os.stat(name)
        print(" ", name, st[6])
    except Exception:
        print(" ", name, "?")
