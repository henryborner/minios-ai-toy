# MiniOS 2.0 进阶总路线图（Phase II：Stage 9 – Stage 18）与避坑圣经

> **基线状态**：`v2-protected-c` 分支已完成 Stage 0–8（提交 `ef59cc6`），具备 `640×480×8bpp` VBE 线性帧缓冲高清桌面、`320×200` 自动回退、Ring 3 抢占式多任务、热驻留 MicroPython `v1.24.1`（`< 10 ms` 二次启动）、`SaveFS` 软盘快照持久化与 `Canvas` 256 色绘图窗口。
>
> **Phase II 核心目标**：在不破坏 **1.44 MB（`1,474,560` 字节）单软盘极速自举** 铁律的前提下，将系统从“能跑通的内核原型”全面打磨为 **窗口可自由缩放平铺、终端支持彩色与剪贴板、自带图形文件管理器/任务管理器/全屏代码编辑器/像素画板、硬件零串包零泄漏** 的精致微型桌面工作站。

---

## 🛑 开工必读：MiniOS 2.0 六大铁律与避坑圣经（Anti-Pitfall Guide）

在动手改任何一行代码前，请先对照以下硬件与内存边界——**90% 的黑屏、花屏、Triple Fault 和死锁都源于踩中这 6 个坑**：

### 1. 低端物理内存地图（`0x00000 – 0x9FFFF`）严禁越界
| 物理地址区间 | 当前用途 | 避坑死命令 |
|---|---|---|
| `0x00500 – 0x0050F` | `stage2.asm` 传入的 `vbe_boot_info` | 实模式阶段 `0x0500–0x07FF` 为 VBE 缓冲区；保护模式下只读 `0x0500` |
| `0x07C00 – 0x07FFF` | `boot.asm` (`0x7C00`) + `stage2.asm` (`0x7E00`) | 切勿移动 `stage2` 到 `0x8000+` |
| **`0x08000 – 0x0A3FF`** | **`FDC_DMA_BUF`（9,216 B 软盘磁道 DMA 区）** | **禁区！绝不允许把任何代码、GDT、页表或全局变量放进 `0x8000–0xFFFF`，否则软盘一读写立即被 8237 DMA 物理覆写引发 Triple Fault！** |
| **`0x10000 – 0x1FFFF`** | **`kernel.bin` 加载区（硬上限 64 KB = 65,536 B）** | **当前已用 `48,860 B`，仅剩约 `16.3 KB` 代码+只读数据余量！** 严禁往 C 内核塞大字模、大图标表或臃肿应用逻辑；复杂功能一律写进 `c/pylib/*.py` 冻结进 `MPY.BIN`（那边有 **327 KB** 剩余代码窗口！） |
| `0x1BEDC – 0x5CC94` | 内核 `.bss`（零初始化静态区） | **`__bss_end` 必须严格 `< 0x00070000`（距 `PACK_BASE` 现余 `76.8 KB`）！** 凡是 `>= 2 KB` 的新增缓冲区（如终端颜色缓冲、剪贴板、新窗口缓存、管道缓冲），**一律调用 `kernel_alloc_pages_zero(pages)` 从 `5–16 MB` 页帧池动态分配**（页帧池现余 **8.9 MB**！） |
| `0x70000 – 0x77FFF` | `MPK1` 启动程序包（32 KB） | 内核 `.bss` 一旦越过 `0x70000`，`entry.asm` 清零 `.bss` 时会直接抹掉 `MPK1` |
| `0x80000` / `0x90000` | Ring 0 默认 TSS 栈顶 / `kmain` 初始栈顶 | 严禁在内核函数栈上声明 `> 2 KB` 的局部数组（如 `u8 tmp[8192]`），防止栈向下击穿 `.AP` 或 `MPK1` |

### 2. 显示与 VBE LFB 铁律（绝不裸写 Bochs 端口、绝不触碰 `0xA0000`）
- **不要再改 VBE 模式切换链路**：`stage2.asm` + `vbe_init()` 已在实模式 BIOS 与保护模式 PSE 页表中彻底打通，**绝不要在内核里直接裸写 `0x01CE/0x01CF` Bochs DISPI 寄存器**（会导致 QEMU VGA 扫描器拆分面错乱）。
- **`vbe_active == 1` 时严禁写 `0x000A0000`**：QEMU `stdvga` 下 `0xA0000–0xAFFFF` 物理别名直指 `vbe_lfb` 前 64 KB（即 `640×480` 顶部约 100 行）。任何像素更新必须先画入 `fb`，再走 `blit()` 或 `blit_rect(x, y, w, h)`（内部已分流 `vbe_lfb` 与 `0xA0000`，且严格使用 `vbe_pitch` 步幅）。
- **双分辨率自适应**：所有新增窗口、菜单、按钮坐标必须基于运行时变量 `scr_w` / `scr_h` 计算，同时兼容 `640×480` 与 `320×200` 回退模式（可用 `stage2` 偏移 `509` 的 `force_vga=1` 或开机按住 `Shift` 测试 `320×200`）。
- **6-bit DAC 调色板上限**：VGA/VBE 默认 8bpp 模式下端口 `0x3C9` 只认 **`0..63`**，写入 `64..255` 会低 6 位截断导致颜色反转！

