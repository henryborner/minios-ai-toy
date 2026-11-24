#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>

#include "syscall.h"

int errno = 0;

FILE *stdin = (FILE *)1;
FILE *stdout = (FILE *)2;
FILE *stderr = (FILE *)3;

size_t strnlen(const char *s, size_t maxlen) {
    size_t n = 0;
    while (n < maxlen && s[n]) {
        n++;
    }
    return n;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    do {
        if (*s == (char)c) {
            last = s;
        }
    } while (*s++);
    return (char *)last;
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    while (isspace((unsigned char)*s)) {
        s++;
    }
    int neg = 0;
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        base = 16;
    } else if (base == 0) {
        base = (s[0] == '0') ? 8 : 10;
    }

    unsigned long acc = 0;
    int any = 0;
    for (;; s++) {
        unsigned char c = (unsigned char)*s;
        int d;
        if (isdigit(c)) d = c - '0';
        else if (isalpha(c)) d = tolower(c) - 'a' + 10;
        else break;
        if (d >= base) break;
        any = 1;
        acc = acc * (unsigned long)base + (unsigned long)d;
    }
    if (endptr) {
        *endptr = (char *)(any ? s : nptr);
    }
    return neg ? (unsigned long)(-(long)acc) : acc;
}

long strtol(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    while (isspace((unsigned char)*s)) {
        s++;
    }
    int neg = 0;
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    char *ep = NULL;
    unsigned long v = strtoul(s, &ep, base);
    if (endptr) {
        *endptr = (ep == s) ? (char *)nptr : ep;
    }
    return neg ? -(long)v : (long)v;
}

int atoi(const char *s) {
    return (int)strtol(s, NULL, 10);
}

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    uint8_t *arr = (uint8_t *)base;
    for (size_t i = 1; i < nmemb; i++) {
        size_t j = i;
        while (j > 0 && compar(arr + (j - 1) * size, arr + j * size) > 0) {
            uint8_t *a = arr + (j - 1) * size;
            uint8_t *b = arr + j * size;
            for (size_t k = 0; k < size; k++) {
                uint8_t tmp = a[k];
                a[k] = b[k];
                b[k] = tmp;
            }
            j--;
        }
    }
}

void exit(int status) {
    sys_exit_status((uint32_t)status);
}

void abort(void) {
    static const char msg[] = "[mpy] abort()\n";
    sys_write_bytes(msg, sizeof(msg) - 1);
    sys_exit_status(134);
}

void __assert_func(const char *file, int line, const char *func, const char *expr) {
    printf("Assertion '%s' failed at %s:%d (%s)\n", expr, file, line, func ? func : "?");
    abort();
}

/* Supplemental math functions not directly present as files in lib/libm_dbl */
double log2(double x) {
    return log(x) * 1.44269504088896340736;
}

double copysign(double x, double y) {
    union { double d; uint64_t u; } ux = { x }, uy = { y };
    ux.u = (ux.u & 0x7FFFFFFFFFFFFFFFull) | (uy.u & 0x8000000000000000ull);
    return ux.d;
}

double cbrt(double x) {
    if (x == 0.0 || !isfinite(x)) {
        return x;
    }
    int neg = x < 0.0;
    double ax = neg ? -x : x;
    double r = pow(ax, 1.0 / 3.0);
    /* One Halley/Newton refinement step */
    r = (2.0 * r + ax / (r * r)) / 3.0;
    return neg ? -r : r;
}

/* -----------------------------------------------------------------------------
 * 64-bit integer & float conversion compiler runtime builtins for 32-bit i386
 * ----------------------------------------------------------------------------- */
uint64_t __udivmoddi4(uint64_t num, uint64_t den, uint64_t *rem_p) {
    if (den == 0) {
        volatile uint32_t z = 0;
        z = 1u / z;
        (void)z;
        return 0;
    }
    uint64_t quot = 0;
    uint64_t qbit = 1;

    while ((int64_t)den >= 0) {
        den <<= 1;
        qbit <<= 1;
    }
    while (qbit) {
        if (den <= num) {
            num -= den;
            quot += qbit;
        }
        den >>= 1;
        qbit >>= 1;
    }
    if (rem_p) {
        *rem_p = num;
    }
    return quot;
}

uint64_t __udivdi3(uint64_t a, uint64_t b) {
    return __udivmoddi4(a, b, NULL);
}

uint64_t __umoddi3(uint64_t a, uint64_t b) {
    uint64_t r = 0;
    (void)__udivmoddi4(a, b, &r);
    return r;
}

int64_t __divdi3(int64_t a, int64_t b) {
    int neg = 0;
    uint64_t ua = (uint64_t)a;
    uint64_t ub = (uint64_t)b;
    if (a < 0) { ua = -ua; neg ^= 1; }
    if (b < 0) { ub = -ub; neg ^= 1; }
    uint64_t uq = __udivmoddi4(ua, ub, NULL);
    return neg ? -(int64_t)uq : (int64_t)uq;
}

int64_t __moddi3(int64_t a, int64_t b) {
    int neg = 0;
    uint64_t ua = (uint64_t)a;
    uint64_t ub = (uint64_t)b;
    if (a < 0) { ua = -ua; neg = 1; }
    if (b < 0) { ub = -ub; }
    uint64_t ur = 0;
    (void)__udivmoddi4(ua, ub, &ur);
    return neg ? -(int64_t)ur : (int64_t)ur;
}

double __floatundidf(uint64_t a) {
    double hi = (double)(uint32_t)(a >> 32);
    double lo = (double)(uint32_t)(a & 0xFFFFFFFFu);
    return hi * 4294967296.0 + lo;
}

double __floatdidf(int64_t a) {
    if (a < 0) {
        return -__floatundidf((uint64_t)(-(uint64_t)a));
    }
    return __floatundidf((uint64_t)a);
}

uint64_t __fixunsdfdi(double a) {
    if (a <= 0.0) return 0;
    double hi_d = a / 4294967296.0;
    uint32_t hi = (uint32_t)hi_d;
    uint32_t lo = (uint32_t)(a - (double)hi * 4294967296.0);
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

int64_t __fixdfdi(double a) {
    if (a < 0.0) {
        return -(int64_t)__fixunsdfdi(-a);
    }
    return (int64_t)__fixunsdfdi(a);
}
