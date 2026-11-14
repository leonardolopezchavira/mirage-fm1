/* SPDX-License-Identifier: MIT
 * Motorola MC6809E, cycle-counted at instruction granularity (datasheet timings), with NMI, FIRQ, IRQ,
 * SWI/SWI2/SWI3, CWAI and SYNC. A C port of mirage-remake's Cpu6809 (MIT), instruction for instruction.
 *
 * Included once, by mirage.c, after it has defined:
 *   CPU_HOST       the type the bus functions take (mirage_t)
 *   cpu_bus_rd     static uint8_t cpu_bus_rd(CPU_HOST *h, uint16_t a)
 *   cpu_bus_wr     static void cpu_bus_wr(CPU_HOST *h, uint16_t a, uint8_t v)
 * and with the host holding a `cpu6809_t cpu` member. Every memory access is made in the order the hardware makes
 * it, since some of them are I/O reads with side effects. */
#pragma once
#include <stdint.h>

enum { CC_C = 0x01, CC_V = 0x02, CC_Z = 0x04, CC_N = 0x08, CC_I = 0x10, CC_H = 0x20, CC_F = 0x40, CC_E = 0x80 };

typedef struct {
    uint8_t a, b, dp, cc;
    uint16_t x, y, u, s, pc;
    uint64_t total;                /* cycles since power-on (additions only: no 64-bit multiply on the AC79) */
    uint8_t irq, firq, nmi_line, nmi_pending, nmi_armed, cwai, sync;
    uint8_t illegal;
    uint16_t illegal_pc;
    int extra;                     /* extra cycles from indexed addressing */
} cpu6809_t;

