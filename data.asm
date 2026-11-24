; =============================================================================
;  MiniOS data: variables, strings, UI labels, palette, cursor bitmap
; =============================================================================

; --- graphics context --------------------------------------------------------
g_x             dw 0
g_y             dw 0
g_w             dw 0
g_h             dw 0
g_color         db 0
g_str           dw 0
g_scale         dw 1
g_rowcnt        db 0
g_bitcnt        db 0

; --- kernel state ------------------------------------------------------------
boot_drive      db 0
boot_ticks      dd 0
last_tick       dd 0
dirty           db 1
mouse_x         dw 160
mouse_y         dw 100
mouse_prev      db 0
mouse_cycle     db 0
mouse_packet    times 3 db 0
mouse_moved     db 0
cursor_drawn_x  dw 0
cursor_drawn_y  dw 0
kbd_shift       db 0
kbd_ext         db 0
kbd_head        dw 0
kbd_tail        dw 0
kbd_buf         times 64 db 0
base_kb         dw 0
ext_mb          dw 0
cpu_vendor      times 13 db 0
cpu_line        times 24 db 0
ram_short       times 24 db 0
mem_line        times 40 db 0
time_hm         db '00:00', 0
time_hms        db '00:00:00', 0
date_str        db '0000-00-00', 0
uptime_str      db '00:00:00', 0
up_h            db 0
up_m            db 0
up_s            db 0

; --- windows / UI state ------------------------------------------------------
console_x       dw 12
console_y       dw 22
console_visible db 1
system_x        dw 50
system_y        dw 46
system_visible  db 0
about_x         dw 60
about_y         dw 52
about_visible   db 0
menu_open       db 0
menu_i          dw 0
menu_iy         dw 0
menu_hover      db 0
drag_win        db 0
drag_dx         dw 0
drag_dy         dw 0
wf_x            dw 0
wf_y            dw 0
wf_w            dw 0
wf_h            dw 0
wf_title        dw 0
rx              dw 0
ry              dw 0
rw              dw 0
rh              dw 0
sys_ty          dw 0
term_draw_i     db 0

; --- terminal ----------------------------------------------------------------
term_n          dw 1
term_cur        dw 0
term_buf        times 14 * 40 db 0
cmd_buf         times 41 db 0

; --- window titles and labels ------------------------------------------------
title_console   db 'Console - MiniOS', 0
title_system    db 'System Information', 0
title_about     db 'About MiniOS', 0
label_close     db 'x', 0
label_start     db 'Start', 0
label_console   db 'Console', 0
label_system    db 'System', 0
label_brand     db 'MiniOS', 0
label_logo      db 'MiniOS', 0
label_desktop   db 'MiniOS 1.0', 0
label_size      db '1.44 MB GUI OS', 0
label_ok        db 'OK', 0

; --- start menu --------------------------------------------------------------
menu_items      dw menu_system, menu_console, menu_about, menu_reboot
menu_system     db 'System info', 0
menu_console    db 'Console', 0
menu_about      db 'About MiniOS', 0
menu_reboot     db 'Reboot', 0

; --- boot / about ------------------------------------------------------------
banner          db 'MiniOS 1.0  (c) 2026', 10
                db 'Type HELP for commands.', 10, 10
                db 'C:\> ', 0
prompt          db 'C:\> ', 0

splash_msg1     db 'A graphical operating system', 0
splash_msg2     db 'in 1.44 MB', 0
splash_msg3     db '16-bit x86 assembly', 0
splash_msg4     db 'Booting...', 0

about_msg1      db 'A GUI OS in 1.44 MB', 0
about_msg2      db '16-bit x86 assembly', 0
about_msg3      db '(c) 2026 MiniOS Project', 0

; --- system information ------------------------------------------------------
label_sys_title db 'MiniOS 1.0 - System Info', 0
label_cpu_prefix db 'CPU: ', 0
label_ram_prefix db 'RAM: ', 0
label_k_plus    db 'K + ', 0
label_m_suffix  db 'M', 0
label_k_base    db 'K base, ', 0
label_m_ext     db 'M extended', 0
label_video     db 'Video: VGA 320x200x256', 0
label_bootline  db 'Boot: A: 1.44MB floppy', 0
label_mode      db 'Mode: 16-bit real mode', 0
label_uptime_prefix db 'Uptime: ', 0
label_date_prefix   db 'Date: ', 0
label_time      db 'Time: ', 0
label_date      db 'Date: ', 0

; --- shell messages ----------------------------------------------------------
help_msg1       db 'Commands:', 0
help_msg2       db 'help ver time date mem cpu dir', 0
help_msg3       db 'cls sysinfo about reboot', 0
ver_msg1        db 'MiniOS 1.0 - 16-bit real mode GUI', 0
ver_msg2        db 'Booted from a 1.44 MB floppy.', 0
msg_bad         db 'Bad command or file name', 0
msg_sys_open    db 'System window opened.', 0
msg_about_open  db 'About window opened.', 0
msg_reboot      db 'Rebooting...', 0
dir1            db ' Volume in drive A is MINIOS', 0
dir2            db ' Directory of A:\', 0
dir3            db 'KERNEL  BIN     6144  11-23-26', 0
dir4            db 'README  TXT     2048  11-23-26', 0
dir5            db '2 file(s) 8192 bytes, 1440K free', 0
cmd_help        db 'help', 0
cmd_ver         db 'ver', 0
cmd_time        db 'time', 0
cmd_date        db 'date', 0
cmd_mem         db 'mem', 0
cmd_cpu         db 'cpu', 0
cmd_dir         db 'dir', 0
cmd_cls         db 'cls', 0
cmd_sysinfo     db 'sysinfo', 0
cmd_about       db 'about', 0
cmd_reboot      db 'reboot', 0

; --- VGA palette (6-bit RGB), colours 0..15 ----------------------------------
palette:
        db 0, 0, 0              ; 0 black
        db 0, 0, 42             ; 1 dark blue
        db 0, 42, 0             ; 2 dark green
        db 0, 42, 42            ; 3 teal desktop
        db 42, 0, 0             ; 4 dark red
        db 42, 0, 42            ; 5 purple
        db 42, 42, 0            ; 6 olive
        db 42, 42, 42           ; 7 light grey
        db 21, 21, 21           ; 8 grey
        db 0, 0, 63             ; 9 blue
        db 0, 63, 0             ; 10 green
        db 0, 63, 63            ; 11 cyan
        db 63, 0, 0             ; 12 red
        db 63, 0, 63            ; 13 magenta
        db 63, 63, 0            ; 14 yellow
        db 63, 63, 63           ; 15 white

; --- US keyboard scancode maps ------------------------------------------------
%include "keymap.inc"

; --- 12x18 arrow cursor ------------------------------------------------------
;     X = black outline, W = white fill, . = transparent
cursor_art:
        db 'X...........'
        db 'XX..........'
        db 'XWX.........'
        db 'XWWX........'
        db 'XWWWX.......'
        db 'XWWWWX......'
        db 'XWWWWWX.....'
        db 'XWWWWWWX....'
        db 'XWWWWWWWX...'
        db 'XWWWWWWWWX..'
        db 'XWWWWWWWWWX.'
        db 'XWWWWWXXXXXX'
        db 'XWWWXWX.....'
        db 'XWWX.XWX....'
        db 'XWX..XWX....'
        db 'XX....XWX...'
        db 'X.....XWX...'
        db '......XXX...'




