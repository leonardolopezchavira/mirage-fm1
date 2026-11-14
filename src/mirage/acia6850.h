/* SPDX-License-Identifier: MIT
 * Motorola 6850 ACIA, the Mirage's MIDI port. Bytes are paced at MIDI speed (31,250 baud, 10 bits) in CPU cycles.
 * A C port of mirage-remake's Acia6850 (MIT); the receive queue is a fixed ring. */
#pragma once
#include <stdint.h>

#define ACIA_BYTE_CYCLES 640   /* 320 us at 2 MHz */
#define ACIA_RXQ 512u          /* power of two */

typedef struct {
    uint8_t ctrl, status, rdr;
    int rx_wait, tx_busy;
    uint16_t rx_r, rx_w;
    uint8_t rxq[ACIA_RXQ];
    void (*on_tx)(void *ctx, uint8_t b);   /* MIDI out (optional) */
    void *tx_ctx;
} acia6850_t;

static void acia_reset(acia6850_t *a)
{
    a->ctrl = 0x03;
    a->status = 0x02;
    a->rx_r = a->rx_w = 0;
    a->rx_wait = 0;
    a->tx_busy = 0;
}

static inline int acia_irq(const acia6850_t *a)
{
    if ((a->ctrl & 3) == 3) return 0;
    if ((a->ctrl & 0x80) && (a->status & 0x01)) return 1;
    if ((a->ctrl & 0x60) == 0x20 && (a->status & 0x02)) return 1;
    return 0;
}

static uint8_t acia_read(acia6850_t *a, int reg)
{
    if (!(reg & 1)) return (uint8_t)(a->status | (acia_irq(a) ? 0x80 : 0));
    a->status &= (uint8_t)~0x21;   /* RDRF, OVRN */
    return a->rdr;
}

static void acia_write(acia6850_t *a, int reg, uint8_t v)
{
    if (!(reg & 1)) {
        a->ctrl = v;
        if ((v & 3) == 3) { a->status = 0x02; a->tx_busy = 0; }
        return;
    }
    if (a->on_tx) a->on_tx(a->tx_ctx, v);
    a->status &= (uint8_t)~0x02;
    a->tx_busy = ACIA_BYTE_CYCLES;
}

static void acia_tick(acia6850_t *a, int cycles)
{
    if (a->tx_busy > 0 && (a->tx_busy -= cycles) <= 0) a->status |= 0x02;
    if (a->rx_wait > 0) a->rx_wait -= cycles;
    if (a->rx_wait <= 0 && a->rx_r != a->rx_w && !(a->status & 0x01) && (a->ctrl & 3) != 3) {
        a->rdr = a->rxq[a->rx_r & (ACIA_RXQ - 1u)];
        a->rx_r++;
        a->status |= 0x01;
        a->rx_wait = ACIA_BYTE_CYCLES;
    }
}

/* a MIDI byte in; 0 when the queue is full (the byte is dropped) */
static int acia_receive(acia6850_t *a, uint8_t b)
{
    if ((uint16_t)(a->rx_w - a->rx_r) >= ACIA_RXQ) return 0;
    a->rxq[a->rx_w & (ACIA_RXQ - 1u)] = b;
    a->rx_w++;
    return 1;
}
