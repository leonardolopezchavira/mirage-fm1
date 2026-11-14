/* SPDX-License-Identifier: MIT
 * The Mirage, headless, for testing (after mirage-remake's mirage_cli).
 *   mirage_cli rom.bin [commands...]
 *     disk FILE.img        insert a disk image (written back to FILE.img.out when the Mirage saves)
 *     eject                eject it
 *     boot                 power-on until the OS is loaded and the drive is off
 *     run SECONDS          run the machine
 *     key NAME             press a front-panel key for 100 ms (then 200 ms released)
 *     midi HEX,HEX,...     MIDI bytes in
 *     wheels PITCH MOD     the wheels' A/D values (hex)
 *     wav FILE SECONDS     render through the engine to a 16-bit WAV at 44.1 kHz
 *     show                 the display and where the CPU is
 *     peek HEXADDR N       main RAM ($8000-$DFFF)
 *     snapshot FILE        the machine's state (mirage_snapshot_save) and the sample RAM after it */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mirage.h"
#include "mirage_engine.h"
#include "disk_image.h"

static const struct { const char *name; int key; } kKeys[] = {
    {"LOADUPPER", MIRAGE_LOAD_UPPER}, {"LOADLOWER", MIRAGE_LOAD_LOWER}, {"SAMPLEUPPER", MIRAGE_SAMPLE_UPPER},
    {"PLAYSEQ", MIRAGE_PLAY_SEQ}, {"LOADSEQ", MIRAGE_LOAD_SEQ}, {"SAVESEQ", MIRAGE_SAVE_SEQ},
    {"RECSEQ", MIRAGE_REC_SEQ}, {"SAMPLELOWER", MIRAGE_SAMPLE_LOWER}, {"0", MIRAGE_K0_PROG}, {"1", MIRAGE_K1},
    {"2", MIRAGE_K2}, {"3", MIRAGE_K3}, {"4", MIRAGE_K4}, {"5", MIRAGE_K5}, {"6", MIRAGE_K6}, {"7", MIRAGE_K7},
    {"8", MIRAGE_K8}, {"9", MIRAGE_K9}, {"ENTER", MIRAGE_ENTER}, {"UP", MIRAGE_UP}, {"DOWN", MIRAGE_DOWN},
    {"PARAM", MIRAGE_PARAM}, {"VALUE", MIRAGE_VALUE}, {"CANCEL", MIRAGE_CANCEL},
};

static char seg_char(uint8_t s)
{
    static const struct { uint8_t s; char c; } map[] = {
        {0x3F, '0'}, {0x06, '1'}, {0x5B, '2'}, {0x4F, '3'}, {0x66, '4'}, {0x6D, '5'}, {0x7D, '6'}, {0x07, '7'},
        {0x7F, '8'}, {0x6F, '9'}, {0x77, 'A'}, {0x7C, 'b'}, {0x39, 'C'}, {0x5E, 'd'}, {0x79, 'E'}, {0x71, 'F'},
        {0x76, 'H'}, {0x38, 'L'}, {0x54, 'n'}, {0x5C, 'o'}, {0x73, 'P'}, {0x50, 'r'}, {0x3E, 'U'}, {0x1C, 'u'},
        {0x40, '-'}, {0x00, ' '}, {0x78, 't'}, {0x74, 'h'}, {0x58, 'c'}, {0x1E, 'J'}, {0x6E, 'y'}, {0x10, 'i'},
        {0x37, 'N'}, {0x3D, 'G'}, {0x08, '_'},
    };
    size_t i;
    for (i = 0; i < sizeof map / sizeof map[0]; ++i)
        if (map[i].s == (s & 0x7F)) return map[i].c;
    return '?';
}

static void show(mirage_t *m)
{
    const uint8_t l = mirage_display(m, 0), r = mirage_display(m, 1);
    printf("[%c%s%c%s]  (segs %02X %02X)  t=%.3fs pc=%04X\n", seg_char(l), (l & 0x80) ? "." : "", seg_char(r),
           (r & 0x80) ? "." : "", l, r, (double)mirage_cycles(m) / MIRAGE_CPU_HZ, mirage_pc(m));
}

