/* SPDX-License-Identifier: MIT
 * The VIA's registers and timers (via6522.h has the state and says how this is included). */
#pragma once
#include "via6522.h"

static void via_port_a_out(VIA_HOST *h, uint8_t v);
static void via_port_b_out(VIA_HOST *h, uint8_t v);
static uint8_t via_port_a_in(VIA_HOST *h);
static uint8_t via_port_b_in(VIA_HOST *h);

#define V (&h->via)

static inline int via_irq(const via6522_t *v) { return (v->ifr & v->ier & 0x7F) != 0; }
static inline uint8_t via_port_b_value(const via6522_t *v) { return (uint8_t)((v->orb & v->ddrb) | (uint8_t)~v->ddrb); }
static inline uint8_t via_port_a_value(const via6522_t *v) { return (uint8_t)((v->ora & v->ddra) | (uint8_t)~v->ddra); }
static inline int via_t1_irq_enabled(const via6522_t *v) { return (v->ier & IFR_T1) != 0; }

static void via_init(via6522_t *v)
{
    *v = (via6522_t){0};
    v->t1latch = 0xFFFF;
    v->t2latch_lo = 0xFF;
    v->t1 = 0xFFFF;
    v->t2 = 0xFFFF;
    v->pb7 = 1;
    v->ca1 = 1;
}

static void via_reset(via6522_t *v)
{
    v->ora = v->orb = v->ddra = v->ddrb = 0;
    v->acr = v->pcr = v->ifr = v->ier = v->sr = 0;
    v->t1armed = v->t2armed = 0;
    v->pb7 = 1;
}

static void via_out_a(VIA_HOST *h) { via_port_a_out(h, via_port_a_value(V)); }
static void via_out_b(VIA_HOST *h)
{
    uint8_t v = via_port_b_value(V);
    if (V->acr & 0x80) v = (uint8_t)((v & 0x7F) | (V->pb7 ? 0x80 : 0));
    via_port_b_out(h, v);
}

static uint8_t via_read(VIA_HOST *h, int reg)
{
    uint8_t in, v;
    switch (reg & 0xF) {
    case 0x0:
        V->ifr &= (uint8_t)~(IFR_CB1 | IFR_CB2);
        in = via_port_b_in(h);
        v = (uint8_t)((V->orb & V->ddrb) | (in & (uint8_t)~V->ddrb));
        if (V->acr & 0x80) v = (uint8_t)((v & 0x7F) | (V->pb7 ? 0x80 : 0));
        return v;
    case 0x1:
        V->ifr &= (uint8_t)~(IFR_CA1 | IFR_CA2);
        /* fall through */
    case 0xF:
        in = via_port_a_in(h);
        return (uint8_t)((V->ora & V->ddra) | (in & (uint8_t)~V->ddra));
    case 0x2: return V->ddrb;
    case 0x3: return V->ddra;
    case 0x4: V->ifr &= (uint8_t)~IFR_T1; return (uint8_t)V->t1;
    case 0x5: return (uint8_t)(V->t1 >> 8);
    case 0x6: return (uint8_t)V->t1latch;
    case 0x7: return (uint8_t)(V->t1latch >> 8);
    case 0x8: V->ifr &= (uint8_t)~IFR_T2; return (uint8_t)V->t2;
    case 0x9: return (uint8_t)(V->t2 >> 8);
    case 0xA: V->ifr &= (uint8_t)~IFR_SR; return V->sr;
    case 0xB: return V->acr;
    case 0xC: return V->pcr;
    case 0xD: return (uint8_t)((V->ifr & 0x7F) | (via_irq(V) ? 0x80 : 0));
    case 0xE: return (uint8_t)(V->ier | 0x80);
    }
    return 0xFF;
}

static void via_write(VIA_HOST *h, int reg, uint8_t v)
{
    switch (reg & 0xF) {
    case 0x0: V->orb = v; V->ifr &= (uint8_t)~(IFR_CB1 | IFR_CB2); via_out_b(h); break;
    case 0x1:
        V->ifr &= (uint8_t)~(IFR_CA1 | IFR_CA2);
        /* fall through */
    case 0xF: V->ora = v; via_out_a(h); break;
    case 0x2: V->ddrb = v; via_out_b(h); break;
    case 0x3: V->ddra = v; via_out_a(h); break;
    case 0x4: case 0x6: V->t1latch = (uint16_t)((V->t1latch & 0xFF00) | v); break;
    case 0x7: V->t1latch = (uint16_t)((V->t1latch & 0x00FF) | (v << 8)); V->ifr &= (uint8_t)~IFR_T1; break;
    case 0x5:
        V->t1latch = (uint16_t)((V->t1latch & 0x00FF) | (v << 8));
        V->t1 = V->t1latch + 1;
        V->ifr &= (uint8_t)~IFR_T1;
        V->t1armed = 1;
        if (V->acr & 0x80) { V->pb7 = 0; via_out_b(h); }
        break;
    case 0x8: V->t2latch_lo = v; break;
    case 0x9:
        V->t2 = (v << 8 | V->t2latch_lo) + 1;
        V->ifr &= (uint8_t)~IFR_T2;
        V->t2armed = 1;
        break;
    case 0xA: V->sr = v; V->ifr &= (uint8_t)~IFR_SR; break;
    case 0xB: V->acr = v; via_out_b(h); break;
    case 0xC: V->pcr = v; break;
    case 0xD: V->ifr &= (uint8_t)~(v & 0x7F); break;
    case 0xE:
        if (v & 0x80) V->ier |= (uint8_t)(v & 0x7F);
        else V->ier &= (uint8_t)~(v & 0x7F);
        break;
    }
}

static void via_tick(via6522_t *v, int cycles)
{
    v->t1 -= cycles;   /* timer 1 */
    while (v->t1 < 0) {
        if (v->t1armed) {
            v->ifr |= IFR_T1;
            if (!(v->acr & 0x40)) v->t1armed = 0;   /* one-shot */
        }
        if (v->acr & 0x40) {
            v->t1 += v->t1latch + 2;
            if (v->acr & 0x80) v->pb7 = (uint8_t)!v->pb7;
        } else {
            v->t1 += 0x10000;
            if ((v->acr & 0x80) && !v->pb7) v->pb7 = 1;
        }
    }
    if (!(v->acr & 0x20)) {   /* timer 2, timed mode (pulse counting on PB6 is unused) */
        v->t2 -= cycles;
        while (v->t2 < 0) {
            if (v->t2armed) { v->ifr |= IFR_T2; v->t2armed = 0; }
            v->t2 += 0x10000;
        }
    }
}

#undef V
