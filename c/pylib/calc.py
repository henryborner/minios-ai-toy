import sys

expr = " ".join(sys.argv[1:]).strip()
if not expr:
    print("usage: calc <expression>")
else:
    try:
        print(eval(expr))
    except Exception as e:
        print("calc error:", e)