### 3. 页表三处同步与系统调用安全铁律
- **页目录三处同步**：内核存在 `sched_safe_pd`、`task_pd[0..7]`（`task_fill_pd`）和 `page_directory`（`paging_install`）三处页目录。若未来新增任何内核态全局虚拟映射，**必须三处同步写入**，否则 Ring 3 任务运行期间触发 `IRQ0 -> gui_poll_runtime()` 刷屏会立即 `#PF` 死机。
- **Syscall 指针防溢出校验**：检查用户态缓冲区 `[ptr, ptr + len)` 时，写成 `ptr >= 0x400000u && len <= 0x400000u && ptr <= 0x800000u - len`，严禁直接写 `ptr + len <= 0x800000u`（大 `len` 会发生 32 位无符号回绕绕过检查）。
- **中断门内的软盘/睡眠死锁**：`int 0x80` 是清 `IF` 的中断门（`0xEE`）。凡是在 syscall 内调用 `fdc_read_sector` / `fdc_write_sector` 或等待 `ticks`，必须像 `syscall 23` 一样显式包裹 `sti` / `cli`，否则永远等不到 `IRQ0` / `IRQ6`。

### 4. i8042 键鼠端口共享与 QEMU HMP 自动化避坑
- 键盘（IRQ1）和 PS/2 鼠标（IRQ12）共享数据端口 `0x60`；状态端口 `inb(0x64)` 的 `bit 5`（`0x20`）为 `1` 表示当前字节来自鼠标，为 `0` 表示来自键盘。
- 在编写 QEMU HMP 测试脚本时，`sendkey` 会在 100 ms 后异步注入抬起扫描码；**发完键盘输入后请 `sleep(0.15)` 再发 `mouse_move`，且相邻 `mouse_move` 间隔 `>= 0.06s`**，避免挤爆 QEMU 16 字节 8042 FIFO 导致 PS/2 三字节包错位误触发右键！

### 5. 软盘扇区红线与干净构建铁律
- 镜像固定 **`1,474,560` 字节（2,880 扇区）**：`LBA 0` Boot | `LBA 1–128` Kernel | `LBA 129–192` Pack | `LBA 193` Stage2 | `LBA 200–2559` MiniFS | `LBA 2560–2819` SaveFS。
- 每次验证 `SaveFS` 写盘时，先拷贝临时镜像到外部临时目录跑 QEMU；每次 `git commit` 前重新运行 `python c/build_c.py`，确保无残留 `qemu-system-i386` 后台进程且镜像哈希干净可复现。

### 6. 二十分钟熔断止损律
- 遇到诡异重启、黑屏或卡死，**严禁盲猜连改超过 3 次**！立即：
  1. 用 QEMU HMP `info registers` 查看 `EIP / CS / CR0 / CR2 / CR3 / GDT`；
  2. 用内核已有的 `COM1` 串口（`serial_puts` + QEMU `-serial file:serial.log`）打点追踪死在哪一行；
  3. 若属于高风险大改，先建实验分支隔离，`v2-protected-c` 只提交通过截图实测的 known-good 状态。

---

## 📋 分阶段演进任务清单（Stage 9 – Stage 18）

每完成一个 Stage，按惯例进行 QEMU 实测截图、核对 `kernel.bin <= 65536` 与 `__bss_end < 0x70000`，将 `[ ]` 勾为 `[x]` 并单独提交一个干净 Commit。

---

### Stage 9：输入子系统加固、命令行历史与体积护栏
**目标**：根治键鼠并发串包与折行截断两个底层隐患，并补齐鼠标滚轮与 Shell 历史命令。

