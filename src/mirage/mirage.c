/* SPDX-License-Identifier: MIT
 * The Mirage's main board: the memory map, the I/O wiring and the analog section. A C port of mirage-remake's
 * Mirage.cpp (MIT); memory map after MAME's enmirage.cpp (BSD-3-Clause, R. Belmont, tim lindner). */
#include "mirage.h"
#include "mirage_math.h"

struct mirage_machine;
#define CPU_HOST struct mirage_machine
#define VIA_HOST struct mirage_machine
#define DOC_HOST struct mirage_machine
#include "acia6850.h"
#include "cpu6809.h"
#include "via6522.h"
#include "cem3328.h"
#include "es5503.h"
#include "wd1772.h"

#define OUTQ 64   /* analog samples made past the end of a render (one instruction makes at most a few) */

/* The analog section: what the second core touches in split mode, and nothing else, so it is kept apart (on cache
 * lines of its own: the padding around it in the machine) */
typedef struct {
    filter_tuning_t tuning;
    cem3328_t vcf[8];
    float chan_in[8];      /* the sample-and-hold outputs of the DOC's demultiplexer */
    float mix;             /* the summed VCF outputs */
    uint32_t noise;
} analog_t;

struct mirage_machine {
    /* the CPU first: cpu6809.h and via6522.h reach their state through the host */
    cpu6809_t cpu;
    via6522_t via;
    acia6850_t acia;
    wd1772_t fdc;
    es5503_t doc;
    uint8_t pad0[64];
    analog_t an;
    uint8_t pad1[64];

    const uint8_t *rom;
    uint8_t *sram;
    uint8_t ram[MIRAGE_RAM_BYTES];

    int bank;
    uint8_t port_b;
    uint8_t keys[3];   /* pressed bits per row */
    uint8_t dac;
    uint8_t pitch, mod;
    float audio_in;
    const float *in_buf;   /* the analog-rate audio input for the current render */
    int in_pos, in_len;
    float in_level;        /* the input's peak where it reaches the A/D path */

    uint32_t seg_acc[2][8];
    float seg_level[2][8];
    uint8_t last_pa;
    uint64_t last_pa_change, seg_window_start;

    int doc_acc;           /* CPU cycles towards the next DOC frame */
    int analog_acc;        /* CPU cycles towards the next analog sample */

    /* split mode (mirage.h): the record being made */
    uint8_t split, frame_new;
    uint8_t codes[16];     /* the filters' DAC values, kept in both modes */
    mirage_stream_t *rec;
    int rec_steps;
    uint32_t adc_stale;

    int pend;              /* cycles run in this slice and not yet ticked (run_slice) */
    int synced;            /* cycles of this slice ticked at I/O accesses */
    uint8_t resched;       /* an I/O access: the next event may have moved */

    float *out;            /* the current render's output, and how far it has got */
    int out_n, out_len;
    float outq[OUTQ];      /* made past its end, for the next render */
    int outq_n;
};

static inline uint8_t cpu_bus_rd(struct mirage_machine *m, uint16_t a);
static inline void cpu_bus_wr(struct mirage_machine *m, uint16_t a, uint8_t v);
static void update_irqs(struct mirage_machine *m);
_Static_assert(sizeof(struct mirage_machine) <= MIRAGE_MACHINE_MAX, "MIRAGE_MACHINE_MAX is too small");
#include "cpu6809_impl.h"
#include "via6522_impl.h"

static void update_irqs(struct mirage_machine *m);
static void credit_segments(struct mirage_machine *m);

/* ------------------------------------------------------------------ wiring */
static uint8_t via_port_a_in(struct mirage_machine *m)
{
    const int col = via_port_a_value(&m->via) & 7;
    uint8_t v = 0xE0;
    int r;
    for (r = 0; r < 3; ++r)
        if (m->keys[r] & (1 << col)) v &= (uint8_t)~(0x20 << r);
    return (uint8_t)(v | 0x1F);
}

static void via_port_a_out(struct mirage_machine *m, uint8_t v)
{
    /* PA0-2 select a segment (through a 74LS145), PA3/PA4 enable the left/right digit */
    credit_segments(m);
    m->last_pa = v;
}

