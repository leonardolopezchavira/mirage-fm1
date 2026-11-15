/* SPDX-License-Identifier: MIT
 * The Ensoniq Mirage (DSK-8 main board), emulated: a 6809E at 2 MHz, 128 KB of sample RAM, a 6522 VIA, a 6850 ACIA
 * (MIDI), a WD1772 floppy controller, an ES5503 DOC and 8 Curtis CEM3328 filters. It runs Ensoniq's own boot ROM and
 * operating system. Memory map after MAME's enmirage.cpp (BSD-3-Clause, R. Belmont, tim lindner).
 *
 * A C99 port of mirage-remake's src/emu (MIT): no allocation, no OS calls, single precision on the FM-1
 * (mirage_math.h). The caller owns the memory: the machine (mirage_sizeof() bytes), the 128 KB of sample RAM and
 * the 4 KB boot ROM (which may stay in flash). */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIRAGE_CPU_HZ 2000000
/* Two builds of the analog section (the digital parts are the same in both):
 *   exact (MIRAGE_DOUBLE): mirage-remake's arithmetic at its 125 kHz, sample for sample (the oracle checks it)
 *   fast (the default, the FM-1): 62.5 kHz, the filters' arithmetic rearranged (no division per sample), one noise
 *        draw per step. The DOC's outputs change at its frame rate (29.4 kHz with 32 oscillators), so 62.5 kHz still
 *        holds each of them twice; the filters stay below 0.45 of it (28 kHz) and the ROM's tuning still finds them. */
#if defined(MIRAGE_DOUBLE) && !defined(MIRAGE_FAST)
#define MIRAGE_EXACT 1
#endif
#ifndef MIRAGE_ANALOG_DIV
#ifdef MIRAGE_EXACT
#define MIRAGE_ANALOG_DIV 16                                  /* every 16 CPU cycles: 125 kHz */
#else
#define MIRAGE_ANALOG_DIV 32                                  /* every 32: 62.5 kHz */
#endif
#endif
#define MIRAGE_ANALOG_HZ (MIRAGE_CPU_HZ / MIRAGE_ANALOG_DIV)
#define MIRAGE_ROM_BYTES 4096u
#define MIRAGE_SRAM_BYTES (128u * 1024u)
#define MIRAGE_RAM_BYTES 0x6000u                              /* $8000-$DFFF */
#define MIRAGE_TRACKS 80
#define MIRAGE_TRACK_BYTES 5632                               /* sectors 0-4 of 1024 bytes, sector 5 of 512 */
#define MIRAGE_DISK_BYTES (MIRAGE_TRACKS * MIRAGE_TRACK_BYTES) /* 450,560 */

/* A disk in the drive: byte offsets into the 450,560-byte image (track * 5632 + sector offset). read returns 0, or
 * nonzero for a failed read (the controller then sees zeros); write 0 makes the disk write protected. The
 * callbacks run inside mirage_render / mirage_run. */
typedef struct {
    int (*read)(void *ctx, uint32_t off, uint8_t *dst, uint32_t n);
    int (*write)(void *ctx, uint32_t off, const uint8_t *src, uint32_t n);
    void *ctx;
} mirage_disk_t;

/* the front panel's keys: row in bits 4-5, column in bits 0-2 (MAME's layout) */
enum {
    MIRAGE_LOAD_UPPER = 0x00, MIRAGE_LOAD_LOWER = 0x01, MIRAGE_SAMPLE_UPPER = 0x02, MIRAGE_PLAY_SEQ = 0x03,
    MIRAGE_LOAD_SEQ = 0x04, MIRAGE_SAVE_SEQ = 0x05, MIRAGE_REC_SEQ = 0x06, MIRAGE_SAMPLE_LOWER = 0x07,
    MIRAGE_K3 = 0x10, MIRAGE_K6 = 0x11, MIRAGE_K9 = 0x12, MIRAGE_K5 = 0x13, MIRAGE_K8 = 0x14, MIRAGE_K0_PROG = 0x15,
    MIRAGE_K2 = 0x16, MIRAGE_ENTER = 0x17,
    MIRAGE_K1 = 0x20, MIRAGE_K4 = 0x21, MIRAGE_K7 = 0x22, MIRAGE_UP = 0x23, MIRAGE_PARAM = 0x24, MIRAGE_DOWN = 0x25,
    MIRAGE_VALUE = 0x26, MIRAGE_CANCEL = 0x27
};

typedef struct mirage_machine mirage_t;

#define MIRAGE_MACHINE_MAX 40960u   /* mirage_sizeof() is at most this (for static buffers) */
size_t mirage_sizeof(void);
/* mem: mirage_sizeof() bytes, aligned for a pointer; rom: 4096 bytes at $F000; sram: 128 KB */
mirage_t *mirage_init(void *mem, const uint8_t *rom, uint8_t *sram);
void mirage_reset(mirage_t *m);                       /* power-on reset (memory is kept) */