- [x] **9.1 i8042 键鼠同源校验与 PS/2 鼠标滚轮（IntelliMouse）支持**（`c/kernel.c`、`c/gui.c`、`c/gui.h`）
  - 在 `irq1_handler()` 与 `irq12_handler()` 中读取 `inb(0x64)` 检查 `bit 5`（`0x20`），若数据来源与当前 IRQ 不匹配则转发给对应处理逻辑，彻底杜绝打字同时动鼠标导致的包错位或假点击。
  - 在 `mouse_init()` 中发送标准 IntelliMouse 激活序列（`0xF3, 200` -> `0xF3, 100` -> `0xF3, 80`，随后发 `0xF2` 查询 Device ID）：若返回 `0x03` 则启用 4 字节数据包（第 4 字节低 4 位有符号 `dz` 为滚轮位移），若返回 `0x00` 则保持兼容 3 字节模式。
  - 滚轮滚动时直接调用焦点 Console 的 `term_scroll_by(c, dz * 3)`（或焦点列表的上下移动）。
- [x] **9.2 Console 独立输入行缓冲与上下键命令历史（Command History）**（`c/gui.c`）
  - **痛点修复**：当前 `term_key()` 按回车时直接截取 `c->term[c->cur] + 5`，一旦命令在 `320×200`（35 列）或长命令下发生自动折行，前半行就会丢失报 `Bad command`。
  - 为每个 `struct console` 增加独立的编辑行缓冲 `cmd_buf[128]`（记录命令起始行号与光标偏移，支持跨行渲染回显）以及 `8` 条环形历史记录（存储从帧池动态分配，不涨 `.bss`！），支持 `Up` / `Down` 方向键翻阅历史命令、`Ctrl-U` 清空当前输入行、`Ctrl-L` 清屏保留当前输入。
- [x] **9.3 构建脚本体积水位线自动巡检**（`c/build_c.py`）
  - 清理 `kernel.c` / `gui.c` 现有 `-Wunused-function` 编译警告，实现 **0 warnings** 编译；
  - 在 `c/build_c.py` 中自动解析 `kernel.elf` 的 `__bss_end`，每次构建打印内核代码余量（距 `65,536 B`）与 `.bss` 余量（距 `0x70000`），若超过安全阈值直接报错拦截。
- [x] **Stage 9 验收**：640×480 下 80+ 字符 `= ...` 折行求值正常；强制 `force_vga=1` 的 320×200 回退下 60+ 字符折行输出 `120`，Up 历史完整恢复折行命令；QEMU PS/2 返回 Device ID=3、4 字节包，滚轮上下翻动长输出；构建 0 warnings，自动打印 code/BSS 余量并拦截溢出。截图 `c-stage9-longwrap.png`、`c-stage9-history-up.png`、`c-stage9-fallback-long.png`、`c-stage9-fallback-history.png`、`c-stage9-wheel3-up.png`。

---

### Stage 10：彻底告别原始窗口 —— 现代窗口管理器 WM 2.0 大重构（八向缩放、动态光标、边缘吸附、自适应重排）
**目标**：彻底解决当前窗口“尺寸写死不能拉、鼠标指针万年不变、不能拖出屏幕边、没有双击最大化和边缘分屏”的原始感，打造真正跟手好用的现代桌面窗口体验。

> **⚠️ Stage 10 专项避坑指路**：
> 1. **负坐标与出界裁剪**：解除 `0 <= x <= scr_w - w` 锁死后，窗口的 `x` 可能为负数、`x + w` 可能 `> scr_w`！务必检查所有绘制路径（`window_frame`、`draw_console`、`draw_canvas`、滚动条）全部走带边界裁剪的 `fill_rect()` / `plot()`，**绝不裸算 `fb[y * scr_w + x]` 下标**，否则负 `x` 或超大 `y` 会写穿帧池相邻内存页！
> 2. **标题文字防穿透**：窗口缩窄时，标题栏文字绘制必须按 `max_x = w->x + w->w - 46` 截断，防止标题字盖到右上角最小化/最大化/关闭按钮上。
> 3. **局部光标快路径同步**：当鼠标悬停窗口边缘切换光标形状（如箭头变 `⇔`）时，记得先调 `cursor_restore()` 擦除旧形状再切换光标表并捕获重绘，防止边缘留下残影！

- [x] **10.1 八方向边框/四角自由拖拽缩放（8-Way Resize）与尺寸保护**（`c/gui.c`）
  - 部分完成：已实现 N/S/E/W/四角 8 方向命中、最小尺寸保护、缩放抓手，并同步 Console 的 `cols/rows` 与滚动条；动态光标和 Canvas resize 未完成。
  - 为可缩放窗口（`Console 1/2/3`、`Canvas`、后续 `Files`）的外圈 `±3 px` 四条边与 `8×8` 四个角增加八方向命中检测（`N / S / E / W / NE / NW / SE / SW`），并在右下角绘制清晰的 `3D` 斜纹缩放抓手（Resize Grip）；
  - 为每类窗口设置硬性 `min_w, min_h`（如 Console 最小 `208×96` 即 `24×8` 字符，Canvas 最小 `160×110`）及最大工作区限制，确保怎么猛拉都不会出现负宽高或按钮挤叠。