static uint8_t via_port_b_in(struct mirage_machine *m)
{
    uint8_t v = 0;
    if ((m->cpu.total >> 1) & 1u) v |= 0x20;   /* the DOC's CA3 clock (500 kHz) */
    if (fdc_has_disk(&m->fdc)) v |= 0x40;      /* disk present */
    return v;
}

static void via_port_b_out(struct mirage_machine *m, uint8_t v)
{
    m->port_b = v;
    m->bank = v & 3;
    m->fdc.motor = (uint8_t)!(v & 0x10);
}

static uint8_t doc_read_adc(struct mirage_machine *m)
{
    /* VIA PB2-3 select the A/D input: 0 the compressed (mic) input, 1 the line input, 2 the pitch wheel, 3 the mod
     * wheel. The audio input reaches the converter through VCF 7 (the sampling "input filter", analog_step()),
     * mixed with the other filters' outputs. */
    long v;
    switch ((m->port_b >> 2) & 3) {
    case 2: return m->pitch;
    case 3: return m->mod;
    default:
        if (m->split) m->adc_stale++;   /* (the filters are on the other core: their output is a block old) */
        v = m_lroundf(128.0f + 300.0f * m->an.mix);
        return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
    }
}

static void tick(struct mirage_machine *m, int c);

/* Before an I/O access: the peripherals brought up to this instruction's start (the cycles of the instructions run so
 * far in this slice; no event falls among them, run_slice saw to that) */
static inline void sync_io(struct mirage_machine *m)
{
    if (m->pend) {
        tick(m, m->pend);
        m->synced += m->pend;
        m->pend = 0;
    }
    m->resched = 1;
}

/* the bus: memory inline (almost every access), I/O apart */
static uint8_t bus_rd_io(struct mirage_machine *m, uint16_t a)
{
    uint8_t v;
    sync_io(m);
    if (a >= 0xEC00 && a <= 0xECFF) return doc_read(m, &m->doc, a & 0xFF);
    if ((a & 0xFF00) == 0xE200) { v = via_read(m, a & 0xF); update_irqs(m); return v; }
    if (a >= 0xE800 && a <= 0xE803) { v = fdc_read(&m->fdc, a & 3); update_irqs(m); return v; }
    if (a == 0xE100 || a == 0xE101) { v = acia_read(&m->acia, a & 1); update_irqs(m); return v; }
    return 0xFF;
}

static inline uint8_t cpu_bus_rd(struct mirage_machine *m, uint16_t a)
{
    if (a < 0x8000) return m->sram[(size_t)m->bank << 15 | a];
    if (a < 0xE000) return m->ram[a - 0x8000];
    if (a >= 0xF000) return m->rom[a - 0xF000];
    return bus_rd_io(m, a);
}

static void bus_wr_io(struct mirage_machine *m, uint16_t a, uint8_t v)
{
    sync_io(m);
    if (a >= 0xEC00 && a <= 0xECFF) { doc_write(&m->doc, a & 0xFF, v); update_irqs(m); return; }
    if ((a & 0xFF00) == 0xE200) { via_write(m, a & 0xF, v); update_irqs(m); return; }
    if (a >= 0xE800 && a <= 0xE803) { fdc_write(&m->fdc, a & 3, v); update_irqs(m); return; }
    if (a == 0xE100 || a == 0xE101) { acia_write(&m->acia, a & 1, v); update_irqs(m); return; }
    if (a >= 0xE400 && a <= 0xE41F) {
        /* $E418-F: preset the multiplexed DAC. $E400-7: strobe the cutoff and resonance S&H, $E408-F: resonance S&H,
         * $E410-7: cutoff S&H (the channel in the low 3 bits) */
        const int ch = a & 7, sel = (a >> 3) & 3;
        m->dac = v;
        if (sel != 3) {
            if (!(sel & 2)) m->codes[8 + ch] = m->dac;
            if (!(sel & 1)) m->codes[ch] = m->dac;
            if (!m->split) {
                if (!(sel & 2)) vcf_set_resonance(&m->an.vcf[ch], m->dac);
                if (!(sel & 1)) vcf_set_cutoff(&m->an.vcf[ch], m->dac);
            } else if (m->rec) {
                mirage_stream_t *r = m->rec;
                const uint8_t what = (uint8_t)(ch | (!(sel & 2) ? 0x10 : 0) | (!(sel & 1) ? 0x20 : 0));
                if (r->ndac < MIRAGE_STREAM_DACS) {
                    r->dac_step[r->ndac] = (uint16_t)m->rec_steps;
                    r->dac_ch[r->ndac] = what;
                    r->dac_val[r->ndac] = m->dac;
                    r->ndac++;
                } else {   /* full: the last one takes the value (its timing within the block is lost) */
                    r->dac_ch[MIRAGE_STREAM_DACS - 1] = what;
                    r->dac_val[MIRAGE_STREAM_DACS - 1] = m->dac;
                }
            }
        }
    }
}


