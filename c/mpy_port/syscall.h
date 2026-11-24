#ifndef MINIOS_MPY_SYSCALL_H
#define MINIOS_MPY_SYSCALL_H

#include <stdint.h>

#define SYS_WRITE_CSTR   1
#define SYS_EXIT         2
#define SYS_YIELD        3
#define SYS_WRITE_BYTES  4
#define SYS_GETCHAR      5
#define SYS_TICKS_MS     6
#define SYS_READ_FILE    7
#define SYS_SPAWN        8
#define SYS_WAIT         9
#define SYS_SLEEP_MS     10
#define SYS_EXIT_STATUS  11
#define SYS_OPEN_RO      12
#define SYS_READ         13
#define SYS_CLOSE        14
#define SYS_SEEK         15
#define SYS_OPEN_EX      16
#define SYS_WRITE        17
#define SYS_UNLINK       18
#define SYS_SPAWN_ARGS   19
#define SYS_BRK          20
#define SYS_STAT         21
#define SYS_READDIR      22
#define SYS_SYNC         23
#define SYS_POLL_CTRL_C  29

#define MINIOS_O_CREAT   1u
#define MINIOS_O_TRUNC   2u
#define MINIOS_O_WRITE   4u

struct minios_stat {
    uint32_t size;
    uint32_t kind; /* 1 = MiniFS, 2 = RAMFS */
};

struct minios_dirent {
    char name[16];
    uint32_t size;
    uint32_t kind;
};

static inline uint32_t minios_syscall0(uint32_t num) {
    uint32_t ret;
    __asm__ __volatile__("int $0x80" : "=a"(ret) : "a"(num) : "memory", "cc");
    return ret;
}

static inline uint32_t minios_syscall1(uint32_t num, uint32_t a1) {
    uint32_t ret;
    __asm__ __volatile__("int $0x80" : "=a"(ret) : "a"(num), "b"(a1) : "memory", "cc");
    return ret;
}

static inline uint32_t minios_syscall2(uint32_t num, uint32_t a1, uint32_t a2) {
    uint32_t ret;
    __asm__ __volatile__("int $0x80" : "=a"(ret) : "a"(num), "b"(a1), "c"(a2) : "memory", "cc");
    return ret;
}

static inline uint32_t minios_syscall3(uint32_t num, uint32_t a1, uint32_t a2, uint32_t a3) {
    uint32_t ret;
    __asm__ __volatile__(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3)
        : "memory", "cc"
    );
    return ret;
}

static inline int32_t sys_write_bytes(const void *buf, uint32_t len) {
    return (int32_t)minios_syscall2(SYS_WRITE_BYTES, (uint32_t)buf, len);
}

static inline int sys_getchar(void) {
    return (int)minios_syscall0(SYS_GETCHAR);
}

static inline uint32_t sys_ticks_ms(void) {
    return minios_syscall0(SYS_TICKS_MS);
}

static inline void sys_sleep_ms(uint32_t ms) {
    (void)minios_syscall1(SYS_SLEEP_MS, ms);
}

static inline void sys_yield(void) {
    (void)minios_syscall0(SYS_YIELD);
}

static inline void sys_exit_status(uint32_t code) __attribute__((noreturn));
static inline void sys_exit_status(uint32_t code) {
    (void)minios_syscall1(SYS_EXIT_STATUS, code);
    for (;;) {
        __asm__ __volatile__("pause");
    }
}

static inline int sys_open_ex(const char *name, uint32_t flags) {
    return (int)(int32_t)minios_syscall2(SYS_OPEN_EX, (uint32_t)name, flags);
}

static inline int sys_read(int fd, void *buf, uint32_t len) {
    return (int)(int32_t)minios_syscall3(SYS_READ, (uint32_t)fd, (uint32_t)buf, len);
}

static inline int sys_write(int fd, const void *buf, uint32_t len) {
    return (int)(int32_t)minios_syscall3(SYS_WRITE, (uint32_t)fd, (uint32_t)buf, len);
}

static inline int sys_seek(int fd, int32_t offset, int whence) {
    return (int)(int32_t)minios_syscall3(SYS_SEEK, (uint32_t)fd, (uint32_t)offset, (uint32_t)whence);
}

static inline int sys_close(int fd) {
    return (int)(int32_t)minios_syscall1(SYS_CLOSE, (uint32_t)fd);
}

static inline int sys_unlink(const char *name) {
    return (int)(int32_t)minios_syscall1(SYS_UNLINK, (uint32_t)name);
}

static inline uint32_t sys_brk(uint32_t addr) {
    return minios_syscall1(SYS_BRK, addr);
}

static inline int sys_stat_file(const char *name, struct minios_stat *st) {
    return (int)(int32_t)minios_syscall2(SYS_STAT, (uint32_t)name, (uint32_t)st);
}

static inline int sys_readdir_idx(uint32_t index, struct minios_dirent *de) {
    return (int)(int32_t)minios_syscall2(SYS_READDIR, index, (uint32_t)de);
}

static inline int sys_sync_fs(void) {
    return (int)(int32_t)minios_syscall0(SYS_SYNC);
}

static inline int sys_poll_ctrl_c(void) {
    return (int)minios_syscall0(SYS_POLL_CTRL_C);
}

#endif