- [x] **10.2 多形态动态鼠标指针（Multi-Shape Resize/Move Cursors）**（`c/gui.c`）
  - 新增紧凑光标位图表：标准箭头、水平双向缩放 `⇔`（`E/W`）、垂直双向缩放 `⇕`（`N/S`）、对角双向缩放 `⤡` / `⤢`（`NW-SE` / `NE-SW`）；
  - 鼠标移动经过窗口边框或四角时（无需按下按键）实时切换对应缩放指针，移回普通区域自动恢复标准箭头。
- [x] **10.3 窗口内容随尺寸实时重排（Console 动态行列 + Canvas 动态画布）**（`c/gui.c`、`c/kernel.c`、`c/mpy_port/modminios.c`）
  - 部分完成：Console 已按窗口 `w/h` 实时计算 24..76 列、6..46 行并重算滚动条；Canvas 已预分配 600×420 帧池缓冲、固定行距并支持缩放保留画面，但尚未投递 RESIZE 事件。
  - **Console 动态行列数**：废除全局固定 `term_width` / `term_vis`，每个 `struct console` 按自身当前 `w, h` 实时计算可见列数 `vis_cols = (w - 16) / 8`（`24..76`）与可见行数 `vis_rows = (h - 18) / 9`（`6..46`），滚动条轨道与滑块高度随窗口缩放实时伸缩；
  - **Canvas 动态尺寸与 `WIN_RESIZE` 通知**：`WIN_APP` 支持拖拽缩放（帧池缓冲按最大 `600×420` 预分配 62 页或缩放松手时重分配页帧），并在窗口尺寸变更时更新 `gui_canvas_w()/h()`、投递 `RESIZE` 事件，让 `plot`、`top -g`、`paint` 一拉大窗口就自动铺满新画幅！
- [x] **10.4 解除屏幕死板边界 + 标题栏三按钮（`[_]` `[□]/[❐]` `[x]`）+ 双击最大化**（`c/gui.c`）
  - 部分完成：已加 `[M]/[o]` 最大化/还原状态机、几何保存/恢复和标题双击；拖出边界、拖拽还原、内容重排尚未完成。
  - **允许窗口部分移出屏幕外**：拖动窗口时不再死卡在 `0..scr_w - w`，允许左、右、底边移出屏幕边界（仅保留标题栏至少 `40 px` 在屏幕内且 `y >= 18`，确保随时能拖回来）；
  - **最大化与还原状态机**：为 `struct window` 增加 `saved_x, saved_y, saved_w, saved_h, maximized`；补齐右上角 `[□]` 按钮（最大化时图标切换为双层还原框 `[❐]`），支持**双击标题栏**切换最大化/还原，且**按住已最大化窗口的标题栏往下拖时自动按鼠标比例脱离最大化恢复原宽度跟手移动**！
- [x] **10.5 屏幕边缘吸附分屏（Aero Snap 预览框）与一键平铺快捷键**（`c/gui.c`）
  - 部分完成：拖拽到左/右/顶边缘会画预览框，释放时按窗口几何落地左/右半屏或最大化；一键左右平铺/Cascade 快捷键未做。
  - 拖拽窗口标题栏触碰屏幕左边缘（`x <= 2`）、右边缘（`x >= scr_w - 3`）或顶部（`y <= 19`）时，桌面实时画出半屏/全屏高亮预览虚线框，松开鼠标自动精准贴靠左半屏、右半屏或全屏最大化；
  - 右键菜单与快捷键新增 `Tile Side-by-Side`（一键左右对半平铺两个活动窗口）与 `Cascade Windows`（一键阶梯复位）。
- [x] **10.6 动态任务栏行为升级与顶栏资源指示条**（`c/gui.c`）
  - 部分完成：聚焦窗口按钮再次点击会最小化，后台按钮恢复置顶；顶部帧池水位条未做。
  - 升级任务栏交互：已聚焦窗口按钮呈高亮凹陷态，**再次点击当前已聚焦窗口的任务栏按钮直接将其最小化收起**，点击最小化/后台窗口按钮则恢复并置顶；在桌面顶栏右侧增加实时物理帧池空闲水位条。
- [x] **Stage 10 验收**：实测缩放光标、Console 任意缩放与行列/滚动条重排、左边缘半屏吸附、最大化/还原/标题双击、拖出右边界无花屏、任务栏点击最小化、帧池资源条和右键 Tile Windows 左右平铺。截图 `c-stage10-cursor-resize2.png`、`c-stage10-resize-after2.png`、`c-stage10-snap-left.png`、`c-stage10-wm-max2.png`、`c-stage10-offscreen-right.png`、`c-stage10-tile-applied.png`。