void mirage_run(mirage_t *m, uint32_t cycles);        /* run at least this many CPU cycles, the sound dropped */
/* n samples of the analog section at MIRAGE_ANALOG_HZ (mono, about +-1), the CPU run as needed; in: the audio
 * input at the same rate (n samples), or 0 */
void mirage_render(mirage_t *m, float *out, int n, const float *in);

int mirage_midi_in(mirage_t *m, uint8_t b);           /* 0: the receive queue is full */
void mirage_set_midi_out(mirage_t *m, void (*fn)(void *ctx, uint8_t b), void *ctx);
void mirage_set_key(mirage_t *m, int key, int down);
void mirage_set_wheels(mirage_t *m, uint8_t pitch, uint8_t mod);   /* the A/D's wheel inputs, 0x80 / 0x00 at rest */

void mirage_insert_disk(mirage_t *m, const mirage_disk_t *disk);   /* 0 ejects; the struct must outlive it */
int mirage_motor(const mirage_t *m);                  /* the drive's motor is on */
int mirage_disk_dirty(const mirage_t *m);             /* written since the last clear */
void mirage_disk_clear_dirty(mirage_t *m);
void mirage_set_write_protect(mirage_t *m, int on);

/* the display: segment bits (a..g = bits 0..6, dp = bit 7) of the left (0) and right (1) digit, lit during the last
 * ~20 ms; and each segment's brightness (0..1: the multiplex duty) */
uint8_t mirage_display(const mirage_t *m, int digit);
float mirage_segment_level(const mirage_t *m, int digit, int seg);

/* Two cores. In split mode the machine leaves the analog section alone: a render records, for each analog step,
 * what reaches the filters (the DOC's channel outputs when a frame changes them, and every write to the filters'
 * DACs), and mirage_analog_run plays that record through the filters, on another core, while the machine goes on
 * with the next block. The record of a block is a mirage_stream_t. The filters' output is not there for the A/D
 * converter in split mode: the ROM's filter tuning and sampling need the machine in step with its filters (split
 * off); mirage_adc_stale counts the A/D reads of the audio made in split mode, so the caller can tell. */
#define MIRAGE_STREAM_STEPS 400   /* analog steps in a record: a 256-frame block at 44.1 kHz needs 363 */
#define MIRAGE_STREAM_DACS 384    /* filter DAC writes in a record (more are folded into the last values) */
typedef struct {
    uint16_t steps;               /* analog steps recorded */
    uint16_t nframes, ndac;
    uint8_t codes[16];            /* the filters' cutoff (0-7) and resonance (8-15) DAC values at the start */
    uint16_t frame_step[MIRAGE_STREAM_STEPS];   /* the DOC's outputs changed before this step... */
    int32_t frame_chan[MIRAGE_STREAM_STEPS][8]; /* ...to these */
    uint16_t dac_step[MIRAGE_STREAM_DACS];      /* a DAC write before this step: */
    uint8_t dac_ch[MIRAGE_STREAM_DACS];         /* the channel, | 0x10 for the resonance, | 0x20 for the cutoff */
    uint8_t dac_val[MIRAGE_STREAM_DACS];
} mirage_stream_t;

void mirage_set_split(mirage_t *m, int on);
/* split mode: run the machine until s holds at least n analog steps (it may hold one or two more); returns them */
int mirage_render_split(mirage_t *m, mirage_stream_t *s, int n);
/* the filters over a record: s->steps samples into out (the other core; nothing else touches the filters then) */
void mirage_analog_run(mirage_t *m, const mirage_stream_t *s, float *out);
uint32_t mirage_adc_stale(const mirage_t *m);
/* the analog samples a lockstep render made past its end (for the next render): taken out, or put back */
int mirage_take_pending(mirage_t *m, float *dst, int max);
void mirage_put_pending(mirage_t *m, const float *src, int n);

int mirage_sampling(const mirage_t *m);               /* the OS is sampling (VIA timer 1 paces the A/D) */
float mirage_take_input_level(mirage_t *m);           /* the input's peak at the A/D path since the last call */

int mirage_doc_oscillators(const mirage_t *m);       /* the DOC's enabled oscillators (its frame rate follows) */
uint64_t mirage_cycles(const mirage_t *m);
uint16_t mirage_pc(const mirage_t *m);
uint8_t *mirage_main_ram(mirage_t *m);                /* $8000-$DFFF, MIRAGE_RAM_BYTES */
uint8_t *mirage_sample_ram(mirage_t *m);

/* the machine's state for a snapshot: everything but the sample RAM, the ROM and the disk; a snapshot is valid for
 * the build that made it (the same mirage_sizeof()) */
void mirage_snapshot_save(const mirage_t *m, void *dst);
void mirage_snapshot_load(mirage_t *m, const void *src);

#ifdef __cplusplus
}
#endif
