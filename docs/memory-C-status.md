# C 阶段执行状态

## C1 用户多页表

- 每任务最多 20 张按需 4 KiB 用户页表
- 用户虚拟范围 `0x00400000..0x05400000`（4–84 MiB）
- 页表、页目录从低池分配
- `user_map_page/user_unmap_page/user_pte_get` 统一查找
- 空页表在 brk 收缩/任务回收时释放
- 低 4 KiB direct map 从 16 MiB 移到 `0xF0000000`（PDE960），
  避免用户堆占用 PDE 4–6 时覆盖内核 direct map

## C2 非连续物理

- 大任务初始 1 MiB heap 改为逐页分配、逐页映射
- 64 KiB 用户栈也改为逐页分配、逐页映射
- brk 增长逐页分配，不再要求连续物理
- 任务回收按 VA 解映射并释放页框
- 仍保留：私有 code 区是连续物理；这是当前边界

## C3 跨页表 brk

`BRK.BIN`（Ring3 asm）执行：

```text
brk(0x04500000)  -> 64 MiB heap window
写入 6/9/32/68 MiB
COM1: BRK64 OK
```

128 MiB QEMU：

```text
before BRK: free=30571 total=30688
after BRK:  free=30571 total=30688
```

## C4 MPY GC

- MPY GC 区从 1.5 MiB 扩到 10 MiB（虚拟 5–15 MiB）
- 4 MiB 与 8 MiB `bytearray` 活对象均通过：

```text
gc ok 8388608 65 66
```

- 没有引入 upstream `MICROPY_ENABLE_GC split-heap`；当前是单个更大的 GC
  region，物理页由 C1/C3 的非连续映射提供。

## C5 回收

- BRK64 退出后 free 精确回到 30571
- 20 轮同 Console `calc 2**10`：首次 MPY 缓存占 52 页，30571→30519；
  再 5 轮仍为 30519，无逐轮泄漏
- 20 次 `memfail 100` + `exec BRK.BIN`：20/20 均为 `BRK64 FAIL`，
  free 30571→30571，brk 中途失败回滚有效
- host tests PASS
- smoke PASS（640×480 + 320×200 + REGRESS）

## 未完成/边界

- 用户上限仍是 84 MiB，不是 1 GiB。
- 私有 code 区仍连续物理；heap/stack 已非连续。
- 未实现 MPY split-heap；8 MiB 活对象依赖 10 MiB 单 region。
- 失败回滚已用连续 20 轮注入验证。

## 当前构建

```text
kernel:  94,776 / 131,072 B
BSS:     0x34b44e
image:   1,474,560 bytes
smoke:   PASS
host:    PASS
```
