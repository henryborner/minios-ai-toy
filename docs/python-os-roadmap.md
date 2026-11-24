# MiniOS 2.0 × Python 演进总路线图与可执行任务清单

> 本文档为 `v2-protected-c` 分支后续演进的唯一主线蓝图与执行打卡表。
> 每完成一个 Stage，请在对应条目打钩 `[x]`，确保编译、QEMU 实测截图与回归验证全部通过后再推进下一阶段。

---

## 一、系统灵魂定位与五大标志性特色

MiniOS 2.0 不做臃肿的 POSIX 模仿者，也不做简陋的黑框命令行玩具。依托 **1.44 MB 单软盘纯粹形态 + ~30 KB C 微内核 + 抢占式多任务图形桌面**，系统核心定位为：

> **「1.44 MB 瞬启可编程微桌面（Instant-On Live-Scriptable Micro-Desktop）」**
> 融合 **KolibriOS 式的“极致轻快”** 与 **Oberon / PICO-8 式的“全桌面透明可编程”** —— 一张软盘装下高清图形桌面与完整 Python 3 工作站，冷启动不到半秒，敲一行 Python 就能秒算表达式、读写持久化脚本、透视内核状态、弹出彩色画布窗口实时绘图。

### 五大核心特色（Signature Pillars）

1. **极轻极快 · 毫秒级「热驻留瞬启」（Zero-Fat & Warm-Cached Instant Python）**
   - **渲染与磁盘快路径**：采用脏标记合批渲染 + RTC 秒级缓存 + 局部光标重绘 + FDC 整磁道（18 扇区）DMA 预读缓存，大幅提升终端输出吞吐与软盘冷读速度。
   - **母本内存热驻留（Warm Image Cache）**：利用 11 MB 物理页帧池缓存首次读入的 `MPY.BIN`。首次启动走磁道快读，**第二次起无论开 REPL 还是跑脚本均为 0 磁盘 I/O、`< 2 ms` 瞬时拉起**。
2. **Python 即 Shell · 行内秒算与多终端并发（`= expr` & Live Consoles）**
   - 在任意 Console 直接输入 `= (1<<16)-1, hex(3735928559)` 瞬出结果并回到提示符；Console 1 跑长任务或交互 REPL 时，桌面鼠标、窗口管理与 Console 2/3 依旧流畅并发。
3. **单软盘双区「零延迟 + 断电不丢」（Split-Floppy Snapshot Sync）**
   - 平时所有文件新建/修改均在 **纯内存 RAMFS** 中零延迟完成；在 1.44 MB 软盘尾部高扇区（LBA 2560–2819，130 KB）开辟带 CRC32 校验的快照区 `SaveFS`。一条 `sync`（或 `os.sync()`）即可将有效数据秒刷回软盘，下次冷启动自动校验恢复。
4. **全桌面透明内省与实时彩色画布（`import minios` + Live Canvas Window）**
   - 内置原生 C 模块 `minios`，十几行 Python 即可读取实时任务表、页帧位图、中断/帧率计数器、驱动 PC 蜂鸣器，或**直接弹出一个独立可拖动的 256 色像素画布窗口**绘制函数曲线、系统仪表盘与交互小程序。
5. **零寻道冻结瑞士军刀 + VBE 高清大桌面（Frozen Suite & 640×480 LFB Desktop）**
   - 将日常高频工具（计算器 `calc`、轻量编辑器 `ed`、十六进制查看器 `xxd`/`grep`、资源监控 `top`、图形演示 `demos`）通过字节码冻结进 `MPY.BIN`（0 软盘寻道开销）；并将桌面升级为 **VBE `640×480×8bpp` 高清线性帧缓冲（自动兼容回退 `320×200`）**，实现真正宽敞好用的多窗口日常工作台。

---

## 二、全局工程铁律与架构决策（必读）

