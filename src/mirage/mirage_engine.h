/* SPDX-License-Identifier: MIT
 * The machine made playable: its 125 kHz analog output band-limited (an 8th-order Butterworth) and resampled to the
 * output rate, the factory-tuning clock trim, the fast disk, and the power-on boot. A C port of mirage-remake's
 * MirageEngine (MIT), without its threads: the caller serialises access (on the FM-1, the audio interrupt owns it). */
#pragma once
#include <stdint.h>
#include "mirage.h"
#include "mirage_math.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MIRAGE_ENGINE_MAX_BLOCK 512   /* the most output frames per mirage_engine_process call */
#define MIRAGE_FACTORY_TRIM_CENTS (-13.5f)
/* analog samples for one block: 125 kHz / the lowest output rate (32 kHz) x the block, and room */
#define MIRAGE_ENGINE_SCRATCH (MIRAGE_ENGINE_MAX_BLOCK * 4 + 8)

typedef struct {
    mreal b0, b1, b2, a1, a2, z1, z2;
} mirage_biquad_t;

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
} mirage_engine_t;

/* m: an initialised, reset machine with a boot disk in the drive (or a snapshot loaded) */
void mirage_engine_init(mirage_engine_t *e, mirage_t *m, float fs);
void mirage_engine_set_rate(mirage_engine_t *e, float fs);
void mirage_engine_set_clock_trim(mirage_engine_t *e, float cents);
/* power-on as the instrument does it, faster than real time: run until the display is lit and the drive has been
 * off for half a second (at most max_seconds of machine time); returns the machine seconds it took */
float mirage_engine_boot(mirage_engine_t *e, float max_seconds);
/* n output frames (mono, +-1), n <= MIRAGE_ENGINE_MAX_BLOCK; in: the audio input at the output rate, or 0 */
void mirage_engine_process(mirage_engine_t *e, float *out, int n, const float *in);

#ifdef __cplusplus
}
#endif
