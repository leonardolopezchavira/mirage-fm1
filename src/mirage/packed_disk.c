/* SPDX-License-Identifier: MIT
 * The packed disk's decoder (packed_disk.h has the format). */
#include "packed_disk.h"
#include "mirage_math.h"

#define BLOCK 256

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

typedef struct {
    const uint8_t *p, *end;
    uint32_t acc;   /* bits not yet used, at the top */
    int n;          /* how many */
} bitrd_t;

static inline uint32_t bit1(bitrd_t *b)
{
    uint32_t v;
    if (!b->n) {
        b->acc = b->p < b->end ? (uint32_t)*b->p++ << 24 : 0;
        b->n = 8;
    }
    v = b->acc >> 31;
    b->acc <<= 1;
    b->n--;
    return v;
}

static inline uint32_t bits(bitrd_t *b, int n)
{
    uint32_t v = 0;
    while (n--) v = (v << 1) | bit1(b);
    return v;
}

int packed_disk_track(const uint8_t *pack, uint32_t pack_bytes, int t, uint8_t *out)
{
    uint32_t o0, o1;
    bitrd_t b;
    int blk, i, p1 = 0x80, p2 = 0x80;
    if (!pack || pack_bytes < PACKED_HEADER_BYTES || !m_same(pack, "MRD1", 4) || t < 0 || t >= MIRAGE_TRACKS)
        return -1;
    o0 = le32(pack + 4 + 4 * (uint32_t)t);
    o1 = le32(pack + 8 + 4 * (uint32_t)t);
    if (o0 > o1 || PACKED_HEADER_BYTES + o1 > pack_bytes) return -1;
    b.p = pack + PACKED_HEADER_BYTES + o0;
    b.end = pack + PACKED_HEADER_BYTES + o1;
    b.n = 0;
    b.acc = 0;
    for (blk = 0; blk < MIRAGE_TRACK_BYTES / BLOCK; ++blk) {
        const uint32_t h = bits(&b, 8);
        const int k = (int)(h & 7), mode = (int)((h >> 4) & 3);
        for (i = 0; i < BLOCK; ++i) {
            int x;
            if (h & 0x80) {
                x = (int)bits(&b, 8);
            } else {
                uint32_t q = 0, v;
                int r, pred;
                while (!bit1(&b))
                    if (++q > 255u) return -1;   /* a corrupt stream (or one read past its end: zeros) */
                v = (q << k) | bits(&b, k);
                r = (int)(v >> 1) ^ -(int)(v & 1u);
                pred = mode == 0 ? 0 : mode == 1 ? p1 : 2 * p1 - p2;
                x = (pred + r) & 0xFF;
            }
            *out++ = (uint8_t)x;
            p2 = p1;
            p1 = x;
        }
    }
    return 0;
}

static int packed_read(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)
{
    packed_disk_t *d = (packed_disk_t *)ctx;
    while (n) {
        const int t = (int)(off / MIRAGE_TRACK_BYTES);
        const uint32_t in = off % MIRAGE_TRACK_BYTES, k = MIRAGE_TRACK_BYTES - in < n ? MIRAGE_TRACK_BYTES - in : n;
        if (t != d->cached) {
            if (packed_disk_track(d->pack, d->pack_bytes, t, d->cache) != 0) { d->cached = -1; return 1; }
            d->cached = t;
        }
        m_copy(dst, d->cache + in, k);
        dst += k;
        off += k;
        n -= k;
    }
    return 0;
}

int packed_disk_init(packed_disk_t *d, const uint8_t *pack, uint32_t pack_bytes)
{
    if (!pack || pack_bytes < PACKED_HEADER_BYTES || !m_same(pack, "MRD1", 4)) return -1;
    d->pack = pack;
    d->pack_bytes = pack_bytes;
    d->cached = -1;
    d->disk.read = packed_read;
    d->disk.write = 0;
    d->disk.ctx = d;
    return 0;
}