1. **小步提交、每步可启动**：每个 Stage（必要时可拆分为 2 个子提交）必须独立通过 `python c/build_c.py`，产出镜像精确等于 `1,474,560` 字节，并通过 QEMU + `tools/qmon.py` 实测截图验证后再进入下一阶段。
2. **严守物理内存红线**：
   - `0x00000500–0x000005FF`：预留给实模式向保护模式传递启动/VBE 显示信息结构体；
   - `0x00008000–0x0000A3FF`：FDC 整磁道（18 扇区 = 9 KB）DMA 缓冲，**严禁跨越 `0x00010000` 的 64 KB ISA DMA 物理边界**；
   - `0x00010000–0x00020000`：内核加载区（128 扇区 = 64 KB 上限）；
   - `0x00020000–0x0006FFFF`：内核 `.bss` 安全区（`__bss_end` 必须严格 `< 0x00070000` `PACK_BASE`）；
   - `0x00100000–0x0017FFFF`：大模块文件加载暂存区（512 KB）；
   - `0x00500000–0x00FFFFFF`：11 MB 动态物理页帧池（内核通过 `0x01000000–0x01BFFFFF` 直映访问）。
3. **统一走 Ring 3（`UMP1`）正道**：MicroPython 采用 Ring 3 大任务布局（虚拟代码基址 `0x00400000` 上限 512 KB、用户栈 `0x00480000–0x00490000` 64 KB、动态/GC 堆 `0x00500000–0x00700000` 最大 2 MB），确保脚本或 C 扩展崩溃只回收任务不挂整机。
4. **MicroPython 上游源码管理规范（已拍板）**：
   - **上游源码放本地忽略目录**：将固定版本的官方 MicroPython 源码（如 `v1.24.1` 浅克隆）放在 **`tools/micropython/`** 下——仓库根目录 `.gitignore` 已忽略 `/tools/`，**绝不将数万行第三方上游源码或繁重的 submodule 塞进 Git 历史**。
   - **自研移植层进版本库**：仅将 MiniOS 专属移植代码 `c/mpy_port/`（含 `UPSTREAM_VERSION.txt` 记录精确 tag/commit）、冻结工具库 `c/pylib/` 与构建脚本 `c/build_mpy.py` 纳入 Git 跟踪。

---

## 三、分阶段详细执行清单（Stage 0 → Stage 8）

### Stage 0：收尾排雷 +「极轻极快」内核快路径改造
**目标**：消除现存小竞态，把桌面渲染与软盘读取速度榨到极致，并补齐本地自动化测试脚本的符号输入能力。

- [x] **0.1 修复运行期忙状态与按键抢占漏洞**（`c/kernel.c`、`c/gui.c`）
  - 在 `gui_poll_runtime()` 中：当 `runtime_busy == 1` 且 `!scheduler_active`（即运行 Ring 0 `mod` 或 `ring3` 演示）时，禁止后台轮询抽走当前活动 Console 的 `kbd_buf[cid]`，避免按键被 `gui_key()` 截胡。
  - 新增 `int kernel_runtime_busy(void)`（返回 `scheduler_active || runtime_busy`），在 `c/gui.c` 的 `run_command()` 中用它统一拦截危险重入命令（同时把 `modtest` 也纳入 `[busy]` 拦截列表）。
- [x] **0.2 GUI 脏标记合批渲染与 RTC 读取降频**（`c/gui.c`、`c/kernel.c`）
  - `update_clock()` 改为按 `ticks` 节流（每 `50` ticks = 0.5 秒才真正读写 `0x70/0x71` CMOS 端口更新字符串缓存），消除每次重绘的慢速端口 I/O。
  - 引入 `volatile u8 gui_dirty`：`gui_sys_write_console()` / `gui_sys_print_console()` 只更新终端字符缓冲并置 `gui_dirty = 1`，**不再每写一次就同步执行全屏 `gui_redraw()`**；改由 `kmain()` 主循环与 `IRQ0` 的 `gui_poll_runtime()` 按帧率上限（如每 2 ticks = 50 Hz）以及 `module_getchar()` 进入阻塞等待前统一 flush 重绘。
