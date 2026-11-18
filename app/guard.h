/* SPDX-License-Identifier: MIT
 * The CPU-load guard: the emulated Mirage gets a share of the audio interrupt's time, never all of it.
 *
 * Mirage 0.1.0 ran its machine in every block whatever it cost. At 228 % of the block's time, the audio interrupt
 * ran back to back and the main loop, which runs the screen, the keys and USB, got whatever was left. That is no way
 * to leave an FM-1 that someone may want to update. With the guard:
 *   - The machine earns MGUARD_SHARE_PCT of each block's time as credit and spends what its block really took. A
 *     block it has no credit for is skipped: silence, the machine paused. On average it uses at most that share,
 *     whatever it costs, and the rest is the main loop's.
 *   - When half of the blocks in about a second are skipped, the guard trips: the machine stops, the screen says so,
 *     and it tries again after MGUARD_RETRY_MS. The third trip stops it until the FM-1 is switched off.
 * Pure logic (times in, decisions out) so that tests/guard_test.c can drive it without a clock. */
#pragma once
#include <stdint.h>

#define MGUARD_SHARE_PCT 80u     /* the machine's share of each block's time, on average */
#define MGUARD_CAP_BLOCKS 3u     /* the most credit kept: a block that costs more than 3 blocks' time never runs */
#define MGUARD_WINDOW 172u       /* blocks a trip is judged over: about a second at 256 frames and 44.1 kHz */
#define MGUARD_TRIP_SKIPS 86u    /* skipped in a window: the guard trips (half of them) */
#define MGUARD_RETRY_MS 5000u    /* a trip: the machine tries again after this */
#define MGUARD_MAX_TRIPS 3u      /* the third trip: stopped until power-off */

enum { MGUARD_RUN, MGUARD_TRIPPED, MGUARD_STOPPED };

typedef struct {
    uint32_t credit;             /* ticks the machine may still spend */
    uint32_t last;               /* what its last block took, in ticks: the price of the next */
    uint32_t retry_at;           /* MGUARD_TRIPPED: when to try again (ms) */
    uint32_t skipped_total;      /* blocks skipped since the start */
    uint16_t seen, skipped;      /* this window's blocks, and those skipped */
    uint8_t state, trips;
    uint8_t resumed;             /* set when a trip ends: the caller clears the machine's held notes */
} mguard_t;

/* the start of a block of budget ticks at now_ms: 1 when the machine may run it (then call mguard_end) */
static inline int mguard_begin(mguard_t *g, uint32_t budget, uint32_t now_ms)
{
    const uint32_t cap = budget * MGUARD_CAP_BLOCKS;
    int run;
    if (g->state == MGUARD_TRIPPED && (int32_t)(now_ms - g->retry_at) >= 0) {
        g->state = MGUARD_RUN;   /* again, from a clean slate */
        g->credit = cap;
        g->last = 0;
        g->seen = g->skipped = 0;
        g->resumed = 1;
    }
    if (g->state != MGUARD_RUN)
        return 0;
    g->credit += budget / 100u * MGUARD_SHARE_PCT;
    if (g->credit > cap)
        g->credit = cap;
    run = g->credit >= g->last;
    if (!run) {
        g->skipped++;
        g->skipped_total++;
    }
    if (++g->seen >= MGUARD_WINDOW) {
        if (g->skipped >= MGUARD_TRIP_SKIPS) {
            g->state = ++g->trips >= MGUARD_MAX_TRIPS ? MGUARD_STOPPED : MGUARD_TRIPPED;
            g->retry_at = now_ms + MGUARD_RETRY_MS;
            run = 0;
        }
        g->seen = g->skipped = 0;
    }
    return run;
}

/* the machine's block took used ticks */
static inline void mguard_end(mguard_t *g, uint32_t used)
{
    g->last = used;
    g->credit = g->credit > used ? g->credit - used : 0u;
}

/* the screen's line for a state other than MGUARD_RUN: 0 when running */
static inline const char *mguard_text(const mguard_t *g)
{
    return g->state == MGUARD_TRIPPED ? "CPU OVERLOAD: SOUND PAUSED"
           : g->state == MGUARD_STOPPED ? "CPU OVERLOAD: SOUND OFF" : 0;
}
