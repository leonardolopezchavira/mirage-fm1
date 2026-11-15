/* SPDX-License-Identifier: MIT
 * One Curtis CEM3328 4-pole low-pass VCF as wired in the Mirage: cutoff and resonance come from 8-bit DAC values
 * held on sample-and-hold capacitors. A topology-preserving (TPT) 4-pole ladder with a soft-saturating feedback
 * path, so that it self-oscillates at high resonance (the boot ROM's filter tuning relies on this).
 * A C port of mirage-remake's Cem3328 (MIT), in mreal (mirage_math.h). */
#pragma once
#include <stdint.h>
#include "mirage_math.h"

typedef struct {
    mreal ref_hz;            /* cutoff at DAC value ref_code */
    mreal ref_code;
    mreal steps_per_octave;  /* measured: OS "tracking 4 = 1 octave" moves 2 codes per semitone */
    mreal max_hz, min_hz;
    mreal max_resonance;     /* feedback gain at resonance DAC = 255 (4.0 is the onset of self-oscillation) */
    mreal input_gain;
} filter_tuning_t;

typedef struct {
    const filter_tuning_t *tune;
    mreal fs, detune;
    uint8_t cut, res, dirty;
    mreal k, G, G2, G3, G4, a;
    mreal aG, aG2, aG3, inv;   /* the fast build's: a x G^n, and 1 / (1 + k G^4) */
    mreal s1, s2, s3, s4, y;
} cem3328_t;

static void tuning_defaults(filter_tuning_t *t)
{
    t->ref_hz = MR(1231.0);
    t->ref_code = MR(176.0);   /* 0xB0 */
    t->steps_per_octave = MR(24.0);
    t->max_hz = MR(22000.0);
    t->min_hz = MR(5.0);
    t->max_resonance = MR(4.25);
    t->input_gain = MR(0.5);
}

static void vcf_init(cem3328_t *f, const filter_tuning_t *t, mreal fs, mreal detune)
{
    f->tune = t;
    f->fs = fs;
    f->detune = detune;
    f->cut = f->res = 0;
    f->k = 0;
    f->G = f->G2 = f->G3 = f->G4 = f->a = 0;
    f->s1 = f->s2 = f->s3 = f->s4 = f->y = 0;
    f->dirty = 1;
}

static inline void vcf_reset(cem3328_t *f) { f->s1 = f->s2 = f->s3 = f->s4 = 0; f->y = 0; }
static inline void vcf_set_cutoff(cem3328_t *f, uint8_t c) { if (c != f->cut) { f->cut = c; f->dirty = 1; } }
static inline void vcf_set_resonance(cem3328_t *f, uint8_t r)
{
#ifndef MIRAGE_EXACT
    if (r != f->res) f->dirty = 1;   /* inv follows k */
#endif
    f->res = r;
    f->k = f->tune->max_resonance * (mreal)r / MR(255.0);
}

static mreal vcf_cutoff_hz(const cem3328_t *f)
{
    mreal hz = f->tune->ref_hz * m_exp2(((mreal)f->cut - f->tune->ref_code + f->detune) / f->tune->steps_per_octave);
    if (hz > f->tune->max_hz) hz = f->tune->max_hz;
    if (hz < f->tune->min_hz) hz = f->tune->min_hz;
    if (hz > f->fs * MR(0.45)) hz = f->fs * MR(0.45);
    return hz;
}

static void vcf_update(cem3328_t *f)
{
    const mreal g = m_tan(M_PI_R * vcf_cutoff_hz(f) / f->fs);
    f->G = g / (MR(1.0) + g);
    f->G2 = f->G * f->G;
    f->G3 = f->G2 * f->G;
    f->G4 = f->G3 * f->G;
    f->a = MR(1.0) / (MR(1.0) + g);
    f->aG = f->a * f->G;
    f->aG2 = f->a * f->G2;
    f->aG3 = f->a * f->G3;
    f->inv = MR(1.0) / (MR(1.0) + f->k * f->G4);
    f->dirty = 0;
}

static inline float vcf_process(cem3328_t *f, float in)
{
    mreal x, S, y4est, u, v, y;
    if (f->dirty) vcf_update(f);
    x = (mreal)in * f->tune->input_gain;
    /* a linear estimate of the 4th stage's output, then a saturating input stage */
#ifdef MIRAGE_EXACT
    S = f->G3 * f->s1 * f->a + f->G2 * f->s2 * f->a + f->G * f->s3 * f->a + f->s4 * f->a;
    y4est = (f->G4 * x + S) / (MR(1.0) + f->k * f->G4);
#else
    S = f->aG3 * f->s1 + f->aG2 * f->s2 + f->aG * f->s3 + f->a * f->s4;
    y4est = (f->G4 * x + S) * f->inv;
#endif
    u = m_tanh(x - f->k * y4est);
    v = (u - f->s1) * f->G; y = v + f->s1; f->s1 = y + v;
    v = (y - f->s2) * f->G; y = v + f->s2; f->s2 = y + v;
    v = (y - f->s3) * f->G; y = v + f->s3; f->s3 = y + v;
    v = (y - f->s4) * f->G; y = v + f->s4; f->s4 = y + v;
    f->y = y;
    return (float)y;
}
