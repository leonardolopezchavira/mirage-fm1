/* SPDX-License-Identifier: MIT
 * The analog models' number type and maths, in one place:
 *   default            float with libm (desktop tools, host simulator)
 *   MIRAGE_FASTMATH    float with fm1-base's fastmath.h (the FM-1: no libm, single-precision FPU only)
 *   MIRAGE_DOUBLE      double with libm: the same arithmetic as mirage-remake, for the oracle tests
 * The digital parts (6809, VIA, ACIA, WD1772, DOC) are integer and identical in every build. */
#pragma once

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