static inline void cpu_bus_wr(struct mirage_machine *m, uint16_t a, uint8_t v)
{
    if (a < 0x8000) { m->sram[(size_t)m->bank << 15 | a] = v; return; }
    if (a < 0xE000) { m->ram[a - 0x8000] = v; return; }
    if (a >= 0xF000) return;
    bus_wr_io(m, a, v);
}

static void update_irqs(struct mirage_machine *m)
{
    cpu_set_irq(m, via_irq(&m->via) || m->fdc.drq || m->doc.irq_line);
    cpu_set_firq(m, acia_irq(&m->acia));
    cpu_set_nmi(m, m->fdc.intrq);
}

static void credit_segments(struct mirage_machine *m)
{
    const uint64_t now = m->cpu.total;
    const uint32_t dt = (uint32_t)(now - m->last_pa_change);
    const int seg = m->last_pa & 7;
    uint32_t window;
    int d, s;
    if (!(m->last_pa & 0x08)) m->seg_acc[0][seg] += dt;   /* left digit anode (PA3, active low) */
    if (!(m->last_pa & 0x10)) m->seg_acc[1][seg] += dt;   /* right digit anode (PA4, active low) */
    m->last_pa_change = now;

    window = (uint32_t)(now - m->seg_window_start);
    if (window >= 40000u) {   /* 20 ms */
        for (d = 0; d < 2; ++d)
            for (s = 0; s < 8; ++s) {
                /* one of 8 segment lines is driven at a time: full duty for a segment is 1/8 */
                const float l = (float)m->seg_acc[d][s] * 8.0f / (float)window;
                m->seg_level[d][s] = l < 1.0f ? l : 1.0f;
                m->seg_acc[d][s] = 0;
            }
        m->seg_window_start = now;
    }
}

/* ------------------------------------------------------------------ the analog section */
/* one step of the filters: chan_in (+ in7 at VCF 7) to the summed output */
static inline float analog_compute(analog_t *an, float in7)
{
    float sum = 0;
    int i;
    /* a little analog noise at each filter's input: the real VCFs never sit at exactly zero, which is what lets
     * them start self-oscillating during the boot-time tuning */
#ifdef MIRAGE_EXACT
    for (i = 0; i < 8; ++i) {
        float n;
        an->noise = an->noise * 1664525u + 1013904223u;
        n = (float)((int32_t)an->noise >> 8) * (1.0f / 8388608.0f) * 1e-4f;
        sum += vcf_process(&an->vcf[i], an->chan_in[i] + n + (i == 7 ? in7 : 0.0f));
    }
#else
    an->noise = an->noise * 1664525u + 1013904223u;   /* one draw, a different rotation of it for each filter */
    for (i = 0; i < 8; ++i) {
        const uint32_t r = i ? (an->noise << (4 * i)) | (an->noise >> (32 - 4 * i)) : an->noise;
        const float n = (float)((int32_t)r >> 8) * (1e-4f / 8388608.0f);
        sum += vcf_process(&an->vcf[i], an->chan_in[i] + n + (i == 7 ? in7 : 0.0f));
    }
#endif
    an->mix = sum;
    return sum;
}

