# B 阶段执行状态

## 设计

32 位物理地址空间的上限从 512 MiB 提升到 2 GiB：

```text
低 8–16 MiB   4 KiB direct map，只给内核栈、guard、页表/fd 等低页对象
高 16 MiB+    4 MiB PSE direct map
              物理 0..2 GiB -> 虚拟 1..3 GiB
```

- 用户地址仍为 4 MB 起，VBE 仍在 0xE0000000，PSE direct map 只占 1–3 GiB。
- 每个任务页目录、`sched_safe_pd` 都安装同一组高 PSE 条目。
- 普通用户/内核数据、RAMFS、管道、MPY 堆可使用高页。
- 内核栈、页目录、页表、fd 表从低池分配，guard 继续使用 4 KiB direct。
- 帧 bitmap 覆盖 2 GiB；低/高分配都由同一 bitmap 记账。

## 实测

QEMU 只读镜像，COM1 `metrics`：

```text
32 MiB   total=6,112
768 MiB  total=194,528
1024 MiB total=260,064
2048 MiB total=522,208
```

2 GiB：

```text
MEMTEST initial=522092 alloc=64 rest=522092 ok=1
```

`ok=1` 包含：

- 64 个单页唯一分配/写读
- 8 MiB 连续区间
- 512 MiB 以上物理页写读
- guard PTE 解除/恢复且 CR3 不变
- double-free 不变更计数

128 MiB 下 `calc 2**10`、`selftest`、`regress`、FDC/disk 和 smoke 均通过。

## 边界

- 当前物理上限仍是 **2 GiB**，不是任意 4 GiB；定向映射窗口因此只占 1–3 GiB。
- bitmap 仍是静态 128 KiB，不是 B1 文档中的完全动态元数据；但它已从 512 MiB 静态 direct 页表（508 KiB）中释放出来，BSS 为 0x34a44e。
- 低池只有 8–16 MiB，专供内核栈/页表/fd；大量并发任务可能耗尽低池。
- 没有 B3 的临时映射槽：高物理页永久映射在 1–3 GiB。
- C 阶段（多页表用户堆、GC 扩展）尚未开始。

## 当前构建

```text
kernel:  93,012 / 131,072 B
BSS:     0x34a44e
image:   1,474,560 bytes
smoke:   PASS
host:    PASS
```
