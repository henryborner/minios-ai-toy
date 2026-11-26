# A 阶段执行状态

基线计划：[memory-execution-plan.zh-CN.md](memory-execution-plan.zh-CN.md)

## 已完成

| 项 | 状态 | 证据 |
| --- | --- | --- |
| A1 分配器搜索 | 完成 | `frame_alloc_search.h` + `c/tests/frame_alloc_test.c`；64 次单页、跨 word、多页、交错占用 |
| A2 E820 交接 | 完成主路径 | boot header v1：magic/version/status/count/stride/buffer_end；E820 CF、EBX=0、SMAP、ECX 20/24、32 条截断 |
| A3 可用区规范化 | 完成 | `memmap_normalize.h` + `c/tests/memmap_test.c`；重叠、保留、1 字节洞、64 位高地址/高长度、空表 |
| A4 分配/回收账本 | 完成 | 独立 allocated 位图；free 先整段校验；double-free 被拒绝；memtest 恢复计数 |
| A5 guard/TLB | 完成 | `memtest` 实测 guard PTE 解除/恢复且 CR3 前后相同 |
| A6 诊断 | 完成 | `memtest`、32 MiB metrics/selftest/regress、跨 16 MiB 物理页写读、20 轮 calc |

## 实测

32 MiB：

```text
MEMTEST initial=5996 alloc=64 rest=5996 ok=1
[selftest] PASS
REGRESS SYNC_FAIL
REGRESS APPEND 0AB
REGRESS SEEKEND 3
REGRESS STATUSES 5 5
[metrics] free=5996 total=6112
```

`--m32` 页池与计划中的 6,112 一致。

20 轮同 Console `calc 2**10`：

```text
before: free=5996 total=6112
after:  free=5944 total=6112
extra 5 rounds: free=5944 total=6112
```

首次 MPY 模块/缓存留下 52 页，预热后稳定，不是逐轮泄漏。

`memmap` 实测：

```text
magic=1095583053 ver=1 status=1 count=6 stride=24
```

E820 handoff 已完成 6 条 QEMU 记录的正常路径。

宿主：

```text
HOST TESTS PASS
FRAME_ALLOC_TEST PASS 64 singles, cross-word, multi-page, interleaved
MEMMAP_TEST PASS overlap, reserved, byte-hole, high-base, high-len, empty
```

## 未完全关闭

- A2 的 20 字节 E820 返回和“截断后仍有后续”没有真实固件/QEMU 注入用例，只有代码路径。
- 无 E820 时不再使用 8–16 MiB 放行，但也没有 E801 fallback；会得到零可用页和一个明确的串口错误。
- A6 的 `regress` 会新建 Console；测试后 free 5996→5940 是持久 Console 资源，不是单任务泄漏。20 轮 calc 的预热基线为 5944。
- B/C 阶段（>512 MiB、进程多页表、GC 扩展）未开始。

## 当前构建

```text
kernel:  90,176 / 131,072 B
BSS:     0x3ae44e
image:   1,474,560 bytes
smoke:   PASS
```
