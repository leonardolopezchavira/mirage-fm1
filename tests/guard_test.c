/* SPDX-License-Identifier: MIT
 * app/guard.h: the machine's share of the audio interrupt, at steady loads from light to impossible. */
#include <stdio.h>
#include "../app/guard.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define BUDGET 139317u   /* 256 frames at 44.1 kHz, in 24 MHz ticks */
#define BLOCK_MS 5.805

typedef struct { uint32_t blocks, ran, first_trip_block, last_state; uint64_t used; } run_t;

/* `seconds` of blocks whose machine costs load_pct of a block each */
static run_t steady(mguard_t *g, uint32_t load_pct, double seconds)
{
    run_t r = {0, 0, 0, 0, 0};
    const uint32_t n = (uint32_t)(seconds * 1000.0 / BLOCK_MS);
    uint32_t i;
    for (i = 0; i < n; i++) {
        const uint32_t now = (uint32_t)(i * BLOCK_MS);
        const uint32_t cost = (uint32_t)((uint64_t)BUDGET * load_pct / 100u);
        r.blocks++;
        if (mguard_begin(g, BUDGET, now)) {
            mguard_end(g, cost);
            r.ran++;
            r.used += cost;
        }
        if (g->state != MGUARD_RUN && !r.first_trip_block)
            r.first_trip_block = i + 1;
    }
    r.last_state = g->state;
    return r;
}

int main(void)
{
    {   /* light: every block runs */
        mguard_t g = {0};
        run_t r = steady(&g, 40, 10.0);
        CHECK(r.ran == r.blocks, "40%%: %u of %u blocks ran", r.ran, r.blocks);
        CHECK(g.state == MGUARD_RUN && g.trips == 0, "40%%: state %u trips %u", g.state, g.trips);
    }
    {   /* 75 %: under the share, every block runs */
        mguard_t g = {0};
        run_t r = steady(&g, 75, 10.0);
        CHECK(r.ran == r.blocks, "75%%: %u of %u blocks ran", r.ran, r.blocks);
    }
    {   /* 100 %: about a fifth skipped, the machine held near its share, no trip */
        mguard_t g = {0};
        run_t r = steady(&g, 100, 10.0);
        const double share = (double)r.used / ((double)r.blocks * BUDGET);
        CHECK(g.state == MGUARD_RUN && g.trips == 0, "100%%: state %u trips %u", g.state, g.trips);
        CHECK(share <= 0.81 && share >= 0.78, "100%%: the machine used %.3f of the time", share);
    }
    {   /* 0.1.0's 228 %: trips within a second and a half, tries again after 5 s, off at the third */
        mguard_t g = {0};
        run_t r = steady(&g, 228, 1.5);
        CHECK(r.first_trip_block && r.first_trip_block <= MGUARD_WINDOW, "228%%: tripped at block %u",
              r.first_trip_block);
        CHECK(g.state == MGUARD_TRIPPED && g.trips == 1, "228%%: state %u trips %u", g.state, g.trips);
        {   /* while tripped: nothing runs */
            const uint32_t at = (uint32_t)(r.blocks * BLOCK_MS);
            CHECK(!mguard_begin(&g, BUDGET, at + 100u), "228%%: ran while tripped");
            CHECK(mguard_text(&g) != 0, "228%%: no text while tripped");
        }
    }
    {   /* the whole story at 228 %: three trips in under 20 s, then off for good */
        mguard_t g = {0};
        run_t r = steady(&g, 228, 30.0);
        const double share = (double)r.used / ((double)r.blocks * BUDGET);
        CHECK(g.state == MGUARD_STOPPED && g.trips == MGUARD_MAX_TRIPS, "228%%x30s: state %u trips %u", g.state,
              g.trips);
        CHECK(share < 0.2, "228%%x30s: the machine used %.3f of the time", share);
    }
    {   /* a resume is flagged, once */
        mguard_t g = {0};
        (void)steady(&g, 228, 1.5);
        g.resumed = 0;
        CHECK(!mguard_begin(&g, BUDGET, g.retry_at - 1u), "ran before the retry");
        (void)mguard_begin(&g, BUDGET, g.retry_at);
        CHECK(g.state == MGUARD_RUN && g.resumed == 1, "retry: state %u resumed %u", g.state, g.resumed);
    }
    {   /* impossible (400 %, past the cap): never runs, trips */
        mguard_t g = {0};
        run_t r = steady(&g, 400, 2.0);
        CHECK(r.ran <= 1, "400%%: %u blocks ran", r.ran);
        CHECK(g.trips >= 1, "400%%: no trip");
    }
    {   /* a load that drops back: no trip, all blocks run again */
        mguard_t g = {0};
        run_t a = steady(&g, 130, 0.5), b;
        b = steady(&g, 50, 2.0);
        CHECK(g.trips == 0, "130%% then 50%%: %u trips", g.trips);
        CHECK(b.ran + 2u >= b.blocks, "50%% after 130%%: %u of %u ran", b.ran, b.blocks);   /* (one skip as it settles) */
        (void)a;
    }
    printf(fails ? "GUARD FAILED (%d)\n" : "GUARD OK\n", fails);
    return fails != 0;
}