static void analog_step(struct mirage_machine *m)
{
    float in7 = 0, sum, a;
    int src;
    if (m->split) {   /* recorded for the other core */
        mirage_stream_t *r = m->rec;
        if (r && m->rec_steps < MIRAGE_STREAM_STEPS) {
            if (m->frame_new) {
                int i;
                r->frame_step[r->nframes] = (uint16_t)m->rec_steps;
                for (i = 0; i < 8; ++i) r->frame_chan[r->nframes][i] = m->doc.chan[i];
                r->nframes++;
                m->frame_new = 0;
            }
            m->rec_steps++;
        }
        return;
    }
    if (m->in_buf && m->in_pos < m->in_len) m->audio_in = m->in_buf[m->in_pos++];

    /* while sampling, the audio input feeds VCF 7, which the OS sets up as the anti-alias "input filter"
     * (parameter 74). VIA PB2-3 pick the source: 01 the line input, 00 the microphone input with its compressor
     * (parameter 75). */
    if (via_t1_irq_enabled(&m->via)) {
        src = (m->port_b >> 2) & 3;
        if (src == 1) in7 = m->audio_in;
        else if (src == 0) in7 = 0.8f * m_tanhf_(4.0f * m->audio_in);
        a = in7 < 0 ? -in7 : in7;
        if (a > m->in_level) m->in_level = a;
    }
    sum = analog_compute(&m->an, in7);
    if (m->out && m->out_n < m->out_len) m->out[m->out_n++] = sum;
    else if (m->outq_n < OUTQ) m->outq[m->outq_n++] = sum;
}

static void tick(struct mirage_machine *m, int c)
{
    int i, frame_cpu;
    via_tick(&m->via, c);
    acia_tick(&m->acia, c);
    fdc_tick(&m->fdc, c);

    frame_cpu = doc_frame_clocks(&m->doc) / 4;   /* 8 MHz DOC clocks to 2 MHz CPU cycles */
#ifdef MIRAGE_EXACT
    /* as mirage-remake: the frames the cycles hold, then the analog steps (a slice ends at each of them) */
    m->doc_acc += c;
    while (m->doc_acc >= frame_cpu) {
        m->doc_acc -= frame_cpu;
        doc_frame(&m->doc);
        if (m->split) m->frame_new = 1;
        else
            for (i = 0; i < 8; ++i) m->an.chan_in[i] = (float)m->doc.chan[i] * (1.0f / 32768.0f);
    }
    m->analog_acc += c;
    while (m->analog_acc >= MIRAGE_ANALOG_DIV) {
        m->analog_acc -= MIRAGE_ANALOG_DIV;
        analog_step(m);
    }
#else
    /* the frames and the analog steps in the order they fall in time (a slice can hold several of each) */
    for (;;) {
        const int to_frame = frame_cpu - m->doc_acc, to_step = MIRAGE_ANALOG_DIV - m->analog_acc;
        if (to_frame <= to_step) {
            if (to_frame > c) break;
            c -= to_frame;
            m->doc_acc = 0;
            m->analog_acc += to_frame;
            doc_frame(&m->doc);
            if (m->split) m->frame_new = 1;
            else
                for (i = 0; i < 8; ++i) m->an.chan_in[i] = (float)m->doc.chan[i] * (1.0f / 32768.0f);
        } else {
            if (to_step > c) break;
            c -= to_step;
            m->analog_acc = 0;
            m->doc_acc += to_step;
            analog_step(m);
        }
    }
    m->doc_acc += c;
    m->analog_acc += c;
#endif
    update_irqs(m);
}

/* ------------------------------------------------------------------ the interface */
size_t mirage_sizeof(void) { return sizeof(struct mirage_machine); }

mirage_t *mirage_init(void *mem, const uint8_t *rom, uint8_t *sram)
{
    /* each filter's component tolerance, in DAC steps */
    static const mreal kDetune[8] = {MR(0.7), MR(-1.1), MR(0.3), MR(1.6), MR(-0.4), MR(-1.8), MR(1.0), MR(-0.2)};
    struct mirage_machine *m = (struct mirage_machine *)mem;
    int i;
    m_zero(m, sizeof *m);
    m->rom = rom;
    m->sram = sram;
    m_fill(sram, 0x7F, MIRAGE_SRAM_BYTES);
    via_init(&m->via);
    acia_reset(&m->acia);
    fdc_init(&m->fdc);
    m->doc.ram = sram;
    doc_reset(&m->doc);
    tuning_defaults(&m->an.tuning);
    for (i = 0; i < 8; ++i) vcf_init(&m->an.vcf[i], &m->an.tuning, (mreal)MIRAGE_ANALOG_HZ, kDetune[i]);
    m->pitch = 0x80;
    m->mod = 0x00;
    m->last_pa = 0xFF;
    m->an.noise = 12345u;
    return m;
}

