/* SPDX-License-Identifier: MIT
 * The engine (mirage_engine.h says what it does), after mirage-remake's MirageEngine (MIT). */
#include "mirage_engine.h"

static void design_filter(mirage_engine_t *e)
{
    /* an 8th-order Butterworth low-pass as four biquads (bilinear transform) */
    mreal fc = MR(0.42) * e->fs, K;
    int i;
    if (fc > MR(18000.0)) fc = MR(18000.0);
    K = m_tan(M_PI_R * fc / (mreal)MIRAGE_ANALOG_HZ);
    for (i = 0; i < 4; ++i) {
        const mreal q = MR(1.0) / (MR(2.0) * m_cos(M_PI_R * (mreal)(2 * i + 1) / MR(16.0)));
        const mreal norm = MR(1.0) / (MR(1.0) + K / q + K * K);
        mirage_biquad_t *b = &e->aa[i];
        b->b0 = K * K * norm;
        b->b1 = 2 * b->b0;
        b->b2 = b->b0;
        b->a1 = 2 * (K * K - 1) * norm;
        b->a2 = (1 - K / q + K * K) * norm;
        b->z1 = b->z2 = 0;
    }
}

static inline float biquad_run(mirage_biquad_t *b, float x)
{
    const mreal y = b->b0 * x + b->z1;
    b->z1 = b->b1 * x - b->a1 * y + b->z2;
    b->z2 = b->b2 * x - b->a2 * y;
    return (float)y;
}

void mirage_engine_set_clock_trim(mirage_engine_t *e, float cents)
{
    e->clock_scale = m_exp2((mreal)cents / MR(1200.0));
    e->step = (mreal)MIRAGE_ANALOG_HZ * e->clock_scale / e->fs;
}

void mirage_engine_set_rate(mirage_engine_t *e, float fs)
{
    e->fs = (mreal)fs;
    e->step = (mreal)MIRAGE_ANALOG_HZ * e->clock_scale / e->fs;
    design_filter(e);
}

void mirage_engine_init(mirage_engine_t *e, mirage_t *m, float fs)
{
    m_zero(e, sizeof *e);
    e->m = m;
    e->fs = (mreal)fs;
    e->clock_scale = 1;
    e->volume = 0.8f;
    e->factory_tuning = e->applied_factory_tuning = 1;
    e->fast_disk = 1;
    e->disk_turbo = 8;
    /* At the nominal 8 MHz the factory library plays ~13.5 cents sharp with Master Tune at its default of 50, while
     * sounds sampled on the machine play in tune. Running the whole machine 13.5 cents slow puts the factory sounds
     * at A440 and leaves sampled sounds in tune, so Master Tune 50 is right for both. */
    mirage_engine_set_clock_trim(e, MIRAGE_FACTORY_TRIM_CENTS);
    design_filter(e);
}

void mirage_engine_set_core2(mirage_engine_t *e, const mirage_core2_t *c2) { e->core2 = c2; }

float mirage_engine_boot(mirage_engine_t *e, float max_seconds)
{
    const uint64_t c0 = mirage_cycles(e->m);
    const int steps = (int)(max_seconds * 20.0f);
    int i, idle = 0;
    for (i = 0; i < steps && idle < 10; ++i) {
        int lit;
        mirage_run(e->m, MIRAGE_CPU_HZ / 20);   /* 50 ms */
        lit = mirage_display(e->m, 0) || mirage_display(e->m, 1);
        idle = (!mirage_motor(e->m) && lit) ? idle + 1 : 0;
    }
    mirage_run(e->m, MIRAGE_CPU_HZ / 5);
    mirage_disk_clear_dirty(e->m);
    e->booted = 1;
    return (float)(uint32_t)(mirage_cycles(e->m) - c0) / (float)MIRAGE_CPU_HZ;
}

/* the analog samples a block of frames takes from phase *ph, stepping it as resample() will (so both cores agree to
 * the last bit); *ph is left where resample() will leave the phase */
static int count_need(mreal step, int frames, mreal *ph)
{
    mreal p = *ph;
    int i, need = 0;
    for (i = 0; i < frames; ++i) {
        p += step;
        while (p >= 1) {
            need++;
            p -= 1;
        }
    }
    *ph = p;
    return need;
}

/* need analog samples (from src, n of them, n >= need) through the anti-alias filter and the resampler into frames
 * output samples; returns how many of src it took (need) */
static int resample(mirage_engine_t *e, const float *src, int need, int frames, mreal step, float *out)
{
    const float gain = e->volume * 0.8f;
    int i, k, consumed = 0;
    for (i = 0; i < frames; ++i) {
        float t, p0, p1, p2, p3, y;
        e->phase += step;
        while (e->phase >= 1 && consumed < need) {
            float x = src[consumed++];
            for (k = 0; k < 4; ++k) x = biquad_run(&e->aa[k], x);
            e->hist[0] = e->hist[1];
            e->hist[1] = e->hist[2];
            e->hist[2] = e->hist[3];
            e->hist[3] = x;
            e->phase -= 1;
        }
        /* Catmull-Rom between hist[1] and hist[2] */
        t = (float)e->phase;
        p0 = e->hist[0]; p1 = e->hist[1]; p2 = e->hist[2]; p3 = e->hist[3];
        y = p1 + 0.5f * t * (p2 - p0 + t * (2 * p0 - 5 * p1 + 4 * p2 - p3 + t * (3 * (p1 - p2) + p3 - p0)));
        y *= gain;
        out[i] = y < -1.0f ? -1.0f : y > 1.0f ? 1.0f : y;
    }
    return consumed;
}

