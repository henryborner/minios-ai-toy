import sys

name = sys.argv[1] if len(sys.argv) > 1 else "NOTES.TXT"
print("ED:", name)
print("Enter lines; an empty line saves and exits.")
f = open(name, "w")
while True:
    line = input("> ")
    if not line:
        break
    f.write(line + "\n")
f.close()
print("saved", name)
