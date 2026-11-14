/* SPDX-License-Identifier: MIT
 * A Mirage disk packed by tools/pack_disk.py, read in the drive one track at a time: the FM-1 keeps the pack in
 * flash and one decoded track (5,632 bytes) in RAM.
 *
 * The pack: "MRD1", then 81 little-endian uint32 offsets (track t's bytes are [off[t], off[t+1]) after the header),
 * then the tracks. A track is 22 blocks of 256 bytes; each block is a header byte, then 256 values, most significant
 * bit first:
 *   header 0x80             the 256 bytes stored as they are (8 bits each)
 *   header (mode << 4) | k  each byte as a residual: q zeros, a one, k bits (v = q << k | bits), r = zigzag(v), and
 *                           byte = prediction + r (mod 256); the prediction is 0 (mode 0), the last byte (1), or
 *                           2 x last - the one before (2). The last two bytes start at 0x80 at each track.
 *
 * Writes go to an optional overlay (a callback): the pack itself is read-only. */
#pragma once
#include <stdint.h>
#include "mirage.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PACKED_HEADER_BYTES (4u + 4u * (MIRAGE_TRACKS + 1u))

/* decodes track t of the pack into out (MIRAGE_TRACK_BYTES); 0, or -1 for a bad pack or track */
int packed_disk_track(const uint8_t *pack, uint32_t pack_bytes, int t, uint8_t *out);

typedef struct {
    mirage_disk_t disk;          /* hand &this->disk to mirage_insert_disk */
    const uint8_t *pack;
    uint32_t pack_bytes;
    int cached;                  /* the track in cache, -1 none */
    uint8_t cache[MIRAGE_TRACK_BYTES];
} packed_disk_t;

/* 0, or -1 when pack is not a disk pack; the disk is read-only (write protect the drive) */
int packed_disk_init(packed_disk_t *d, const uint8_t *pack, uint32_t pack_bytes);

#ifdef __cplusplus
}
#endif
