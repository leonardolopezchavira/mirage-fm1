/* SPDX-License-Identifier: MIT
 * How much the machine costs, playing: boots OS 3.2, holds an 8-note chord (every voice busy) and renders it through
 * the engine at the FM-1's rate, timed; then the same with the analog section's parts left out, to see what each
 * costs. For a profile by function: valgrind --tool=callgrind --toggle-collect=play ./mirage_bench rom os32.img
 *   mirage_bench rom.bin os32.img [seconds [2]]   (2: the analog section on a second core, its share apart:
 *   valgrind --tool=callgrind --toggle-collect=play ... then core2_job's inclusive count is the second core's) */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "mirage.h"
#include "mirage_engine.h"
#include "disk_image.h"

#define FS 44117.647f
#define BLOCK 256

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static float sink;

/* two cores, measured apart: the "other core" runs its job right away, in its own function (core2_job) */
static mirage_engine_t *job_engine;
__attribute__((noinline)) static void core2_job(void) { mirage_engine_job(job_engine); }
static int c2_start(void *ctx) { (void)ctx; core2_job(); return 1; }
static int c2_wait(void *ctx) { (void)ctx; return 0; }
static const mirage_core2_t c2 = {c2_start, c2_wait, 0};

/* n blocks of output: the machine-time seconds per wall second */
__attribute__((noinline)) static double play(mirage_engine_t *e, int blocks)
{
    static float out[BLOCK];
    double t0 = now();
    int b, i;
    for (b = 0; b < blocks; ++b) {
        mirage_engine_process(e, out, BLOCK, 0);
        for (i = 0; i < BLOCK; ++i) sink += out[i];
    }
    return (blocks * (double)BLOCK / FS) / (now() - t0);
}

int main(int argc, char **argv)
{
    static mirage_engine_t eng;
    static const uint8_t chord[8] = {41, 45, 48, 52, 55, 60, 64, 67};
    uint8_t *rom, *img;
    disk_image_t disk;
    mirage_t *m;
    const double secs = argc > 3 ? atof(argv[3]) : 3.0;
    int i;
    double rt;
    if (argc < 3 || read_file(argv[1], &rom) != MIRAGE_ROM_BYTES || read_file(argv[2], &img) != MIRAGE_DISK_BYTES) {
        fprintf(stderr, "usage: %s rom.bin os32.img [seconds]\n", argv[0]);
        return 2;
    }
    m = mirage_init(malloc(mirage_sizeof()), rom, (uint8_t *)malloc(MIRAGE_SRAM_BYTES));
    disk_image_init(&disk, img);
    mirage_reset(m);
    mirage_insert_disk(m, &disk.disk);
    mirage_engine_init(&eng, m, FS);
    mirage_engine_boot(&eng, 30.0f);
    if (argc > 4 && argv[4][0] == '2') {
        job_engine = &eng;
        mirage_engine_set_core2(&eng, &c2);
    }
    for (i = 0; i < 8; ++i) {
        mirage_midi_in(m, 0x90);
        mirage_midi_in(m, chord[i]);
        mirage_midi_in(m, 100);
    }
    play(&eng, 40);   /* the notes start */
    rt = play(&eng, (int)(secs * FS / BLOCK));
    printf("8-note chord: %.1fx real time (%.1f %% of one core of this machine), oscillators %d, %s\n", rt,
           100.0 / rt, mirage_doc_oscillators(m), eng.split ? "two cores" : "one core");
    return sink == 12345.0f;
}
