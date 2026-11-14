/* SPDX-License-Identifier: MIT
 * The packed disk: every track of the pack decodes to the image it was made from, and the Mirage boots from it as
 * from the image (the same display, the same RAM).
 *   mirage_pack_test rom.bin os32.img os32.pack */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mirage.h"
#include "mirage_engine.h"
#include "packed_disk.h"
#include "../tools/disk_image.h"

int main(int argc, char **argv)
{
    static packed_disk_t pd;
    static mirage_engine_t ea, eb;
    static uint8_t track[MIRAGE_TRACK_BYTES];
    uint8_t *rom, *img, *pack;
    long pack_n;
    int t, bad = 0, same;
    disk_image_t di;
    mirage_t *a, *b;

    if (argc < 4 || read_file(argv[1], &rom) != MIRAGE_ROM_BYTES || read_file(argv[2], &img) != MIRAGE_DISK_BYTES
        || (pack_n = read_file(argv[3], &pack)) <= 0) {
        fprintf(stderr, "usage: %s rom.bin disk.img disk.pack\n", argv[0]);
        return 2;
    }
    for (t = 0; t < MIRAGE_TRACKS; ++t)
        if (packed_disk_track(pack, (uint32_t)pack_n, t, track) != 0
            || memcmp(track, img + t * MIRAGE_TRACK_BYTES, MIRAGE_TRACK_BYTES) != 0) {
            printf("track %d differs\n", t);
            bad++;
        }
    printf("pack: %ld bytes, %d of %d tracks decode to the image\n", pack_n, MIRAGE_TRACKS - bad, MIRAGE_TRACKS);
    if (packed_disk_track(pack, (uint32_t)pack_n - 100u, 79, track) == 0) { printf("a truncated pack was accepted\n"); bad++; }

    /* the same machine twice: one on the image, one on the pack */
    a = mirage_init(malloc(mirage_sizeof()), rom, (uint8_t *)malloc(MIRAGE_SRAM_BYTES));
    b = mirage_init(malloc(mirage_sizeof()), rom, (uint8_t *)malloc(MIRAGE_SRAM_BYTES));
    disk_image_init(&di, img);
    di.disk.write = 0;   /* both read-only */
    packed_disk_init(&pd, pack, (uint32_t)pack_n);
    mirage_reset(a);
    mirage_reset(b);
    mirage_insert_disk(a, &di.disk);
    mirage_insert_disk(b, &pd.disk);
    mirage_engine_init(&ea, a, 44100.0f);
    mirage_engine_init(&eb, b, 44100.0f);
    mirage_engine_boot(&ea, 30.0f);
    mirage_engine_boot(&eb, 30.0f);
    same = mirage_cycles(a) == mirage_cycles(b) && !memcmp(mirage_main_ram(a), mirage_main_ram(b), MIRAGE_RAM_BYTES)
           && !memcmp(mirage_sample_ram(a), mirage_sample_ram(b), MIRAGE_SRAM_BYTES);
    printf("boot from the pack: display %02X %02X, %s the boot from the image\n", mirage_display(b, 0),
           mirage_display(b, 1), same ? "identical to" : "DIFFERENT from");
    printf("%s\n", !bad && same ? "PACK OK" : "PACK FAIL");
    return !bad && same ? 0 : 1;
}
