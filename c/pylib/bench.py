import gc
import math
import time
import minios

gc.collect()
v = minios.video()
s = minios.sysinfo()
cw = minios.mpy_ticks()

mode = "VBE LFB" if v[4] else "VGA 13h"
print("=== MiniOS 2.0 Speed & Space Benchmark ===")
print("Video     : %dx%d (%s @ 0x%08X) Canvas %dx%d" % (v[0], v[1], mode, v[5], v[2], v[3]))
print("Frames    : %d/%d free (%d KB avail) | KHeap %d B" % (
    s[2], s[3], s[2] * 4, s[4]
))
def _ms(ticks):
    return "<10ms" if ticks == 0 else ("%dms" % (ticks * 10))

print("MPY Load  : last=%s (cold=%s, warm=%s)" % (
    _ms(s[6]), _ms(cw[0]), _ms(cw[1])
))

t0 = time.ticks_ms()
acc = 0
for i in range(60000):
    acc = (acc + (i ^ (i >> 3))) & 0x7FFFFFFF
dt_int = max(1, time.ticks_diff(time.ticks_ms(), t0))
print("Int Loop  : 60,000 iters in %d ms (%d Kops/s) chk=%x" % (
    dt_int, 60000 // dt_int, acc & 0xFFFF
))

t0 = time.ticks_ms()
facc = 0.0
for i in range(10000):
    facc += math.sin(i * 0.01) * math.sqrt(i + 1.0)
dt_fp = max(1, time.ticks_diff(time.ticks_ms(), t0))
print("x87 Float : 10,000 sin*sqrt in %d ms (%d Kops/s)" % (
    dt_fp, 10000 // dt_fp
))

gc.collect()
t0 = time.ticks_ms()
objs = None
for i in range(12000):
    objs = (i, i + 1, i + 2, objs if (i & 7) else None)
dt_gc = max(1, time.ticks_diff(time.ticks_ms(), t0))
gc_used = gc.mem_alloc() // 1024
gc_free = gc.mem_free() // 1024
objs = None
gc.collect()
print("GC Alloc  : 12,000 tuples in %d ms (%d K/s) heap=%d/%d KB" % (
    dt_gc, 12000 // dt_gc, gc_used, gc_used + gc_free
))

big_digits = len(str(1 << 1000))
print("BigInt    : 2**1000 digits=%d [%s]" % (
    big_digits, "OK" if big_digits == 302 else "FAIL"
))
