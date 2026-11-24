import minios

v = minios.video()
s = minios.sysinfo()
print("MiniOS top (%dx%d %s)" % (v[0], v[1], "VBE LFB" if v[4] else "VGA"))
print("uptime_s:", s[1], "ticks:", s[0])
print("free_frames:", s[2], "/", s[3], "(%d KB free)" % (s[2] * 4))
print("kheap_used:", s[4], "/ 65536 B")
print("tasks:", s[5], "last_mpy_ticks:", s[6])