void mirage_reset(mirage_t *m)
{
    int i;
    via_reset(&m->via);
    acia_reset(&m->acia);
    fdc_reset(&m->fdc);
    doc_reset(&m->doc);
    for (i = 0; i < 8; ++i) vcf_reset(&m->an.vcf[i]);
    m->bank = 0;
    m->doc_acc = m->analog_acc = 0;
    cpu_reset(m);
}

/* The CPU runs in slices: from one event to the next, the peripherals are left alone and ticked once at the end,
 * instead of after every instruction. An event is whatever can change what the CPU sees, or the order of the DOC's
 * frames and the analog section's steps: a VIA timer running out, the ACIA finishing a byte or taking the next one
 * in, the disk controller's next step, the DOC's next frame (its oscillators can interrupt), the next analog step.
 * The slice ends after the instruction that reaches its event, which is where the instruction-by-instruction
 * machine (mirage-remake's) handled it: the same frames and steps, in the same order, from the same state. An I/O
 * access in a slice first ticks the peripherals up to its instruction (sync_io), and the slice's end is worked out
 * again after it. The CPU waiting in CWAI or SYNC for an interrupt that cannot come yet waits out the slice at once.
 * Exact: tests/oracle.cpp holds it to mirage-remake, sample for sample. */
static int next_event(const struct mirage_machine *m, int limit)
{
    int k = limit, d;
    /* a timer's underflow is an event only when it raises an enabled interrupt that is not up already: otherwise
     * the CPU sees it only by reading the VIA, and that read syncs first (timer 1 free-runs with its latch at 0 in
     * OS 3.2, an underflow every 2 cycles) */
    if ((m->via.ier & IFR_T1) && m->via.t1armed && !(m->via.ifr & IFR_T1) && (d = m->via.t1 + 1) < k) k = d;
    if ((m->via.ier & IFR_T2) && m->via.t2armed && !(m->via.ifr & IFR_T2) && !(m->via.acr & 0x20)
        && (d = m->via.t2 + 1) < k)
        k = d;
    if (m->acia.tx_busy > 0 && (m->acia.ctrl & 0x60) == 0x20 && m->acia.tx_busy < k) k = m->acia.tx_busy;   /* (its
                                                                     * interrupt on: else only a read sees it) */
    if (m->acia.rx_r != m->acia.rx_w && (d = m->acia.rx_wait > 0 ? m->acia.rx_wait : 1) < k) k = d;
    if (m->fdc.op != FDC_NONE && !m->fdc.drq && (d = m->fdc.delay > 0 ? m->fdc.delay : 1) < k) k = d;
#ifdef MIRAGE_EXACT
    d = doc_frame_clocks(&m->doc) / 4 - m->doc_acc;
    if (d < k) k = d;
    d = MIRAGE_ANALOG_DIV - m->analog_acc;
    if (d < k) k = d;
#else
    /* a frame is an event only when an oscillator running can interrupt (its halt sets the DOC's IRQ); otherwise a
     * frame changes only what the CPU reads, and a read syncs first; the analog steps go in order in the tick */
    {
        int n;
        for (n = 0; n < m->doc.osc_count; ++n)
            if ((m->doc.osc[n].control & 0x09) == 0x08) {
                d = doc_frame_clocks(&m->doc) / 4 - m->doc_acc;
                if (d < k) k = d;
                break;
            }
    }
#endif
    return k < 1 ? 1 : k;
}

static inline int cpu_can_wake(const struct mirage_machine *m)   /* cpu_step would do more than wait a cycle */
{
    const cpu6809_t *c = &m->cpu;
    if (c->sync) return c->nmi_pending || c->firq || c->irq;
    return (c->nmi_pending && c->nmi_armed) || (c->firq && !(c->cc & CC_F)) || (c->irq && !(c->cc & CC_I));
}