- [x] **0.3 鼠标纯移动局部光标快路径**（`c/gui.c`）
  - 当仅发生鼠标移动（按键状态未变、未处于窗口拖拽、未处于菜单/列表悬停变化、且 `gui_dirty == 0`）时，仅局部恢复旧 `12×18` 光标区域像素并绘制新位置光标，跳过全屏窗口重绘。
- [x] **0.4 FDC 整磁道预读缓存（Track Read Cache）**（`c/fdc.c`）
  - 利用 `0x00008000–0x0000A3FF`（18 扇区 = 9,216 字节，不跨 `0x10000` DMA 64 KB 边界）作为单磁道缓存，记录 `cached_cyl`、`cached_head` 与 `cache_valid`。
  - `fdc_read_sector()` 命中缓存磁道时直接内存拷贝 512 字节返回（0 硬件交互）；未命中时尝试从扇区 1 开始一次 DMA 读入整条磁道 18 个扇区（若整轨读失败则自动回退单扇区读）。
- [x] **0.5 升级 `tools/qmon.py`（本地忽略目录）与修正文档旧数字**
  - 在本地 `tools/qmon.py` 的 `type_text()` 中补全全部美式 Shift 符号映射（`! @ # $ % ^ & * ( ) _ + { } | : " ~ < > ?` → `shift-1`、`shift-semicolon` 等）以及 `\t` → `tab`。
  - 修正 `c/README.md` 镜像偏移表（128 扇区内核、Pack 偏移 `66048` / LBA 129）及 `docs/micropython-user-abi.md` 末尾残留的旧描述。
- [x] **Stage 0 验收**：脏标记/50 Hz 渲染、18 扇区整磁道缓存、光标局部重绘已落地；QEMU 回归 `utest`、`FD.BIN`、`tasks`、Console 2 运行期命令通过。连续 200 行和毫秒级冷/热加载基准留到 Stage 3 的正式 bench 记录。

---

### Stage 1：补齐用户态 ABI 最后拼图（大任务扩容、参数、目录枚举与进程控制）
**目标**：为完整功能的 Python 解释器准备好 512 KB 代码空间、`argc/argv`、无损长输出、`stat`/`readdir` 与 `ps`/`kill`。

- [x] **1.1 扩容大任务代码窗至 512 KB 并支持 `argc/argv`**（`c/kernel.c`）
  - 将 `BIG_CODE_MAX` 从 `0x00040000u`（256 KB）提升至 **`0x00080000u`（512 KB）**（`BIG_USER_STACK` 位于 `0x00480000`，虚拟空间天然预留了 512 KB）。
  - 实现 `int run_user_module_args(const char *name, int argc, const char *const *argv)`：在 `task_create_big()` 中将参数字符串与 `argv[]` 指针表压入大任务用户栈顶（`0x00490000` 向下生长），使入口处 `[esp] = argc, [esp+4] = argv[0]...` 与 `task_spawn_args` 约定一致。
- [x] **1.2 解除 `syscall 4` 的 4096 字节截断限制**（`c/kernel.c`）
  - 允许 `syscall 4` (`write bytes`) 写入任意位于用户合法映射区间内（单次上限放宽至 `BIG_HEAP_SIZE`）的字节流，完整输出并返回实际长度，越界指针返回 `-1`。
- [x] **1.3 新增 `stat` 与 `readdir` 系统调用（`syscalls 21–22`）**（`c/kernel.c`、`docs/micropython-user-abi.md`）
  - `syscall 21` (`stat`)：`EBX = name`, `ECX = user_stat_ptr`（写入 `{ u32 size; u32 kind; }`，`1=MiniFS`, `2=RAMFS`，优先查 RAMFS 再查 MiniFS；不存在返回 `-1`，成功返回 `0`）。
  - `syscall 22` (`readdir`)：`EBX = index`, `ECX = user_dirent_ptr`（写入 `{ char name[16]; u32 size; u32 kind; }`），按序枚举合并去重后的 RAMFS + MiniFS 文件列表（同名以 RAMFS 覆盖为准），成功返回 `1`，遍历结束返回 `0`。
