/* SPDX-License-Identifier: MIT
 * What the core needs from the C library, in one place. The FM-1's toolchain has no libc headers but <stddef.h> and
 * <stdint.h>: memory is cleared and copied by hand here (no <string.h> anywhere in src/mirage), and the maths come
 * from fm1-base's fastmath.h there. The analog models' number type and maths:
 *   default            float with libm (desktop tools, host simulator)
 *   MIRAGE_FASTMATH    float with fm1-base's fastmath.h (the FM-1: no libm, single-precision FPU only)
 *   MIRAGE_DOUBLE      double with libm: the same arithmetic as mirage-remake, for the oracle tests
 * The digital parts (6809, VIA, ACIA, WD1772, DOC) are integer and identical in every build. */
#pragma once
#include <stddef.h>
#include <stdint.h>

#if defined(MIRAGE_DOUBLE)
#include <math.h>
typedef double mreal;
#define MR(x) (x)
#define m_tanh(x) tanh(x)
#define m_tan(x) tan(x)
#define m_exp2(x) exp2(x)
#define m_cos(x) cos(x)
#define m_fabs(x) fabs(x)
#define m_floor(x) floor(x)
#elif defined(MIRAGE_FASTMATH)
#include "fastmath.h"
typedef float mreal;
#define MR(x) (x##f)
#define m_tanh(x) fm_tanhf(x)
#define m_tan(x) fm_tanf(x)
#define m_exp2(x) fm_exp2f(x)
#define m_cos(x) fm_cosf(x)
#define m_fabs(x) fm_fabsf(x)
#define m_floor(x) fm_floorf(x)
#else
#include <math.h>
typedef float mreal;
#define MR(x) (x##f)
#define m_tanh(x) tanhf(x)
#define m_tan(x) tanf(x)
#define m_exp2(x) exp2f(x)
#define m_cos(x) cosf(x)
#define m_fabs(x) fabsf(x)
#define m_floor(x) floorf(x)
#endif

#define M_PI_R MR(3.14159265358979323846)

/* float helpers used in every build (single precision in mirage-remake too) */
static inline float m_tanhf_(float x)
{
#if defined(MIRAGE_FASTMATH)
    return fm_tanhf(x);
#else
    return tanhf(x);
#endif
}

/* lround for the A/D converter's value: half away from zero, as std::lround */
static inline long m_lroundf(float x)
{
#if defined(MIRAGE_FASTMATH)
    return x >= 0.0f ? (long)(x + 0.5f) : -(long)(0.5f - x);
#else
    return lroundf(x);
#endif
}

static inline void m_zero(void *p, size_t n)
{
    unsigned char *d = (unsigned char *)p;
    while (n--) *d++ = 0;
}

static inline void m_fill(void *p, unsigned char v, size_t n)
{
    unsigned char *d = (unsigned char *)p;
    while (n--) *d++ = v;
}

static inline void m_copy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
}

static inline int m_same(const void *a, const void *b, size_t n)   /* 1 when equal */
{
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    while (n--)
        if (*x++ != *y++) return 0;
    return 1;
}
