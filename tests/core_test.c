/* SPDX-License-Identifier: MIT
 * The C core as the FM-1 runs it (single precision), checked as a player would hear it, after mirage-remake's
 * engine_check: power-on from the OS 3.2 disk, a chord that sounds and stops, every filter tuned, a sample recorded
 * from the audio input that plays back at pitch, and the speed against real time.
 *   mirage_core_test rom.bin os32.img */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mirage.h"
#include "mirage_engine.h"
#include "../tools/disk_image.h"

#define FS 44117.647f   /* the FM-1's audio rate: 24 MHz / 544 */
#define BLOCK 256

static mirage_engine_t eng;
static float buf[BLOCK], in[BLOCK];
static double ph;
static int sig;

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static void blocks(int n, float *peak, float *rec, int *nrec)
{
    int b, i;
    for (b = 0; b < n; ++b) {
        for (i = 0; i < BLOCK; ++i) {
            in[i] = sig ? 0.5f * (float)sin(ph) : 0.0f;
            ph += 2.0 * 3.14159265358979 * 440.0 / FS;
        }
        mirage_engine_process(&eng, buf, BLOCK, in);
        for (i = 0; i < BLOCK; ++i) {
            if (peak && fabsf(buf[i]) > *peak) *peak = fabsf(buf[i]);
            if (rec) rec[(*nrec)++] = buf[i];
        }
    }
}

static void key(int k)
{
    mirage_set_key(eng.m, k, 1);
    blocks(17, 0, 0, 0);
    mirage_set_key(eng.m, k, 0);
    blocks(34, 0, 0, 0);
}

static void midi3(uint8_t a, uint8_t b, uint8_t c)
{
    mirage_midi_in(eng.m, a);
    mirage_midi_in(eng.m, b);
    mirage_midi_in(eng.m, c);
}

int main(int argc, char **argv)
{
    static const uint8_t chord[] = {48, 52, 55, 60};
    uint8_t *rom, *img, *sram = (uint8_t *)malloc(MIRAGE_SRAM_BYTES);
    void *mem = malloc(mirage_sizeof());
    disk_image_t disk;
    mirage_t *m;
    float quiet = 0, loud = 0, tail = 0, *rec;
    double t0, t1, rt, hz;
    int i, nrec = 0, zc = 0, level_detect, finished, ok;
    float boot_s;

    if (argc < 3 || read_file(argv[1], &rom) != MIRAGE_ROM_BYTES || read_file(argv[2], &img) != MIRAGE_DISK_BYTES) {
        fprintf(stderr, "usage: %s rom.bin os32.img\n", argv[0]);
        return 2;
    }
    m = mirage_init(mem, rom, sram);
    disk_image_init(&disk, img);
    mirage_reset(m);
    mirage_insert_disk(m, &disk.disk);
    mirage_engine_init(&eng, m, FS);

    t0 = now();
    boot_s = mirage_engine_boot(&eng, 30.0f);
    t1 = now();
    printf("boot: %.2f s wall, %.2f s of machine time, display %02X %02X\n", t1 - t0, boot_s, mirage_display(m, 0),
           mirage_display(m, 1));

    blocks(100, &quiet, 0, 0);
    for (i = 0; i < 4; ++i) midi3(0x90, chord[i], 100);
    t0 = now();
    blocks(345, &loud, 0, 0);   /* 2 s */
    t1 = now();
    for (i = 0; i < 4; ++i) midi3(0x80, chord[i], 64);
    blocks(345, &tail, 0, 0);
    rt = (345.0 * BLOCK / FS) / (t1 - t0);
    printf("silence peak %.4f, chord peak %.4f, after release %.4f, speed %.1fx real time\n", quiet, loud, tail, rt);

    /* sampling, as a player does it: SAMPLE LOWER, level-detect mode, ENTER, a 440 Hz tone; the display ends on
     * "SF", and the sample plays back at about 440.8 Hz (the OS plays a fresh sample ~3 cents sharp, as the
     * hardware does) on the E below middle C */
    key(MIRAGE_SAMPLE_LOWER);
    blocks(690, 0, 0, 0);
    level_detect = mirage_sampling(m);
    key(MIRAGE_ENTER);
    sig = 1;
    blocks(690, 0, 0, 0);
    sig = 0;
    blocks(345, 0, 0, 0);
    finished = mirage_display(m, 0) == 0x6D && mirage_display(m, 1) == 0x71;
    rec = (float *)malloc(sizeof(float) * 230 * BLOCK);
    midi3(0x90, 52, 100);
    blocks(230, 0, rec, &nrec);
    for (i = 8821; i < nrec; ++i)
        if ((rec[i - 1] < 0) != (rec[i] < 0)) zc++;
    hz = zc / 2.0 / ((nrec - 8821) / (double)FS);
    printf("sampling: level-detect %d, finished (SF) %d, playback %.1f Hz\n", level_detect, finished, hz);

    ok = quiet < 0.01f && loud > 0.05f && tail < loud * 0.5f && rt > 2.0 && level_detect && finished
         && fabs(hz - 440.8) < 2.0;
    printf("%s\n", ok ? "CORE OK" : "CORE FAIL");
    return ok ? 0 : 1;
}