- [x] **1.4 新增运行期进程查看 `ps` 与强制终止 `kill`**（`c/kernel.c`、`c/gui.c`）
  - Shell 新增安全命令 `ps`（即使有任务在跑也可从空闲 Console 执行）：打印活动任务槽的 `PID / Console / State / Pages / Name`。
  - Shell 新增 `kill <PID>` 命令（可在另一 Console 执行）及全局紧急热键（如 `Ctrl+Pause` 或 `Ctrl+\` 视情况设计）：安全回收目标 Ring 3 任务页帧、唤醒等待它的父进程，若无剩余活动任务则干净退出调度器回到桌面。
- [x] **Stage 1 验收**：`uargs` 验证 512 KB 大任务 `argc/argv`；`BIGWRITE.BIN` 验证 5000 字节输出；`DIR.BIN` 验证 RAMFS 覆盖 MiniFS 的 stat/readdir；`SPIN.BIN` + Console 2 `ps`/`kill 1` 验证死循环任务回收；`BIG512.BIN` 加载 300 KB UMP1 载荷成功。

---

### Stage 2：MicroPython i386 Ring 3 核心移植与点亮（`MPY.BIN`）
**目标**：将 MicroPython 编译为符合 `UMP1` 规范的 Ring 3 二进制，在 MiniOS 桌面终端内点亮完整 Python 3 REPL。

- [x] **2.1 搭建干净的本地上游源码与移植目录结构**
  - 拉取官方 MicroPython 稳定版源码（`v1.24.1` / `ecfdd5d6`）至 `tools/micropython/`（受 `.gitignore` 忽略），在 `c/mpy_port/UPSTREAM_VERSION.txt` 记录精确版本号与 commit hash。
  - 创建受 Git 跟踪的 `c/mpy_port/`（`mpconfigport.h`、`mphalport.c`、`mphalport.h`、`main.c`、`start.asm`、`mpy_linker.ld`、`minilibc.c`、`vfs_minios.c`）与独立并行构建脚本 `c/build_mpy.py`（编译产出 `c/build/mpy.bin`；`c/build_c.py` 自动构建并打包为 MiniFS 中的 `MPY.BIN`）。
- [x] **2.2 配置 `mpconfigport.h` 与浮点/大整数算术支撑**
  - 纯汇编实现 x86 NLR（`start.asm` 中无栈帧假设的 `nlr_push`/`nlr_jump_asm`）与寄存器根扫描 GC（`minios_gc_collect_regs`）。
  - 启用完整日常语法特性：编译器、GC、REPL 自动缩进与历史、`f-string`、`MPZ` 任意精度大整数、**64 位 IEEE-754 双精度浮点 `DOUBLE`（x87 53-bit 模式 + 完整 `libm_dbl` + 64 位整除运行时符号）**。
  - 启用核心内建模块：`sys`、`gc`、`micropython`、`array`、`collections`、`struct`、`math`、`json`、`re`、`binascii`、`heapq`、`random`、`time`、`errno`、`io`，并一步到位预置了 `open()` 与 `os` 模块（`vfs_minios.c`）。
- [x] **2.3 实现 `UMP1` 入口、动态堆初始化与 `Ctrl-C` 中断**
  - `start.asm` 置于 `0x00400000` 头部，准确填充 `UMP1` 头并兼容无参/带参栈布局，启动时调用 `syscall 20` (`brk`) 将用户堆扩至 **1.5 MB**（`0x500000–0x680000`，共 445 页）交给 `gc_init()`。
  - 新增非阻塞 `syscall 29` (`poll_ctrl_c`) 配合 VM 轮询钩子检测 `Ctrl-C` 触发 `mp_sched_keyboard_interrupt()`（**实测可打断 `while True: pass` 死循环抛出 `KeyboardInterrupt` 回到 `>>> `**），空行 `Ctrl-D` 正常退回 Shell；`main.c` 已同步支持 `-c`、`-e`、`-m` 与脚本参数模式。
- [x] **2.4 接入 Shell `py` / `python` 命令并实测**
  - 在 `c/gui.c` 添加带引号参数解析的 `py` 与 `python` 命令。
  - **Stage 2 验收**：**`MPY.BIN` 精确体积仅 `181,712` 字节（177.5 KB，`.bss` 仅 560 字节）**；实测 Console 1 启动 `py`、`2**128`、`round(3.1415926*2, 5)`、`json.dumps` + `math.sqrt`、函数定义、`Ctrl-C` 打断死循环、Console 2 并发 `ps`/`mem`、`os.listdir()`、`Ctrl-D` 退出与 `py -e "2**64"` 全部通过（截图 `screenshots/c-stage2-mpy-*.png`）。

---

### Stage 3：特色王牌 ① ——「瞬启热驻留镜像池」与 Shell `= expr` 秒算
**目标**：彻底消灭二次启动的软盘 I/O，让 Python 在系统里达到 `< 2 ms` 随叫随到的原生响应体验。

- [x] **3.1 实现内核大模块母本热缓存（Warm Image Cache）**（`c/kernel.c`）
  - 在物理页帧池（`5–16 MB`）中建立只读母本缓存槽（保存模块名、缓存物理页基址、页数及校验通过的 `UMP1` 头信息）。
  - `run_user_module_args()` 首次加载 `MPY.BIN` 时从 MiniFS 读入并写入缓存物理页；此后再次启动同名模块时，**直接从缓存物理页 `memcpy` 到任务代码页**，0 次软盘访问。
- [x] **3.2 支持 `py -c` / `py -e` 与 Shell `= <表达式>` 秒算语法糖**
  - 在 `c/mpy_port/main.c` 中解析命令行参数：
    - `py`（无参）：进入交互式 REPL；
    - `py -c "<code>"`：执行语句块后退出；
    - `py -e "<expr>"`：按表达式求值（`MP_PARSE_EVAL_INPUT`），若返回值不为 `None` 则自动打印 `repr` 结果后退出。
  - 在 `c/gui.c` 的 `run_command()` 中支持 `=` 前缀快捷求值：例如输入 `= 1024*768, hex(0xdeadbeef)` 直接调用热缓存中的 `MPY.BIN -e "..."` 打印结果并返回提示符。
- [x] **Stage 3 验收**：`MPY.BIN` 181,696B；首次 `py -e 2**64` 冷加载 **26 ticks**，第二次 **0 ticks**（`<=1`）；`= 2**100` 与 `pystat` 实测通过。另修复 HAL 在 readline 阶段直接 `mp_handle_pending` 导致 Ctrl-C 报 `[mpy] FATAL: uncaught NLR` 的问题。

---

### Stage 4：VFS 叠层文件读写、`import` 打通与终端体验升级
**目标**：让 Python 自由读写文件、`import` 脚本模块，并给终端加上历史回滚（Scrollback）与真实行内编辑游标。

- [x] **4.1 实现 Python 内置 `open()` 与 `os` 模块**（`c/mpy_port/`）
  - 基于 `syscalls 13–18` 与 `21–22` 实现完整文件流对象 `open(path, mode)`（支持 `"r"`、`"rb"`、`"w"`、`"wb"`、`"a"` 及 `with` 语句块）并绑定 `mp_builtin_open_obj`。
  - 实现 `os` 模块：`os.listdir()`、`os.stat(name)`、`os.remove(name)` / `os.unlink(name)`。
- [x] **4.2 打通 `import` 搜索链与 `py SCRIPT.PY` 直接执行**
  - 实现 `mp_import_stat()`（走 `syscall 21`）与 `mp_lexer_new_from_file()`：导入 `foo` 时自动匹配 `FOO.PY`（大小写不敏感，且 RAMFS 优先于 MiniFS，支持用户动态覆盖内置脚本）。
  - 支持 `py FILE.PY [arg1 arg2 ...]` 直接执行脚本并填充 `sys.argv`。
- [x] **4.3 终端 96 行回滚缓冲（Scrollback）与真实行内列游标**（`c/gui.c`、`c/kernel.c`）
  - 已升级为原生可点击/可拖拽滚动条：4px 轨道 + thumb、点击跳转、拖拽、PgUp/PgDn 按页；任务运行时鼠标仍可滚动；用户停在历史位置时新输出不强制跳底。鼠标滚轮尚未接入。
  - 补充原生桌面控件：窗口最小化按钮 + 窗口列表 `m` 标记、右键上下文菜单（Console 的滚动/清屏/最小化/关闭，桌面的 Console/System/About），任务运行时仍可用。
  - 将每个 Console 的缓冲扩展为环形历史缓冲（如 96 行），键盘驱动识别 `PgUp` (`0xE0 0x49`) / `PgDn` (`0xE0 0x51`) 上下翻阅历史输出（新按键/新输出自动回到底部），并在窗口右侧绘制简洁滚动位置指示条。
  - 为 Console 增加当前行独立游标列 `cursor_col`，完整支持 `\r`（回行首）、`\b`（游标左移）、`ESC [ D`（左移）、`ESC [ C`（右移）、`ESC [ H`（行首）、`ESC [ K`（清除游标至行尾），使 REPL 左右方向键在行中间插字/删字完全准确。
- [x] **Stage 4 验收**：实测 `open('T.TXT','w')` 写读、`with open('OUT.TXT','w')`、`os.listdir/stat`、`py TOOL.PY`、`import TOOL`；96 行 scrollback + PgUp/PgDn + 原生点击/拖拽滚动条、shell/REPL 行中插入、ZeroDivisionError Traceback 回滚均通过；`utest` 阻塞时鼠标滚动 Console 正常。另验证窗口最小化/列表恢复、桌面与 Console 右键菜单、任务运行时右键操作。截图见 `screenshots/c-stage4-*.png`、`c-scrollbar-*.png`、`c-gui-*.png`。

---

### Stage 5：特色王牌 ② —— 单软盘双区快照持久化（`SaveFS` + `sync`）
**目标**：用最精简可靠的设计实现“平时纯内存零延迟读写，一句 `sync` 真实落盘软盘，断电重启自动恢复”。

- [x] **5.1 补齐 FDC 写扇区驱动**（`c/fdc.c`、`c/fdc.h`）
  - 实现 `int fdc_write_sector(u32 lba, const u8 *src)`：拷贝 512 字节至 `0x8000`，配置 8237 DMA 通道 2 读内存模式（`0x4A`），发送 FDC `WRITE DATA`（`0xC5`）命令等待 IRQ6 并校验状态字节，同时同步更新或失效磁道缓存。
  - 更新启动脚本与文档中的 QEMU 参数，去掉软盘驱动器的 `readonly=on`。
- [x] **5.2 建立软盘尾部 `SaveFS` 快照区（LBA 2560–2819，130 KB）**（`c/kernel.c`、`c/build_c.py`）
  - 在 `c/build_c.py` 中将 `FS_MAX_BYTES` 调整为 `(2560 - 200) * 512`（约 1.15 MB），确保 `MiniFS` 绝不侵入 `LBA 2560+`；首次构建镜像时若保留现有快照可选支持，默认新镜像尾部清零。
  - 定义 `LBA 2560` 为 `MSV1` 超块扇区：包含魔数 `"MSV1"`、代数序号、文件计数、8 项 `{ char name[16]; u32 size; u32 sector_offset; u32 crc32; }` 与头部 CRC32；`LBA 2561–2816` 仅连续写入非空文件的有效扇区（写 1 KB 脚本仅需刷写 1+2=3 个扇区，耗时极短）。
- [x] **5.3 开机自动挂载恢复与 `sync` 接口**
  - 内核启动初始化时自动读取 `LBA 2560`，若 `"MSV1"` 魔数与各文件 CRC32 校验通过，自动分配物理页恢复进 `ramfiles[]`。
  - 新增 `syscall 23` (`sync`)、Shell `sync` 命令及 Python `os.sync()`。
- [x] **Stage 5 验收**：Python 写 `SAVED.PY` 并执行 `os.sync()`，Shell `sync` 显示 `[savefs] snapshot written`；退出并启动新的 QEMU 进程后 `ls` 显示 `SAVED.PY`，`py SAVED.PY` 输出 `42`。截图 `c-savefs-osync.png`、`c-savefs-sync.png`、`c-savefs-restored2.png`、`c-savefs-persisted.png`。

---

### Stage 6：特色王牌 ③ —— 全桌面内省与原生 `minios` 彩色画布窗口
**目标**：打通 Python 到内核状态与 GUI 窗口管理器的直通车，用十几行 Python 即可写出独立可拖动的彩色图形应用。

- [x] **6.1 升级 256 色调色板并新增「用户像素画布窗口（Canvas Window）」**（`c/gui.c`）
  - 保留调色板 `0–15` 号色与现有桌面 UI 完全一致，将 **`16–231` 号色初始化为标准 6×6×6 RGB 彩色立方体、`232–255` 号色初始化为 24 级平滑灰度**，解锁 256 色全彩表现力。
  - 新增可动态开关的通用窗口 `WIN_APP`（例如默认 `180×120` 客户区后备像素缓冲，从物理帧池或静态缓冲分配），支持动态修改窗口标题、Z-order 焦点切换、鼠标拖拽标题栏、记录客户区内鼠标相对坐标与按键、点击右上角 `x` 投递关闭事件。
- [x] **6.2 新增内核内省、画布与硬件交互系统调用（`syscalls 24–28`）**（`c/kernel.c`）
  - `syscall 24` (`sysinfo_ext`)：导出实时内核快照（`ticks`、总/已用物理页帧数与位图摘要、内核堆用量、FDC 读写扇区计数、IRQ0/1/6/12 触发计数、8 个任务槽详情）。
  - `syscall 25` (`canvas_ctl`)：打开/关闭/改尺寸或标题/刷新提交 `WIN_APP`。
  - `syscall 26` (`canvas_draw`)：内核级裁剪绘图原语（清屏、画点、Bresenham 直线、矩形填充/描边、圆、8×8 字体文本、整块字节 `blit`）。
  - `syscall 27` (`canvas_event`)：非阻塞拉取画布窗口事件（按键、鼠标相对坐标与按钮状态、窗口关闭请求）。
  - `syscall 28` (`hw_io`)：提供受控的 PC Speaker 蜂鸣器发声（PIT Channel 2 `0x42`/`0x61`）与非阻塞 `COM1` 串口收发字节能力。
- [x] **6.3 实现 MicroPython 原生模块 `import minios`**（`c/mpy_port/modminios.c`）
  - 提供干净直观的 Python API：`minios.sysinfo()`、`minios.tasks()`、`minios.beep(freq, ms)`、`minios.serial_read()` / `serial_write()`，以及 `minios.Canvas(title)` 绘图与事件接口。
- [x] **Stage 6 验收**：`minios.Canvas("Demo")` 打开 180×120 窗口，`clear/rect/line/text` 绘制 256 色图形；`minios.event()` 返回 `(89,54,0,0)`，`minios.sysinfo()` 返回 ticks/free frames/tasks 等；窗口支持焦点、拖动、最小化、关闭。`minios.beep` 与 syscalls24–28 已接入，`hw_io` 仅实现蜂鸣器，串口字节 API 尚未在该 syscall 内实现。

---

### Stage 7：特色王牌 ④ —— 零寻道冻结日常瑞士军刀集（Frozen Suite）
**目标**：将最常用的日常生产力工具预编译冻结进 `MPY.BIN`，开机零延迟即调即用。

- [ ] **7.1 在构建流中集成 `mpy-cross` 字节码冻结**（`c/build_mpy.py`）
  - 编译宿主机 `mpy-cross`（输出置于 `tools/micropython/mpy-cross/`），自动将 `c/pylib/*.py` 编译为冻结字节码链接进 `MPY.BIN`，实现 **0 软盘读取、0 编译期 GC 堆开销**。
- [ ] **7.2 编写并冻结 5 个核心日常工具模块（`c/pylib/`）**
  - `calc.py`：程序员速算器（同时展示十进制、十六进制、分组二进制、位宽、字节序拆分）。
  - `ed.py`：系统内轻量文本/代码编辑器（支持打开/新建文件、行号浏览、插入/修改/删除行、查找替换、`:w` 保存至 RAMFS、`:s` 调用 `os.sync()` 落盘软盘、`:r` 直接试跑脚本）。
  - `top.py`：字符版与图形版双模式实时系统监控器（展示任务槽、内存池占用地图、FDC/IRQ 计数与运行时间）。
  - `futil.py`：文件分析瑞士军刀（`cat`、`xxd` 十六进制+ASCII 双栏查看器、`grep`、`cp`、`df`）。
  - `demos.py`：精选交互图形应用（如 `plot` 数学函数曲线绘图仪、`sysmon` 实时图形仪表盘、`life` 彩色生命游戏）。
- [ ] **7.3 Shell 原生快捷命令映射**（`c/gui.c`）
  - 在 Shell 中添加快捷入口 `edit <FILE>`、`xxd <FILE>`、`top`、`calc <EXPR>`、`plot <EXPR>`，直接转发至热驻留的 `MPY.BIN` 对应冻结模块。
- [ ] **Stage 7 验收**：不借用宿主机串口，全程在 MiniOS 内用 `edit MYAPP.PY` 编写脚本并 `:s` 落盘，运行 `py MYAPP.PY`、`xxd MYAPP.PY`、`top` 与 `plot`，截图留档。

---

### Stage 8：VBE `640×480` 高清大桌面升级与「轻快账本」全面收官
**目标**：解锁高清大视野多窗口同屏工作流（同时保留 `320×200` 自动回退兼容），并产出完整的性能与体积量化报告。

- [ ] **8.1 Bochs/VESA VBE 2.0 `640×480×8bpp` 线性帧缓冲自适应**（`c/boot.asm`、`c/kernel.c`、`c/gui.c`）
  - 在 `c/boot.asm` 实模式阶段通过 `int 0x10 (AX=0x4F01/0x4F02)` 探测并启用 **`640×480×8bpp`（LFB 线性帧缓冲模式，带 `0x4000` LFB 标志）**，将 `{ magic, width, height, pitch, bpp, phys_base }` 写入 `0x00000500` 传给 32 位内核；若当前环境不支持 VBE 2.0，则自动无缝回退至 VGA `320×200` mode 13h。
  - 在内核页表初始化与任务页目录模板（`page_directory`、`sched_safe_pd`、`task_pd[]`）中为 VBE LFB 物理地址（如 QEMU 默认 `0xFD000000` 附近的 1 个 4 MB 页目录项）建立 Supervisor 直映。
  - 将后备帧缓冲 `fb`（`640×480` = 300 KB）改为从动态物理页帧池（`5–16 MB`）分配并通过 `direct_ptr` 访问——**不仅支撑高清分辨率，还顺带让低端内核 `.bss` 立即瘦身 64 KB！**
- [ ] **8.2 高清窗口布局与大尺寸终端适配**（`c/gui.c`）
  - 根据运行时屏幕宽高自适应布局：在 `640×480` 模式下，Console 窗口默认扩展至 **`72 列 × 26 行`**（可舒适并排摆放 Console 1 编辑器、Console 2 Python REPL 与右上角 `300×200` 彩色画布窗口），鼠标边界、任务栏、开始菜单与窗口列表坐标全部按屏幕宽高动态适配。
- [ ] **8.3 内置基准跑分 `bench.py` 与最终文档/账本收官**
  - 在 `c/pylib/bench.py` 中实现一键自检跑分，并在 `README.md` / `c/README.md` 更新完整量化成绩单与高清桌面全家福截图：
    - **1.44 MB 软盘镜像精确空间账单**（Boot / Kernel / Pack / MiniFS + `MPY.BIN` / SaveFS / 剩余空闲扇区）；
    - **速度指标**：冷启动耗时、首次冷加载 `MPY.BIN` 耗时、**二次热驻留启动耗时（`< 2 ms`）**、Python 算力与 GC 吞吐、高清桌面合成帧率；
    - 实测验证 VBE `640×480` 高清模式与强制回退 `320×200` 模式双路径均完美工作。