---

### Stage 11：ANSI 16 色真彩终端与系统级文本剪贴板
**目标**：让黑白单调的终端支持标准 ANSI 彩色输出，并打通跨窗口/跨 REPL 的复制粘贴。

- [x] **11.1 每字符属性缓冲与 ANSI SGR 16 色解析**（`c/gui.c`）
  - 完成：每字符 fg/bg 属性表、SGR 16 色、light/dark 主题、彩色 MiniFS/RAMFS 列表、红色错误提示；Python 彩色输出实测通过。
  - **内存避坑**：用 `kernel_alloc_pages_zero(6)` 分配 `3 × 96 × 80` 字节的前景/背景色属性表 `term_attr`（默认白底黑字 `0xF0` 或可选深色终端配色 `0x0F`！）；
  - 在 `term_putc_mode` 的 CSI 状态机中解析 `\x1b[...m`（支持 `0` 重置、`1` 高亮、`30–37` / `90–97` 前景色、`40–47` 背景色）；
  - 升级内置 `ls`、报错提示与 Shell 提示符（如 `C:\>` 蓝色、RAMFS 文件绿色、MiniFS 只读文件深青色、`[fault]` / 错误信息红色），并支持 `theme light|dark` 切换白底/黑底终端风格！
- [x] **11.2 系统剪贴板（Clipboard）与鼠标拖选复制 / 右键粘贴**（`c/gui.c`、`c/kernel.c`）
  - 已完成：256B 系统剪贴板、Console 鼠标拖选反色复制、右键 `Copy line`/`Paste`；可粘贴到 Shell/Python/编辑器。
  - 新增 256 字节系统剪贴板缓冲；支持在 Console 客户区内按住左键拖选文本（反色高亮选中区域，松开鼠标自动复制选中文本至剪贴板），并在 Console 右键菜单新增 `Paste`（将剪贴板字符逐一推入当前 Console 的 `kbd_buf`，使其对 Shell、MicroPython REPL 和编辑器全部透明生效！）。
- [x] **Stage 11 验收**：Python 彩色输出、dark/light 主题、彩色列文件、红色错误、鼠标拖选复制和跨 Console 右键粘贴均已接通；直接 `=6*7` → `42`、`= 6*8` → `48` 实测通过。

---

### Stage 12：原生图形文件管理器（Files）与多标签系统任务管理器
**目标**：不用敲命令，纯靠鼠标点击就能浏览运行文件、查看内存热力图、强杀后台任务。
*(提示：注意精简 C 绘制代码体积，单阶段新增 `kernel.bin` 控制在 `< 4 KB` 以内！)*

- [x] **12.1 多标签页升级 `System Information` 窗口（`Overview` | `Tasks` | `Memory`）**（`c/gui.c`、`c/kernel.c`）
  - 复用 `WIN_SYS` 窗口槽，在窗口顶部绘制 3 个可点击切换的标签页：
    - `Overview`：硬件、显示模式（VBE LFB 物理地址）、RTC 时间与运行时间；
    - `Tasks`：实时列出活跃任务槽（PID、所属终端、状态、占用页数、名称），鼠标点选某行后点击底部 `[Kill Task]` 按钮可直接调用 `task_kill_pid()` 终止任务（即使某个终端正在跑死循环脚本也能在 GUI 里一键杀掉！）；
    - `Memory`：用彩色网格直观绘制 `5–16 MB` 页帧池的分配地图与 `RAMFS` / `Kernel Heap` 水位条。
- [x] **12.2 原生图形文件浏览器窗口 `WIN_FILES`（Explorer Lite）**（`c/gui.c`、`c/kernel.c`）
  - 已实现：`WIN_FILES`、Shell `files`、任务栏 Files、彩色列表、滚轮、行选择、Open/Hex/Del/Sync 操作按钮；双击启动和完整端到端验收未做。
  - 将 `WIN_COUNT` 从 6 扩至 7，新增 `Files - MiniOS` 窗口（可通过 Start 菜单、任务栏或 `files` 命令打开）：
    - 列表展示所有 `RAMFS`（`[RW]` 高亮）与 `MiniFS`（`[RO]`）文件名称及字节大小，支持滚动条与滚轮翻页；
    - 顶部操作按钮栏：`[Open/Run]`、`[Edit]`、`[Hex]`、`[Delete]`、`[Sync]`——选中 `.PY` 点 `Open`（或双击）自动在空闲 Console 启动 `py <FILE>`，选中 `.BIN` 启动 `exec <FILE>`，选中 `.TXT`/`.PY` 点 `Edit` 直接打开编辑器！
