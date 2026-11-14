/* SPDX-License-Identifier: MIT
 * MOS 6522 VIA, the subset the Mirage uses (ports, T1 free-run, T2 one-shot, shift register, CA1/CB1 edges,
 * interrupt flags), clocked at the CPU's E clock. A C port of mirage-remake's Via6522 (MIT).
 * Included once, by mirage.c, which supplies the port callbacks:
 *   via_port_a_out(VIA_HOST *, uint8_t)   via_port_b_out(VIA_HOST *, uint8_t)
 *   via_port_a_in(VIA_HOST *)             via_port_b_in(VIA_HOST *) */
#pragma once
#include <stdint.h>

enum { IFR_CA2 = 0x01, IFR_CA1 = 0x02, IFR_SR = 0x04, IFR_CB2 = 0x08, IFR_CB1 = 0x10, IFR_T2 = 0x20, IFR_T1 = 0x40 };

typedef struct {
    uint8_t ora, orb, ddra, ddrb, acr, pcr, ifr, ier, sr;
    uint16_t t1latch, t2latch_lo;
    int32_t t1, t2;
    uint8_t t1armed, t2armed, pb7, ca1;
} via6522_t;

