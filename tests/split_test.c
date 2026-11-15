/* SPDX-License-Identifier: MIT
 * Two cores against one: two machines boot OS 3.2 and are played the same way (chords over MIDI, a parameter edited
 * on the keypad, the wheels); one renders on one core, the other hands the analog section to a second core (a real
 * thread here, as the FM-1's second core). The outputs must be the same, sample for sample: the record the first
 * core makes and the second plays back is the same arithmetic in the same order, and nothing is shared between them
 * while they run (a race would show here as a difference). Then sampling: the two-core machine goes back to one core
 * while the OS samples, and comes back to two after.
 *   mirage_split_test rom.bin os32.img */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mirage.h"
#include "mirage_engine.h"
#include "../tools/disk_image.h"

#define FS 44117.647f
#define BLOCK 256

/* the second core: a thread that runs mirage_engine_job when started */
typedef struct {
    pthread_t t;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int go, done, quit;
    mirage_engine_t *e;
} core2_t;

static void *core2_main(void *arg)
{
    core2_t *c = (core2_t *)arg;
    pthread_mutex_lock(&c->mu);
    for (;;) {
        while (!c->go && !c->quit) pthread_cond_wait(&c->cv, &c->mu);
        if (c->quit) break;
        c->go = 0;
        pthread_mutex_unlock(&c->mu);
        mirage_engine_job(c->e);
        pthread_mutex_lock(&c->mu);
        c->done = 1;
        pthread_cond_broadcast(&c->cv);
    }
    pthread_mutex_unlock(&c->mu);
    return 0;
}

static int c2_start(void *ctx)
{
    core2_t *c = (core2_t *)ctx;
    pthread_mutex_lock(&c->mu);
    c->done = 0;
    c->go = 1;
    pthread_cond_broadcast(&c->cv);
    pthread_mutex_unlock(&c->mu);
    return 1;
}

static int c2_wait(void *ctx)
{
    core2_t *c = (core2_t *)ctx;
    pthread_mutex_lock(&c->mu);
    while (!c->done) pthread_cond_wait(&c->cv, &c->mu);
    pthread_mutex_unlock(&c->mu);
    return 0;
}

static mirage_engine_t ea, eb;
static long blocks, diffs, split_blocks;
static double phase;

static void both(int n, int with_tone)
{
    static float a[BLOCK], b[BLOCK], in[BLOCK];
    int k, i;
    for (k = 0; k < n; ++k) {
        if (with_tone)
            for (i = 0; i < BLOCK; ++i) {
                in[i] = 0.5f * (float)__builtin_sin(phase);
                phase += 2.0 * 3.14159265358979 * 440.0 / FS;
            }
        mirage_engine_process(&ea, a, BLOCK, with_tone ? in : 0);
        mirage_engine_process(&eb, b, BLOCK, with_tone ? in : 0);
        for (i = 0; i < BLOCK; ++i)
            if (a[i] != b[i]) {
                if (diffs < 3) printf("block %ld sample %d: %.9g / %.9g\n", blocks, i, a[i], b[i]);
                diffs++;
            }
        split_blocks += eb.split;
        blocks++;
    }
}

static void midi(const uint8_t *p, int n)
{
    int i;
    for (i = 0; i < n; ++i) {
        mirage_midi_in(ea.m, p[i]);
        mirage_midi_in(eb.m, p[i]);
    }
}

static void key(int k)
{
    mirage_set_key(ea.m, k, 1);
    mirage_set_key(eb.m, k, 1);
    both(17, 0);
    mirage_set_key(ea.m, k, 0);
    mirage_set_key(eb.m, k, 0);
    both(34, 0);
}

