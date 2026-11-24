#ifndef _MINIOS_MATH_H
#define _MINIOS_MATH_H

#include <stdint.h>

typedef float float_t;
typedef double double_t;

#define NAN       (__builtin_nanf(""))
#define INFINITY  (__builtin_inff())
#define HUGE_VAL  (__builtin_inf())
#define HUGE_VALF (__builtin_inff())

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E  2.71828182845904523536
#endif

#define FP_NAN       0
#define FP_INFINITE  1
#define FP_ZERO      2
#define FP_SUBNORMAL 3
#define FP_NORMAL    4

int __fpclassify(double x);
int __signbitd(double x);

#define fpclassify(x) __fpclassify((double)(x))
#define isnan(x)      __builtin_isnan(x)
#define isinf(x)      __builtin_isinf(x)
#define isfinite(x)   __builtin_isfinite(x)
#define signbit(x)    __signbitd((double)(x))

static inline double fabs(double x) { return __builtin_fabs(x); }
static inline float fabsf(float x) { return __builtin_fabsf(x); }
static inline double nan(const char *tagp) { (void)tagp; return __builtin_nan(""); }
static inline float nanf(const char *tagp) { (void)tagp; return __builtin_nanf(""); }

double copysign(double x, double y);
double sqrt(double x);
double cbrt(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double sinh(double x);
double cosh(double x);
double tanh(double x);
double asinh(double x);
double acosh(double x);
double atanh(double x);
double exp(double x);
double expm1(double x);
double log(double x);
double log10(double x);
double log1p(double x);
double log2(double x);
double pow(double x, double y);
double ceil(double x);
double floor(double x);
double trunc(double x);
double round(double x);
double nearbyint(double x);
double rint(double x);
double fmod(double x, double y);
double modf(double x, double *iptr);
double frexp(double x, int *exp);
double ldexp(double x, int exp);
double scalbn(double x, int n);
double erf(double x);
double erfc(double x);
double tgamma(double x);
double lgamma(double x);

#endif