static void write_wav(const char *path, const float *s, long n, int rate)
{
    FILE *f = fopen(path, "wb");
    uint32_t u;
    uint16_t w;
    long i;
    if (!f) return;
#define U32(v) (u = (uint32_t)(v), fwrite(&u, 4, 1, f))
#define U16(v) (w = (uint16_t)(v), fwrite(&w, 2, 1, f))
    fwrite("RIFF", 1, 4, f); U32(36 + n * 2); fwrite("WAVEfmt ", 1, 8, f);
    U32(16); U16(1); U16(1); U32(rate); U32(rate * 2); U16(2); U16(16);
    fwrite("data", 1, 4, f); U32(n * 2);
    for (i = 0; i < n; ++i) {
        float v = s[i] < -1.0f ? -1.0f : s[i] > 1.0f ? 1.0f : s[i];
        int16_t x = (int16_t)(v * 32767.0f);
        fwrite(&x, 2, 1, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    static mirage_engine_t eng;
    uint8_t *rom, *img = 0, *sram = (uint8_t *)malloc(MIRAGE_SRAM_BYTES);
    void *mem = malloc(mirage_sizeof());
    disk_image_t disk;
    const char *disk_path = 0;
    mirage_t *m;
    int i;

    if (argc < 2 || read_file(argv[1], &rom) != MIRAGE_ROM_BYTES) {
        fprintf(stderr, "usage: %s rom.bin [commands]\n", argv[0]);
        return 1;
    }
    m = mirage_init(mem, rom, sram);
    mirage_reset(m);
    mirage_engine_init(&eng, m, 44100.0f);

    for (i = 2; i < argc; ++i) {
        const char *c = argv[i];
#define NEXT() (i + 1 < argc ? argv[++i] : "")
        if (!strcmp(c, "disk")) {
            free(img);
            disk_path = NEXT();
            if (read_file(disk_path, &img) != MIRAGE_DISK_BYTES) { printf("disk: bad image %s\n", disk_path); continue; }
            disk_image_init(&disk, img);
            mirage_insert_disk(m, &disk.disk);
        } else if (!strcmp(c, "eject")) {
            mirage_insert_disk(m, 0);
        } else if (!strcmp(c, "boot")) {
            printf("booted in %.2f s of machine time\n", mirage_engine_boot(&eng, 30.0f));
        } else if (!strcmp(c, "run")) {
            mirage_run(m, (uint32_t)(atof(NEXT()) * MIRAGE_CPU_HZ));
        } else if (!strcmp(c, "key")) {
            const char *k = NEXT();
            size_t j;
            for (j = 0; j < sizeof kKeys / sizeof kKeys[0] && strcmp(kKeys[j].name, k); ++j) {}
            if (j == sizeof kKeys / sizeof kKeys[0]) { printf("unknown key %s\n", k); continue; }
            mirage_set_key(m, kKeys[j].key, 1);
            mirage_run(m, MIRAGE_CPU_HZ / 10);
            mirage_set_key(m, kKeys[j].key, 0);
            mirage_run(m, MIRAGE_CPU_HZ / 5);
        } else if (!strcmp(c, "midi")) {
            char *s = NEXT(), *e;
            while (*s) {
                unsigned long b = strtoul(s, &e, 16);
                if (e == s) break;
                mirage_midi_in(m, (uint8_t)b);
                s = *e ? e + 1 : e;
            }
        } else if (!strcmp(c, "wheels")) {
            const uint8_t p = (uint8_t)strtoul(NEXT(), 0, 16);
            mirage_set_wheels(m, p, (uint8_t)strtoul(NEXT(), 0, 16));
        } else if (!strcmp(c, "wav")) {
            const char *p = NEXT();
            const long n = (long)(atof(NEXT()) * 44100.0);
            float *out = (float *)malloc(sizeof(float) * (size_t)(n + 256)), peak = 0;
            long k, j;
            for (k = 0; k < n; k += 256) mirage_engine_process(&eng, out + k, 256, 0);
            for (j = 0; j < n; ++j) peak = out[j] > peak ? out[j] : -out[j] > peak ? -out[j] : peak;
            write_wav(p, out, n, 44100);
            printf("wrote %s (%ld samples, peak %.3f)\n", p, n, peak);
            free(out);
        } else if (!strcmp(c, "show")) {
            show(m);
        } else if (!strcmp(c, "peek")) {
            const unsigned a = (unsigned)strtoul(NEXT(), 0, 16);
            const int n = atoi(NEXT());
            int k;
            for (k = 0; k < n; ++k) {
                const unsigned x = a + (unsigned)k;
                printf("%02X%s", x >= 0x8000 && x < 0xE000 ? mirage_main_ram(m)[x - 0x8000] : 0xFF,
                       (k % 16 == 15) ? "\n" : " ");
            }
            printf("\n");
        } else if (!strcmp(c, "snapshot")) {
            const char *p = NEXT();
            FILE *f = fopen(p, "wb");
            void *s = malloc(mirage_sizeof());
            mirage_snapshot_save(m, s);
            if (f) {
                fwrite(s, 1, mirage_sizeof(), f);
                fwrite(mirage_sample_ram(m), 1, MIRAGE_SRAM_BYTES, f);
                fclose(f);
            }
            free(s);
            printf("snapshot %s: %zu + %u bytes\n", p, mirage_sizeof(), MIRAGE_SRAM_BYTES);
        } else {
            printf("unknown command %s\n", c);
        }
    }
    if (img && disk_path && mirage_disk_dirty(m)) {
        char out[1024];
        FILE *f;
        snprintf(out, sizeof out, "%s.out", disk_path);
        if ((f = fopen(out, "wb"))) {
            fwrite(img, 1, MIRAGE_DISK_BYTES, f);
            fclose(f);
            printf("the Mirage wrote to the disk: saved as %s\n", out);
        }
    }
    return 0;
}