void mirage_engine_job(mirage_engine_t *e)
{
    const mirage_stream_t *s = e->job;
    int i, have, took;
    if (!s) return;
    for (i = 0; i < e->jq_n; ++i) e->scratch[i] = e->jq[i];
    mirage_analog_run(e->m, s, e->scratch + e->jq_n);
    have = e->jq_n + s->steps;
    took = resample(e, e->scratch, e->job_need < have ? e->job_need : have, e->job_frames, e->job_step, e->delay);
    e->jq_n = 0;
    for (i = took; i < have && e->jq_n < 16; ++i) e->jq[e->jq_n++] = e->scratch[i];
    e->delay_n = e->job_frames;
    e->jobs++;
}

/* one block on this core, filters and all, into the delayed output */
static void lockstep(mirage_engine_t *e, int frames, const float *in)
{
    const float *in_analog = 0;
    mreal ph = e->phase;
    int need = count_need(e->step, frames, &ph), k;
    if (need > MIRAGE_ENGINE_SCRATCH - 4) need = MIRAGE_ENGINE_SCRATCH - 4;
    if (in && frames > 0 && need > 0) {
        /* the input at the output rate to the analog rate (linear: VCF 7 band-limits it) */
        for (k = 0; k < need; ++k) {
            const mreal pos = (mreal)(k + 1) * (mreal)frames / (mreal)need - 1;   /* in output samples */
            const int i0 = (int)m_floor(pos);
            const float fr = (float)(pos - (mreal)i0);
            const float a = i0 < 0 ? e->last_in : in[i0 < frames - 1 ? i0 : frames - 1];
            const int i1 = i0 + 1 < 0 ? 0 : i0 + 1 > frames - 1 ? frames - 1 : i0 + 1;
            const float b = in[i1];
            e->in_scratch[k] = a + (b - a) * fr;
        }
        e->last_in = in[frames - 1];
        in_analog = e->in_scratch;
    }
    mirage_render(e->m, e->scratch, need, in_analog);
    resample(e, e->scratch, need, frames, e->step, e->delay);
    e->delay_n = frames;
}

void mirage_engine_process(mirage_engine_t *e, float *out, int frames, const float *in)
{
    mirage_t *m = e->m;
    int i, lock;

    if (frames > MIRAGE_ENGINE_MAX_BLOCK) frames = MIRAGE_ENGINE_MAX_BLOCK;
    if (e->factory_tuning != e->applied_factory_tuning) {
        e->applied_factory_tuning = e->factory_tuning;
        mirage_engine_set_clock_trim(e, e->factory_tuning ? MIRAGE_FACTORY_TRIM_CENTS : 0.0f);
    }
    if (!e->booted) {   /* power-on: the display lit with the drive off for half a second */
        const int lit = mirage_display(m, 0) || mirage_display(m, 1);
        e->settled_s = lit && !mirage_motor(m) ? e->settled_s + (float)frames / (float)e->fs : 0.0f;
        if (e->settled_s >= 0.5f) e->booted = 1;
    }
    if (e->split && mirage_adc_stale(m) != e->stale_seen) {   /* the OS read the audio: a second in lockstep */
        e->stale_seen = mirage_adc_stale(m);
        e->lock_blocks = (int)(e->fs / (mreal)frames) + 1;
    }
    lock = !e->core2 || in || !e->booted || mirage_sampling(m) || e->lock_blocks > 0
           || (int)((mreal)frames * e->step) + 4 > MIRAGE_STREAM_STEPS - 8;

    /* fast disk: while the drive runs, let the machine run ahead (the OS mutes the sound while loading anyway) */
    e->motor = (uint8_t)mirage_motor(m);
    if (e->motor && e->fast_disk && e->disk_turbo > 1)
        mirage_run(m, (uint32_t)((mreal)frames / e->fs * (mreal)MIRAGE_CPU_HZ * e->clock_scale
                                 * (mreal)(e->disk_turbo - 1)));

    if (lock && e->split) {   /* back to one core: the record waiting is played here first */
        if (e->job) {
            mirage_engine_job(e);
            e->jobs_here++;
            e->job = 0;
        }
        mirage_put_pending(m, e->jq, e->jq_n);
        e->jq_n = 0;
        mirage_set_split(m, 0);
        e->split = 0;
    } else if (!lock && !e->split) {   /* to two: the analog samples a lockstep render made ahead go with them */
        mirage_set_split(m, 1);
        e->split = 1;
        e->dphase = e->phase;
        e->jq_n = mirage_take_pending(m, e->jq, 16);
        e->carry = e->jq_n;
        e->job = 0;
    }

    if (!e->split) {
        for (i = 0; i < frames; ++i) out[i] = i < e->delay_n ? e->delay[i] : 0.0f;
        lockstep(e, frames, in);
        if (e->lock_blocks > 0) e->lock_blocks--;
        return;
    }

    {
        const int started = e->job ? e->core2->start(e->core2->ctx) : 0;
        mirage_stream_t *s = &e->stream[e->cur];
        const int need = count_need(e->step, frames, &e->dphase);
        int req = need - e->carry, got;
        if (req < 0) req = 0;
        got = mirage_render_split(m, s, req);
        e->carry += got - need;
        if (e->job) {
            if (!started || e->core2->wait(e->core2->ctx) != 0) {
                mirage_engine_job(e);
                e->jobs_here++;
            }
        }
        for (i = 0; i < frames; ++i) out[i] = i < e->delay_n ? e->delay[i] : 0.0f;
        e->job = s;
        e->job_need = need;
        e->job_frames = frames;
        e->job_step = e->step;
        e->cur ^= 1;
    }
}
