/* SPDX-License-Identifier: MIT
 * Ensoniq ES5503 "DOC", stepped one frame (every enabled oscillator once) at a time. Behaviour after the ES5503
 * datasheet and MAME's es5503.cpp (BSD-3-Clause, R. Belmont) for halt, swap and sync. A C port of mirage-remake's
 * Es5503 (MIT). DOC_HOST supplies doc_read_adc(DOC_HOST *). */
#pragma once
#include <stdint.h>

#define DOC_CHANNELS 8

typedef struct {
    uint16_t freq, wtsize;
    uint8_t control, vol, data;
    uint32_t wtptr;   /* 17-bit */
    uint8_t wtsize_sel, res;
    uint32_t acc;
    uint8_t irqpend;
    /* worked out when the registers are written, not at every frame: */
    uint8_t resshift;     /* kResShifts[res] - wtsize_sel */
    uint32_t base;        /* wtptr & kWaveMasks[wtsize_sel] */
    uint32_t sizemask;    /* kAccMasks[wtsize_sel] */
} doc_osc_t;

typedef struct {
    const uint8_t *ram;
    doc_osc_t osc[32];
    int osc_count;
    uint8_t reg_e0;
    uint8_t irq_line;
    int32_t chan[DOC_CHANNELS];
} es5503_t;

static uint8_t doc_read_adc(DOC_HOST *h);

static const uint16_t kWaveSizes[8] = {256, 512, 1024, 2048, 4096, 8192, 16384, 32768};
static const uint32_t kWaveMasks[8] = {0x1FF00, 0x1FE00, 0x1FC00, 0x1F800, 0x1F000, 0x1E000, 0x1C000, 0x18000};
static const uint32_t kAccMasks[8] = {0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF, 0x3FFF, 0x7FFF};
static const int kResShifts[8] = {9, 10, 11, 12, 13, 14, 15, 16};
enum { MODE_FREE = 0, MODE_ONESHOT = 1, MODE_SYNCAM = 2, MODE_SWAP = 3 };

static inline void doc_osc_cache(doc_osc_t *o)
{
    o->resshift = (uint8_t)(kResShifts[o->res] - o->wtsize_sel);
    o->base = o->wtptr & kWaveMasks[o->wtsize_sel];
    o->sizemask = kAccMasks[o->wtsize_sel];
}

static inline int doc_frame_clocks(const es5503_t *d) { return (d->osc_count + 2) * 8; }   /* 8 MHz clocks */

static void doc_reset(es5503_t *d)
{
    int i;
    for (i = 0; i < 32; ++i) {
        doc_osc_t *o = &d->osc[i];
        *o = (doc_osc_t){0};
        o->wtsize = 256;
        o->control = 1;
        o->data = 0x80;
        doc_osc_cache(o);
    }
    d->osc_count = 1;
    d->reg_e0 = 0xFF;
    d->irq_line = 0;
    for (i = 0; i < DOC_CHANNELS; ++i) d->chan[i] = 0;
}

static void doc_update_irq(es5503_t *d)
{
    int i;
    d->irq_line = 0;
    for (i = 0; i < d->osc_count; ++i)
        if (d->osc[i].irqpend) { d->irq_line = 1; break; }
}

static void doc_halt(es5503_t *d, int n, int zero_byte, uint32_t *acc, int resshift, uint8_t new_ctrl)
{
    doc_osc_t *o = &d->osc[n];
    doc_osc_t *partner = &d->osc[n ^ 1];
    int mode = (o->control >> 1) & 3;
    const int partner_mode = (partner->control >> 1) & 3;

    if (mode == MODE_SYNCAM) {
        if (!(n & 1) && n > 0 && !(d->osc[n - 1].control & 1)) d->osc[n - 1].acc = 0;
        mode = MODE_FREE;
    }
    if (mode != MODE_FREE || zero_byte) o->control |= 1;
    else *acc -= (uint32_t)(o->wtsize - 1) << resshift;

    if (mode == MODE_SWAP) {
        partner->control &= (uint8_t)~1;
        partner->acc = 0;
    } else if (partner_mode == MODE_SWAP && !(n & 1) && (o->control & 1)) {
        /* an even oscillator that just halted, its partner in swap mode, retriggers (MAME, verified on IIgs
         * hardware); only when it halted: rewinding a free-running one a second time made every loop ~1.5x too long */
        o->control &= (uint8_t)~1;
        *acc -= (uint32_t)(o->wtsize - 1) << resshift;
    }

    if (o->control & 0x08) {
        if (!zero_byte && !(new_ctrl & 0x08)) return;
        o->irqpend = 1;
        d->irq_line = 1;
    }
}

