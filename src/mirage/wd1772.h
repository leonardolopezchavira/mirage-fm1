/* SPDX-License-Identifier: MIT
 * WD1772 floppy controller with a Mirage-format disk: 80 tracks, single sided, sectors 0-4 of 1024 bytes and
 * sector 5 of 512 bytes (5,632 bytes a track). A C port of mirage-remake's Wd1772 (MIT).
 * The disk is not held here: it is reached through mirage_disk_t (mirage.h), so it can be a RAM image on the
 * desktop, a virtual disk in flash on the FM-1, or a drive served by a web page over USB. */
#pragma once
#include <stdint.h>
#include <string.h>
#include "mirage.h"

enum { ST_BUSY = 0x01, ST_DRQ = 0x02, ST_TR00 = 0x04, ST_RNF = 0x10, ST_SPINUP = 0x20, ST_WP = 0x40, ST_MOTOR = 0x80 };
enum { FDC_NONE, FDC_SEEK, FDC_READ, FDC_WRITE, FDC_READADDR, FDC_READTRACK, FDC_WRITETRACK };
#define FDC_WAIT (0x7FFFFFFF / 2)

typedef struct {
    const mirage_disk_t *disk;
    uint8_t status, track, sector, data, cmd;
    int head;
    uint8_t intrq, drq, motor, dirty, type1, write_protect;
    int op, delay, pos, len;
    uint32_t track_off;   /* byte offset of the current transfer in the image */
    int byte_cycles;      /* cycles between a byte being consumed and the next DRQ */
    uint8_t buf[6400];    /* a sector, a track (5,632) or a format stream (6,250) */
} wd1772_t;

static inline int fdc_has_disk(const wd1772_t *f) { return f->disk != 0; }
static int fdc_sector_offset(int s) { return s < 5 ? s * 1024 : 5 * 1024; }
static int fdc_sector_len(int s) { return s < 5 ? 1024 : 512; }

static void fdc_init(wd1772_t *f)
{
    memset(f, 0, sizeof *f);
    f->byte_cycles = 32;
    f->type1 = 1;
}

static void fdc_reset(wd1772_t *f)
{
    f->status = 0; f->track = 0; f->sector = 0; f->data = 0; f->cmd = 0;
    f->intrq = f->drq = 0;
    f->op = FDC_NONE;
    f->type1 = 1;
}

static void fdc_disk_read(wd1772_t *f, uint32_t off, uint8_t *dst, uint32_t n)
{
    if (!f->disk || f->disk->read(f->disk->ctx, off, dst, n) != 0) memset(dst, 0, n);
}

static void fdc_disk_write(wd1772_t *f, uint32_t off, const uint8_t *src, uint32_t n)
{
    if (f->disk && f->disk->write) f->disk->write(f->disk->ctx, off, src, n);
    f->dirty = 1;
}

static uint8_t fdc_type1_status(const wd1772_t *f)
{
    uint8_t s = ST_SPINUP;
    if (f->motor) s |= ST_MOTOR;
    if (f->head == 0) s |= ST_TR00;
    if (f->write_protect && fdc_has_disk(f)) s |= ST_WP;
    return s;
}

static void fdc_finish(wd1772_t *f, uint8_t extra)
{
    f->op = FDC_NONE;
    f->drq = 0;
    f->status = (uint8_t)((f->type1 ? fdc_type1_status(f) : (f->motor ? ST_MOTOR : 0)) | extra);
    f->intrq = 1;
}

static uint8_t fdc_read(wd1772_t *f, int reg)
{
    uint8_t s;
    switch (reg & 3) {
    case 0:
        f->intrq = 0;
        s = f->status;
        if (f->op == FDC_NONE && f->type1) s = (uint8_t)(fdc_type1_status(f) | (f->status & 0x18));
        if (f->drq) s |= ST_DRQ;
        if (f->op != FDC_NONE) s |= ST_BUSY;
        return s;
    case 1: return f->track;
    case 2: return f->sector;
    default:
        if (f->drq && (f->op == FDC_READ || f->op == FDC_READADDR || f->op == FDC_READTRACK)) {
            f->drq = 0;
            f->delay = f->pos >= f->len ? 40 : f->byte_cycles;
        }
        return f->data;
    }
}

static int fdc_locate(const wd1772_t *f)
{
    return fdc_has_disk(f) && f->head < MIRAGE_TRACKS && f->sector <= 5 && f->track == f->head;
}

static void fdc_not_found(wd1772_t *f)   /* completes later with RNF */
{
    f->op = FDC_SEEK;
    f->delay = 20000;
    f->status = ST_BUSY;
    f->cmd = 0xFF;
}