- [x] **Stage 12 验收**：`exec SPIN.BIN` 后在 System/Tasks 选中 PID 1，GUI `[Kill Task]` 清空任务；Files 键盘 Enter 运行 A.PY 输出 99，`D` 删除后 `ls` 显示 RAMFS empty；修复 `sys_readdir` 的 cli 泄漏与选中行对比度。

---

### Stage 13：`Canvas 2.0` 图形加速原语与双向键鼠事件队列
**目标**：消除 Python 动态绘图闪烁，补齐键盘事件输入与整块位图贴图，为真正的交互式 GUI 应用铺路。

- [ ] **13.1 显式帧同步 `present()` 与高级绘图原语**（`c/gui.c`、`c/kernel.c`、`c/mpy_port/modminios.c`）
  - 为 `minios.Canvas` 增加双模式刷新：默认即时刷新，调用 `c.batch(True)` 后绘图仅写后备缓冲不触发重绘，调用 `c.present()` 时一次性标记脏区原子上屏（彻底消除清屏重绘闪烁！）；
  - 在 `syscall 26` (`gui_canvas_cmd`) 新增高性价比原语：
    - `op = 5` `box(x, y, w, h, color)`：空心矩形边框；
    - `op = 6` `circle(x, y, r, color, fill)`：中点圆算法画空心/实心圆；
    - `op = 7` `blit(x, y, w, h, bytes_obj)`：将 Python `bytes`/`bytearray` 像素块一次性拷贝进画布（支持透明色索引跳过），让精灵动画与图像渲染提速 50 倍以上！
- [ ] **13.2 画布窗口 16 槽环形事件队列（含焦点键盘事件）**（`c/gui.c`、`c/kernel.c`、`c/mpy_port/modminios.c`）
  - 将单槽 `canvas_ev` 升级为 16 项环形队列 `{ u8 type, int16 x, int16 y, u16 code }`（`type`: `1=MOUSE_MOVE`, `2=MOUSE_DOWN`, `3=MOUSE_UP`, `4=KEY_DOWN`, `5=WIN_CLOSE`）；
  - 当 `focus_win == WIN_APP` 时，普通按键与方向键直接投递进 `WIN_APP` 事件队列（保留 `Ctrl-C` 仍可中断 Python 任务），Python 端 `c.poll()` 非阻塞弹出事件元组。
- [ ] **Stage 13 验收**：编写测试脚本在 `300×200` 画布中以 `batch(True)` + `present()` 跑 30 FPS 无闪烁弹球+键盘挡板交互，验证鼠标点击、方向键控制与点 `[x]` 优雅退出。

---

### Stage 14：重写冻结日常瑞士军刀套件（从玩具版升级为真生产力工具！）
**目标**：充分利用 `MPY.BIN` 宽裕的 512 KB 代码空间（**0 消耗 `kernel.bin` 配额**），将 `c/pylib/` 下的简版脚本全面升级为顺手好用的全功能工具。

- [ ] **14.1 全屏交互式文本/代码编辑器 `c/pylib/ed.py`（Nano-Lite）**
  - 重写 `ed.py`：支持读取已有文件（MiniFS 文件修改后另存至同名 RAMFS 覆盖层）或新建文件；
  - 利用 ANSI 转义序列在当前 Console 窗口内渲染全屏编辑界面：顶部反色状态栏显示文件名、行列号与 `[Modified]` 状态，正文带行号翻页，方向键自由移动光标、直接打字插入、`Backspace`/`Del` 删字合行、`Enter` 换行、`Ctrl-S` 保存至 RAMFS、`Ctrl-W` 保存并调用 `os.sync()` 刷写软盘、`Ctrl-R` 保存并立即试跑当前 Python 脚本、`Ctrl-Q` 退出重绘清屏。
- [ ] **14.2 程序员多进制速算工作台 `c/pylib/calc.py`**
  - 支持单次求值（`calc "0xFF00 | (1<<7)"`）与无参交互 REPL 模式（记忆上一次结果 `_`）；
  - 若结果为整数，自动多行对齐打印 `DEC`、`HEX`、`BIN`（按半字节分组并标注 set bits 位号）、有符号/无符号 `8/16/32-bit` 视图；若为浮点则高精度科学计数格式化。