static void doc_frame(es5503_t *d)
{
    int n, i;
    for (i = 0; i < DOC_CHANNELS; ++i) d->chan[i] = 0;
    for (n = 0; n < d->osc_count; ++n) {
        doc_osc_t *o = &d->osc[n];
        uint32_t wtptr, sizemask, acc, altram, addr;
        int resshift, mode;
        uint8_t raw;
        if (o->control & 1) continue;
        wtptr = o->base;
        resshift = o->resshift;
        sizemask = o->sizemask;
        mode = (o->control >> 1) & 3;
        acc = o->acc;

        altram = acc >> resshift;
        addr = ((altram & sizemask) + wtptr) & 0x1FFFF;
        acc = (acc + o->freq) & 0xFFFFFF;
        raw = d->ram[addr];
        if (raw == 0x00) {
            doc_halt(d, n, 1, &acc, resshift, o->control);
        } else {
            const int32_t s = (int32_t)(int8_t)(raw ^ 0x80) * o->vol;
            o->data = raw;
            if (mode != MODE_SYNCAM) {
                d->chan[(o->control >> 4) & 7] += s;
            } else if (n & 1) {
                if (n < 31 && !(d->osc[n + 1].control & 1)) d->osc[n + 1].vol = raw;
            } else {
                d->chan[(o->control >> 4) & 7] += s;
            }
            if (altram >= (uint32_t)(o->wtsize - 1)) doc_halt(d, n, 0, &acc, resshift, o->control);
        }
        o->acc = acc;
    }
}

static uint8_t doc_read(DOC_HOST *h, es5503_t *d, int reg)
{
    int i;
    uint8_t ret;
    reg &= 0xFF;
    if (reg < 0xE0) {
        const doc_osc_t *o = &d->osc[reg & 0x1F];
        switch (reg & 0xE0) {
        case 0x00: return (uint8_t)o->freq;
        case 0x20: return (uint8_t)(o->freq >> 8);
        case 0x40: return o->vol;
        case 0x60: return o->data;
        case 0x80: return (uint8_t)(o->wtptr >> 8);
        case 0xA0: return o->control;
        case 0xC0: return (uint8_t)(((o->wtptr & 0x10000) ? 0x40 : 0) | (o->wtsize_sel << 3) | o->res);
        }
        return 0;
    }
    switch (reg) {
    case 0xE0:
        ret = d->reg_e0;
        d->irq_line = 0;
        for (i = 0; i < d->osc_count; ++i) {
            if (d->osc[i].irqpend) {
                ret = (uint8_t)(i << 1);
                d->reg_e0 = (uint8_t)(ret | 0x80);
                d->osc[i].irqpend = 0;
                break;
            }
        }
        doc_update_irq(d);
        return (uint8_t)(ret | 0x41);
    case 0xE1: return (uint8_t)((d->osc_count - 1) << 1);
    case 0xE2: return doc_read_adc(h);
    }
    return 0;
}

static void doc_write(es5503_t *d, int reg, uint8_t v)
{
    reg &= 0xFF;
    if (reg < 0xE0) {
        const int n = reg & 0x1F;
        doc_osc_t *o = &d->osc[n];
        switch (reg & 0xE0) {
        case 0x00: o->freq = (uint16_t)((o->freq & 0xFF00) | v); break;
        case 0x20: o->freq = (uint16_t)((o->freq & 0x00FF) | (v << 8)); break;
        case 0x40: o->vol = v; break;
        case 0x60: break;
        case 0x80: o->wtptr = (o->wtptr & 0x10000) | (uint32_t)(v << 8); doc_osc_cache(o); break;
        case 0xA0:
            if ((o->control & 1) && !(v & 1)) o->acc = 0;
            if (!(o->control & 1) && (v & 1) && ((v >> 1) & 1)) doc_halt(d, n, 0, &o->acc, kResShifts[o->res], v);
            o->control = v;
            break;
        case 0xC0:
            if (v & 0x40) o->wtptr |= 0x10000; else o->wtptr &= 0xFFFF;
            o->wtsize_sel = (v >> 3) & 7;
            o->wtsize = kWaveSizes[o->wtsize_sel];
            o->res = v & 7;
            doc_osc_cache(o);
            break;
        }
        return;
    }
    if (reg == 0xE1) d->osc_count = ((v >> 1) & 0x1F) + 1;
}