int main(int argc, char **argv)
{
    static const uint8_t on[] = {0x90, 41, 100, 0x90, 45, 90, 0x90, 48, 110, 0x90, 52, 80, 0x90, 55, 100, 0x90, 60, 127,
                                 0x90, 64, 70, 0x90, 67, 100};
    static const uint8_t off[] = {0x80, 41, 0, 0x80, 45, 0, 0x80, 48, 0, 0x80, 52, 0, 0x80, 55, 0, 0x80, 60, 0,
                                  0x80, 64, 0, 0x80, 67, 0};
    static const uint8_t note[] = {0x90, 57, 100}, note_off[] = {0x80, 57, 0};
    static core2_t c2;
    static mirage_core2_t hooks;
    uint8_t *rom, *img_a, *img_b;
    disk_image_t da, db;
    mirage_t *a, *b;
    long split_before;
    int ok;

    if (argc < 3 || read_file(argv[1], &rom) != MIRAGE_ROM_BYTES || read_file(argv[2], &img_a) != MIRAGE_DISK_BYTES
        || read_file(argv[2], &img_b) != MIRAGE_DISK_BYTES) {
        fprintf(stderr, "usage: %s rom.bin os32.img\n", argv[0]);
        return 2;
    }
    a = mirage_init(malloc(mirage_sizeof()), rom, (uint8_t *)malloc(MIRAGE_SRAM_BYTES));
    b = mirage_init(malloc(mirage_sizeof()), rom, (uint8_t *)malloc(MIRAGE_SRAM_BYTES));
    disk_image_init(&da, img_a);
    disk_image_init(&db, img_b);
    mirage_reset(a);
    mirage_reset(b);
    mirage_insert_disk(a, &da.disk);
    mirage_insert_disk(b, &db.disk);
    mirage_engine_init(&ea, a, FS);
    mirage_engine_init(&eb, b, FS);
    ea.disk_turbo = eb.disk_turbo = 1;   /* as on the FM-1 */

    c2.e = &eb;
    pthread_mutex_init(&c2.mu, 0);
    pthread_cond_init(&c2.cv, 0);
    pthread_create(&c2.t, 0, core2_main, &c2);
    hooks.start = c2_start;
    hooks.wait = c2_wait;
    hooks.ctx = &c2;
    mirage_engine_set_core2(&eb, &hooks);

    both((int)(10.0f * FS / BLOCK), 0);   /* power-on in real time: the filters tuned, OS 3.2 loaded */
    printf("power-on: display %02X %02X; two cores from block %ld\n", mirage_display(b, 0), mirage_display(b, 1),
           blocks - split_blocks);
    midi(on, sizeof on);
    both(400, 0);
    midi(off, sizeof off);
    both(200, 0);
    key(MIRAGE_PARAM); key(MIRAGE_K3); key(MIRAGE_K8); key(MIRAGE_VALUE); key(MIRAGE_DOWN); key(MIRAGE_DOWN);
    midi(on, sizeof on);
    mirage_set_wheels(a, 0xC0, 0x40);
    mirage_set_wheels(b, 0xC0, 0x40);
    both(400, 0);
    midi(off, sizeof off);
    both(200, 0);
    split_before = split_blocks;
    printf("played: %ld blocks, %ld on two cores; %u records played on the second core, %u on the first; "
           "stale A/D reads %u\n", blocks, split_blocks, eb.jobs - eb.jobs_here, eb.jobs_here, mirage_adc_stale(b));

    /* sampling: one core while the OS listens (SAMPLE LOWER, ENTER, a tone), then two again */
    key(MIRAGE_SAMPLE_LOWER);
    both(700, 0);
    key(MIRAGE_ENTER);
    both(700, 1);
    both(400, 0);
    midi(note, sizeof note);
    both(300, 0);
    midi(note_off, sizeof note_off);
    both(300, 0);
    printf("sampled: display %02X %02X; two cores again after: %s\n", mirage_display(b, 0), mirage_display(b, 1),
           eb.split ? "yes" : "no");

    pthread_mutex_lock(&c2.mu);
    c2.quit = 1;
    pthread_cond_broadcast(&c2.cv);
    pthread_mutex_unlock(&c2.mu);
    pthread_join(c2.t, 0);

    ok = diffs == 0 && split_before > 1000 && eb.split && mirage_display(b, 0) == 0x6D && mirage_display(b, 1) == 0x71;
    printf("%ld samples differ\n%s\n", diffs, ok ? "SPLIT OK" : "SPLIT FAIL");
    return ok ? 0 : 1;
}