/* one slice, ending by limit cycles from now (or after the instruction that reaches it) */
static void run_slice(struct mirage_machine *m, int limit)
{
    int budget = next_event(m, limit);
    m->pend = 0;
    m->synced = 0;
    m->resched = 0;
    while (m->pend < budget) {
        if ((m->cpu.cwai || m->cpu.sync) && !cpu_can_wake(m)) {
            const int k = budget - m->pend;
            m->cpu.total += (uint64_t)k;
            m->pend += k;
            break;
        }
        m->pend += cpu_step(m);
        if (m->resched) {   /* (from the I/O access's instruction's start, where pend counts from) */
            m->resched = 0;
            budget = next_event(m, limit - m->synced);
        }
    }
    tick(m, m->pend);
    m->pend = 0;
}

void mirage_run(mirage_t *m, uint32_t cycles)
{
    const uint64_t end = m->cpu.total + cycles;
    m->out = 0;
    while (m->cpu.total < end) {
        const uint64_t left = end - m->cpu.total;
        run_slice(m, left < 4096u ? (int)left : 4096);
    }
    m->outq_n = 0;
}

void mirage_set_split(mirage_t *m, int on)
{
    int i;
    if (!on && m->split)   /* the filters take the DAC values they missed */
        for (i = 0; i < 8; ++i) {
            vcf_set_cutoff(&m->an.vcf[i], m->codes[i]);
            vcf_set_resonance(&m->an.vcf[i], m->codes[8 + i]);
        }
    if (on && !m->split) m->frame_new = 1;   /* (the first record starts with the DOC's outputs) */
    m->split = (uint8_t)(on != 0);
    m->rec = 0;
}

int mirage_render_split(mirage_t *m, mirage_stream_t *s, int n)
{
    int i;
    if (n > MIRAGE_STREAM_STEPS - 4) n = MIRAGE_STREAM_STEPS - 4;
    s->steps = s->nframes = s->ndac = 0;
    for (i = 0; i < 16; ++i) s->codes[i] = m->codes[i];
    m->rec = s;
    m->rec_steps = 0;
    m->out = 0;
    while (m->rec_steps < n)   /* (a slice ends where the n-th step is taken, or the instruction after) */
        run_slice(m, (n - m->rec_steps) * MIRAGE_ANALOG_DIV - m->analog_acc);
    m->rec = 0;
    s->steps = (uint16_t)m->rec_steps;
    return m->rec_steps;
}

void mirage_analog_run(mirage_t *m, const mirage_stream_t *s, float *out)
{
    analog_t *an = &m->an;
    int k, i, fi = 0, di = 0;
    for (i = 0; i < 8; ++i) {
        vcf_set_cutoff(&an->vcf[i], s->codes[i]);
        vcf_set_resonance(&an->vcf[i], s->codes[8 + i]);
    }
    for (k = 0; k < s->steps; ++k) {
        while (di < s->ndac && s->dac_step[di] <= k) {
            const uint8_t ch = s->dac_ch[di] & 7;
            if (s->dac_ch[di] & 0x10) vcf_set_resonance(&an->vcf[ch], s->dac_val[di]);
            if (s->dac_ch[di] & 0x20) vcf_set_cutoff(&an->vcf[ch], s->dac_val[di]);
            di++;
        }
        if (fi < s->nframes && s->frame_step[fi] <= k) {
            for (i = 0; i < 8; ++i) an->chan_in[i] = (float)s->frame_chan[fi][i] * (1.0f / 32768.0f);
            fi++;
        }
        out[k] = analog_compute(an, 0.0f);
    }
}

uint32_t mirage_adc_stale(const mirage_t *m) { return m->adc_stale; }

int mirage_take_pending(mirage_t *m, float *dst, int max)
{
    int i, n = m->outq_n < max ? m->outq_n : max;
    for (i = 0; i < n; ++i) dst[i] = m->outq[i];
    m->outq_n = 0;
    return n;
}

void mirage_put_pending(mirage_t *m, const float *src, int n)
{
    int i;
    for (i = 0; i < n && m->outq_n < OUTQ; ++i) m->outq[m->outq_n++] = src[i];
}

