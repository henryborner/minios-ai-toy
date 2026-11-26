# E820 memory-pool verification

Boot-sector E820 entries are consumed by `frame_init()`.  Frames are reserved
by one bitmap covering 4 KiB pages up to 512 MiB; the direct map covers
4..512 MiB at virtual 16 MiB.  E820 reserved holes stay marked used.

Measured with `metrics` on COM1 (`free/total` frames, 4 KiB each):

```text
-m 64    free=14188  total=14304
-m 128   free=30572  total=30688
-m 256   free=63340  total=63456
-m 512   free=128876 total=128992
```

The counted total is the usable E820 pages above the 8 MiB frame-pool start,
not the raw RAM size.  Machines without E820 fall back to the legacy
8..16 MiB window.

Verification after the full 512 MiB direct-map fix:

```text
calc 2**10
selftest        -> [selftest] PASS
smoke_test.py   -> SMOKE PASS (640x480 + forced 320x200 + REGRESS)
```
