import sys

args = sys.argv[1:]
if args and args[0] == "calc":
    args = args[1:]
expr = " ".join(args).strip()
if not expr:
    print("usage: calc <expression>")
else:
    try:
        print(eval(expr))
    except Exception as e:
        print("calc error:", e)