static void fdc_write(wd1772_t *f, int reg, uint8_t v)
{
    int hi, steps, dir, diff;
    switch (reg & 3) {
    case 1: f->track = v; return;
    case 2: f->sector = v; return;
    case 3:
        f->data = v;
        if (f->drq && (f->op == FDC_WRITE || f->op == FDC_WRITETRACK)) {
            f->drq = 0;
            f->buf[f->pos++] = v;
            f->delay = f->pos >= f->len ? 40 : f->byte_cycles;
        }
        return;
    default: break;
    }

    f->intrq = 0;   /* a command */
    if ((v & 0xF0) == 0xD0) {   /* force interrupt */
        f->op = FDC_NONE;
        f->drq = 0;
        f->type1 = 1;
        f->status = fdc_type1_status(f);
        if (v & 0x08) f->intrq = 1;
        return;
    }
    if (f->op != FDC_NONE) return;
    f->cmd = v;
    f->drq = 0;
    hi = v >> 4;

    if (!(v & 0x80)) {   /* type I */
        f->type1 = 1;
        steps = 0;
        if (hi == 0x0) { steps = f->head; f->head = 0; f->track = 0; }
        else if (hi == 0x1) {
            diff = (int)f->data - (int)f->track;
            f->head += diff;
            if (f->head < 0) f->head = 0;
            if (f->head > 83) f->head = 83;
            steps = diff < 0 ? -diff : diff;
            f->track = f->data;
        } else {
            if (hi == 0x4 || hi == 0x5) dir = 1;
            else if (hi == 0x6 || hi == 0x7) dir = -1;
            else dir = 1;   /* STEP: the last direction, taken as inward */
            f->head += dir;
            if (f->head < 0) f->head = 0;
            if (f->head > 83) f->head = 83;
            if (v & 0x10) f->track = (uint8_t)(f->track + dir);
            steps = 1;
        }
        f->op = FDC_SEEK;
        f->delay = 2000 + 200 * steps;
        f->status = (uint8_t)(fdc_type1_status(f) | ST_BUSY);
        return;
    }

    f->type1 = 0;
    f->status = (uint8_t)(ST_BUSY | (f->motor ? ST_MOTOR : 0));
    if (hi == 0x8 || hi == 0x9) {   /* read sector */
        if (!fdc_locate(f)) { fdc_not_found(f); return; }
        f->track_off = (uint32_t)f->head * MIRAGE_TRACK_BYTES + (uint32_t)fdc_sector_offset(f->sector);
        f->len = fdc_sector_len(f->sector);
        fdc_disk_read(f, f->track_off, f->buf, (uint32_t)f->len);
        f->pos = 0;
        f->op = FDC_READ;
        f->delay = 1000;
    } else if (hi == 0xA || hi == 0xB) {   /* write sector */
        if (f->write_protect && fdc_has_disk(f)) { fdc_finish(f, ST_WP); return; }
        if (!fdc_locate(f)) { fdc_not_found(f); return; }
        f->track_off = (uint32_t)f->head * MIRAGE_TRACK_BYTES + (uint32_t)fdc_sector_offset(f->sector);
        f->len = fdc_sector_len(f->sector);
        f->pos = 0;
        f->op = FDC_WRITE;
        f->delay = 500;
    } else if (hi == 0xC) {   /* read address */
        if (!fdc_has_disk(f)) { f->op = FDC_SEEK; f->delay = 20000; f->cmd = 0xFF; return; }
        f->buf[0] = (uint8_t)f->head; f->buf[1] = 0; f->buf[2] = 0; f->buf[3] = 3; f->buf[4] = 0; f->buf[5] = 0;
        f->len = 6; f->pos = 0;
        f->op = FDC_READADDR;
        f->delay = 500;
    } else if (hi == 0xE) {   /* read track (data only, simplified) */
        if (!fdc_has_disk(f) || f->head >= MIRAGE_TRACKS) { f->op = FDC_SEEK; f->delay = 20000; f->cmd = 0xFF; return; }
        f->len = MIRAGE_TRACK_BYTES; f->pos = 0;
        fdc_disk_read(f, (uint32_t)f->head * MIRAGE_TRACK_BYTES, f->buf, (uint32_t)f->len);
        f->op = FDC_READTRACK;
        f->delay = 1000;
    } else if (hi == 0xF) {   /* write track (format) */
        if (f->write_protect && fdc_has_disk(f)) { fdc_finish(f, ST_WP); return; }
        f->len = 6250; f->pos = 0;
        f->op = FDC_WRITETRACK;
        f->delay = 1000;
    }
}

static void fdc_tick(wd1772_t *f, int cycles)
{
    int i, sec, n;
    if (f->op == FDC_NONE || f->drq) return;
    f->delay -= cycles;
    if (f->delay > 0) return;

    switch (f->op) {
    case FDC_SEEK:
        fdc_finish(f, f->cmd == 0xFF ? ST_RNF : 0);
        break;
    case FDC_READ: case FDC_READADDR: case FDC_READTRACK:
        if (f->pos >= f->len) {
            if (f->op == FDC_READADDR) f->sector = f->buf[0];
            fdc_finish(f, 0);
            break;
        }
        f->data = f->buf[f->pos++];
        f->drq = 1;
        f->delay = FDC_WAIT;
        break;
    case FDC_WRITE:
        if (f->pos >= f->len) {
            if (fdc_has_disk(f)) fdc_disk_write(f, f->track_off, f->buf, (uint32_t)f->len);
            fdc_finish(f, 0);
            break;
        }
        f->drq = 1;
        f->delay = FDC_WAIT;
        break;
    case FDC_WRITETRACK:
        if (f->pos >= f->len) {
            /* the formatting stream: an ID mark (FE) and track/side/sector/size, then a data mark (FB) and the
             * sector's bytes */
            if (fdc_has_disk(f) && f->head < MIRAGE_TRACKS) {
                sec = -1;
                for (i = 0; i < f->len; ++i) {
                    if (f->buf[i] == 0xFE && i + 4 < f->len) { sec = f->buf[i + 3]; i += 4; continue; }
                    if (f->buf[i] == 0xFB && sec >= 0 && sec <= 5) {
                        n = fdc_sector_len(sec);
                        if (i + 1 + n <= f->len)
                            fdc_disk_write(f, (uint32_t)f->head * MIRAGE_TRACK_BYTES + (uint32_t)fdc_sector_offset(sec),
                                           &f->buf[i + 1], (uint32_t)n);
                        i += n;
                        sec = -1;
                    }
                }
                f->dirty = 1;
            }
            fdc_finish(f, 0);
            break;
        }
        f->drq = 1;
        f->delay = FDC_WAIT;
        break;
    default: break;
    }
}
