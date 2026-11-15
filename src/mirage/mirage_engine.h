/* SPDX-License-Identifier: MIT
 * The machine made playable: its analog output band-limited (an 8th-order Butterworth) and resampled to the output
 * rate, the factory-tuning clock trim, the fast disk, the power-on boot, and the analog section on a second core.
 * After mirage-remake's MirageEngine (MIT), without its threads: the caller serialises access (on the FM-1, the
 * audio interrupt owns it).
 *
 * Two cores (mirage_engine_set_core2): while a block's machine runs on this core, the other plays the block before
 * it through the filters, the anti-alias filter and the resampler (mirage.h, split mode). The output is one block
 * late in every mode, so the engine can move between one core and two without a gap: it runs on one (lockstep)
 * until the OS has booted (the ROM tunes the filters by listening to them through the A/D converter), while the OS
 * samples (the same), while an audio input is given, and for a second after any other A/D read of the audio. */
#pragma once
#include <stdint.h>
#include "mirage.h"
#include "mirage_math.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MIRAGE_ENGINE_MAX_BLOCK 256   /* the most output frames per mirage_engine_process call */
#define MIRAGE_FACTORY_TRIM_CENTS (-13.5f)
/* analog samples for one block: the analog rate / the lowest output rate (32 kHz) x the block, and room */
#define MIRAGE_ENGINE_SCRATCH (MIRAGE_ENGINE_MAX_BLOCK * 4 + 32)

typedef struct {
    mreal b0, b1, b2, a1, a2, z1, z2;
} mirage_biquad_t;

/* how the engine gets the other core: start has it call mirage_engine_job(e) now (1), or cannot (0: the engine
 * runs it itself); wait returns once it has, 0, or nonzero when the job was dropped (the engine runs it itself) */
typedef struct {
    int (*start)(void *ctx);
    int (*wait)(void *ctx);
    void *ctx;
} mirage_core2_t;

typedef struct {
    mirage_t *m;
    mreal fs;            /* the output rate */
    mreal clock_scale;   /* the machine's speed against its nominal clocks */
    mreal step;          /* analog samples per output sample */
    mreal phase;
    mirage_biquad_t aa[4];
    float hist[4];       /* the last 4 filtered analog samples, for cubic interpolation */
    float last_in;
    float volume;        /* 0..1 */
    uint8_t factory_tuning, applied_factory_tuning;
    uint8_t fast_disk;   /* while the drive runs, run the machine ahead */
    uint8_t disk_turbo;  /* how far ahead: x this (8 in mirage-remake) */
    uint8_t motor;
    float scratch[MIRAGE_ENGINE_SCRATCH];
    float in_scratch[MIRAGE_ENGINE_SCRATCH];

    /* the output, one block late */
    float delay[MIRAGE_ENGINE_MAX_BLOCK];
    int delay_n;

    /* power-on: lockstep until the display has been lit with the drive off for half a second */
    uint8_t booted;
    float settled_s;

    /* two cores */
    const mirage_core2_t *core2;
    uint8_t split;                 /* the machine in split mode */
    int lock_blocks;               /* lockstep for this many more blocks (after a stale A/D read) */
    uint32_t stale_seen;
    mreal dphase;                  /* this core's copy of the resampler's phase, for the next record's length */
    int carry;                     /* analog samples made beyond what the resampler has taken (both cores agree) */
    mirage_stream_t stream[2];
    int cur;                       /* the record being made */
    const mirage_stream_t *job;    /* the record for the other core (0: none) */
    int job_need, job_frames;
    mreal job_step;
    float jq[16];                  /* the other core's analog samples made past what its last block took */
    int jq_n;
    uint32_t jobs, jobs_here;      /* records played: all, and on this core (the other could not) */
} mirage_engine_t;

/* m: an initialised, reset machine with a boot disk in the drive (or a snapshot loaded) */
void mirage_engine_init(mirage_engine_t *e, mirage_t *m, float fs);
void mirage_engine_set_rate(mirage_engine_t *e, float fs);
void mirage_engine_set_clock_trim(mirage_engine_t *e, float cents);
void mirage_engine_set_core2(mirage_engine_t *e, const mirage_core2_t *c2);   /* 0: one core */
/* power-on as the instrument does it, faster than real time: run until the display is lit and the drive has been
 * off for half a second (at most max_seconds of machine time); returns the machine seconds it took */
float mirage_engine_boot(mirage_engine_t *e, float max_seconds);
/* n output frames (mono, +-1), n <= MIRAGE_ENGINE_MAX_BLOCK, one block late; in: the audio input at the output
 * rate, or 0 */
void mirage_engine_process(mirage_engine_t *e, float *out, int n, const float *in);
/* the other core's job (mirage_core2_t.start): the pending record through the filters into the delayed output */
void mirage_engine_job(mirage_engine_t *e);

#ifdef __cplusplus
}
#endif
