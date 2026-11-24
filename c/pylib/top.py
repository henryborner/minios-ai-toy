import minios

s = minios.sysinfo()
print("MiniOS top")
print("uptime_s:", s[1])
print("free_frames:", s[2], "/", s[3])
print("tasks:", s[5])
print("last_mpy_ticks:", s[6])