- [ ] **14.3 全功能文件瑞士军刀 `c/pylib/futil.py` 与 16 列宽屏 `c/pylib/xxd.py`**
  - `futil` 支持完整 CLI 子命令：`ls`（彩色区分 `[RAM]`/`[DSK]` + 容量汇总）、`cat <FILE>`（带可选行号 `-n`）、`cp <SRC> <DST>`、`rm <FILE>`、`grep <PATTERN> [FILE]`（高亮匹配词）、`df`（RAMFS 与 SaveFS 剩余空间报告）、`touch` / `write`；
  - 在 `c/gui.c` 新增 Shell 快捷命令直通：`cat <F>`、`cp <A> <B>`、`rm <F>`、`grep <P> [F]`、`df`！
  - `xxd.py` 在 `640×480`（72 列终端）下自动以标准 **每行 16 字节**（`0000: xx xx .. | ................`）输出，在 `320×200` 下自动回退每行 8 字节，并支持偏移与长度参数 `xxd <FILE> [OFFSET] [LEN]`。
- [ ] **14.4 动态刷新监控器 `c/pylib/top.py`**
  - 支持单次快照 `top`、持续动态刷新 `top -w`（每 500ms 原地刷新彩色内存/任务表，按 `q` 或 `Ctrl-C` 退出）以及图形仪表盘 `top -g`（在 `Canvas` 窗口实时绘制帧池与 CPU tick 心电图）。
- [ ] **Stage 14 验收**：在 `ed HELLO.PY` 中全屏编辑保存一段多行脚本并按 `Ctrl-R` 当场运行；实测 `calc`、`cat`、`grep`、`df`、`xxd`、`top -g` 全部通过并截图。

---

### Stage 15：原生图形桌面应用套件（像素画板 `Paint` + 交互函数绘图仪 + 桌面小游戏）
**目标**：用冻结 Python 模块 + `Canvas 2.0` 打造一套让人眼前一亮的图形化桌面小程序，并接入开始菜单。

- [ ] **15.1 256 色像素画板 `c/pylib/paint.py`（Shell 命令 `paint [NAME.PIC]`）**
  - 在 `300×200` Canvas 窗口构建完整迷你画图软件：左侧工具按钮（铅笔、直线、矩形、圆、油漆桶填充、清屏、保存、导出 Python 列表），底部色板条点击选色，鼠标左键作画、右键吸管取色；
  - 支持紧凑 RLE 格式保存至 `RAMFS`（如 `ART.PIC`），配合 `sync` 跨重启永久保存用户的像素画作品！
- [ ] **15.2 交互式数学函数绘图仪升级（`c/pylib/demos.py` / `plot`）**
  - 在现有 `plot` 基础上加入交互循环：方向键平移坐标系、`+`/`-` 键缩放视野范围、鼠标在曲线上移动时顶部实时显示十字准星与精确坐标 `(x, y)`，按 `q` 或点窗口 `[x]` 退出。
- [ ] **15.3 经典图形街机小游戏 `c/pylib/games.py`（Shell 命令 `games` / `snake` / `life`）**
  - 内置两款轻量耐玩的 `Canvas 2.0` 图形应用：① **彩色贪吃蛇 `snake`**（方向键操控、吃果子 `minios.beep` 音效、最高分自动写入 `RAMFS`）；② **康威生命游戏 `life`**（鼠标点击绘制/擦除细胞、空格键暂停/演化、`r` 随机种子、`c` 清空）。
  - 扩展 Start 开始菜单子项或快捷入口，鼠标点两下即可打开 `Files`、`Paint`、`Plot`、`Snake`！
- [ ] **Stage 15 验收**：鼠标打开 `Paint` 画一幅彩色图案保存并重新加载；打开 `Snake` 试玩计分；截图展示丰富的图形桌面生态。

---

### Stage 16：存储扩容（`RAMFS` 16 槽动态页）与宿主机串口快传管道
**目标**：打破“最多 8 个 16 KB 文件”的局促限制，并打通宿主机一键向运行中虚拟机秒传代码执行的开发快车道。

- [ ] **16.1 `RAMFS` 扩容至 16 文件 + 按需页帧伸缩 + `SaveFS v2` 兼容升级**（`c/kernel.c`）
  - 将 `MAX_RAMFILES` 提升至 **16**，单文件上限提升至 **64 KB**（按实际写入字节数以 4 KB 页粒度动态申请/释放帧池页面，空文件或短脚本仅占 1 页 = 4 KB，不再一上来固定吃掉 16 KB！）；
  - `SaveFS` 超块升级为 `"MSV2"`（支持最多 16 个文件目录项，仍位于 `LBA 2560–2819` 的 130 KB 扇区池内，且开机恢复同时兼容旧 `"MSV1"` 与新 `"MSV2"`）。