void mirage_render(mirage_t *m, float *out, int n, const float *in)
{
    int k = m->outq_n < n ? m->outq_n : n, i;
    m->in_buf = in;
    m->in_pos = 0;
    m->in_len = in ? n : 0;
    /* first what the last render made past its end */
    for (i = 0; i < k; ++i) out[i] = m->outq[i];
    for (i = k; i < m->outq_n; ++i) m->outq[i - k] = m->outq[i];
    m->outq_n -= k;
    m->out = out;
    m->out_n = k;
    m->out_len = n;
    while (m->out_n < n)   /* (a wait ends where the render's last sample is made) */
        run_slice(m, (n - m->out_n) * MIRAGE_ANALOG_DIV - m->analog_acc);
    m->out = 0;
    m->in_buf = 0;
}

int mirage_midi_in(mirage_t *m, uint8_t b) { return acia_receive(&m->acia, b); }

void mirage_set_midi_out(mirage_t *m, void (*fn)(void *ctx, uint8_t b), void *ctx)
{
    m->acia.on_tx = fn;
    m->acia.tx_ctx = ctx;
}

void mirage_set_key(mirage_t *m, int key, int down)
{
    const int row = (key >> 4) & 3, col = key & 7;
    if (row > 2) return;
    if (down) m->keys[row] |= (uint8_t)(1 << col);
    else m->keys[row] &= (uint8_t)~(1 << col);
}

void mirage_set_wheels(mirage_t *m, uint8_t pitch, uint8_t mod) { m->pitch = pitch; m->mod = mod; }
void mirage_insert_disk(mirage_t *m, const mirage_disk_t *disk)
{
    m->fdc.disk = disk;
    m->fdc.dirty = 0;
    m->fdc.write_protect = (uint8_t)(disk && !disk->write);   /* a disk that cannot be written is write protected */
}
int mirage_motor(const mirage_t *m) { return m->fdc.motor; }
int mirage_disk_dirty(const mirage_t *m) { return m->fdc.dirty; }
void mirage_disk_clear_dirty(mirage_t *m) { m->fdc.dirty = 0; }
void mirage_set_write_protect(mirage_t *m, int on) { m->fdc.write_protect = (uint8_t)(on != 0); }

uint8_t mirage_display(const mirage_t *m, int digit)
{
    uint8_t bits = 0;
    int s;
    for (s = 0; s < 8; ++s)
        if (m->seg_level[digit & 1][s] > 0.25f) bits |= (uint8_t)(1 << s);
    return bits;
}

float mirage_segment_level(const mirage_t *m, int digit, int seg) { return m->seg_level[digit & 1][seg & 7]; }
int mirage_sampling(const mirage_t *m) { return via_t1_irq_enabled(&m->via); }

float mirage_take_input_level(mirage_t *m)
{
    const float v = m->in_level;
    m->in_level = 0;
    return v;
}

int mirage_doc_oscillators(const mirage_t *m) { return m->doc.osc_count; }
uint64_t mirage_cycles(const mirage_t *m) { return m->cpu.total; }
uint16_t mirage_pc(const mirage_t *m) { return m->cpu.pc; }
uint8_t *mirage_main_ram(mirage_t *m) { return m->ram; }
uint8_t *mirage_sample_ram(mirage_t *m) { return m->sram; }

void mirage_snapshot_save(const mirage_t *m, void *dst) { m_copy(dst, m, sizeof *m); }

void mirage_snapshot_load(mirage_t *m, const void *src)
{
    const uint8_t *rom = m->rom;
    uint8_t *sram = m->sram;
    const mirage_disk_t *disk = m->fdc.disk;
    void (*tx)(void *, uint8_t) = m->acia.on_tx;
    void *tx_ctx = m->acia.tx_ctx;
    int i;
    m_copy(m, src, sizeof *m);
    m->rom = rom;
    m->sram = sram;
    m->doc.ram = sram;
    m->fdc.disk = disk;
    m->acia.on_tx = tx;
    m->acia.tx_ctx = tx_ctx;
    for (i = 0; i < 8; ++i) m->an.vcf[i].tune = &m->an.tuning;
    m->rec = 0;
    m->out = 0;
    m->in_buf = 0;
}