- [ ] **16.2 补齐 `syscall 28` 串口非阻塞收发与宿主机 `tools/push.py`**（`c/kernel.c`、`c/mpy_port/modminios.c`、`tools/push.py`）
  - 在 `kernel.c` 的 `syscall 28` (`SYS_HW_IO`) 中补齐 `op == 1`（串口发送缓冲区）与 `op == 2`（串口非阻塞读取可用字节，带 256B IRQ4 或轮询环形缓冲），并在 `modminios.c` 暴露 `minios.serial_write(data)` 与 `minios.serial_read(max_bytes)`；
  - 编写宿主机辅助工具 `tools/push.py`：配合 QEMU `-serial tcp:127.0.0.1:5555,server,nowait`，支持宿主机执行 `python tools/push.py my_script.py --run` 一键传入 MiniOS `RAMFS`！
- [ ] **Stage 16 验收**：创建 12 个不同大小的 RAMFS 文件（含一个 40 KB 大文件），执行 `sync` 并重启 QEMU 验证全部 CRC32 无损恢复；实测宿主机串口推送脚本成功运行。

---

### Stage 17：多进程管道通信（Pipe IPC）与安全自启脚本 `AUTOEXEC.PY`
**目标**：补齐进程间数据流通信能力，并赋予用户开机个性化定制系统的能力。

- [ ] **17.1 轻量级内核匿名/命名内存管道（Kernel Pipe IPC）**（`c/kernel.c`、`c/mpy_port/modminios.c`）
  - 实现 4 个内核级 4 KB 内存环形管道槽（物理页按需从帧池分配）：提供非阻塞/阻塞读写与关闭引用计数系统调用；
  - 在 Shell 中支持简易单级命令管道 `cmd1 | cmd2`（或先通过缓冲区重定向实现如 `ls | grep BIN`、`xxd TEST.TXT | grep MiniOS`），并在 `minios` 模块暴露进程间消息收发接口，使后台小任务与前台 Python 窗口可实时联动。
- [ ] **17.2 开机自启钩子 `AUTOEXEC.PY`（含 `Ctrl` 安全模式绕过）**（`c/kernel.c`、`c/gui.c`）
  - 系统启动完成 `savefs_restore()` 并首次绘制桌面后，检查 `RAMFS` 是否存在 `AUTOEXEC.PY`：
    - 若存在且启动时未按住 `Ctrl` 键，则在 Console 1 自动执行 `py AUTOEXEC.PY`；
    - **防锁死设计**：若启动时检测到 `ctrl_down`（或用户在执行中按 `Ctrl-C`），立即跳过 `AUTOEXEC.PY` 并提示 `[boot] AUTOEXEC.PY skipped (Safe Mode)`，确保写坏自启脚本也绝不会变砖！
- [ ] **Stage 17 验收**：编写 `AUTOEXEC.PY`（打印个性化欢迎横幅并切好配色主题），`sync` 后冷启动自动生效；验证按住 `Ctrl` 启动可安全跳过。

---

### Stage 18：桌面图标、多主题换肤与全系统自动化回归收官
**目标**：完成桌面视觉交互的最后一块拼图，并建立全自动防退化回归验收套件。

- [ ] **18.1 桌面快捷图标（Desktop Icons）与三套精选配色主题**（`c/gui.c`）
  - 在桌面空白区左侧绘制整齐的 `16×16` 像素级快捷方式图标列（`Terminal`、`Python`、`Files`、`Editor`、`Paint`、`System`），支持单击反色选中、双击直接打开对应窗口或应用；
  - 内置 3 套高对比度桌面配色主题（`Classic Teal` 经典青、`Midnight Dark` 暗夜蓝黑、`Warm Amber` 复古琥珀），支持 Shell 命令 `theme <name>` 与右键桌面菜单一键换肤！
- [ ] **18.2 全自动无人值守冒烟回归套件 `tools/smoke_test.py` 与总文档收官**（`tools/smoke_test.py`、`c/README.md`、`README.md`）
  - 编写 `tools/smoke_test.py`：自动启动 QEMU 依次覆盖 VBE `640×480` 与强制 `320×200` 回退双模式，自动执行命令测试、GUI 窗口开关缩放、MicroPython 工具集、`SaveFS` 读写重启验证，并在测试首尾比对 `minios.sysinfo()` 的 `free_frames`，断言 **0 页帧内存泄漏**！
  - 更新根目录 `README.md`（增加 MiniOS 2.0 `v2-protected-c` 高清 Python 桌面分支介绍与对比导航）及 `c/README.md` 最终空间/性能/功能全景文档。
- [ ] **Stage 18 验收**：`python tools/smoke_test.py` 全绿通过（`0 leaks, 0 faults`），产出三套主题下的 `640×480` 终极桌面全家福截图，镜像精确保持 `1,474,560` 字节！

















