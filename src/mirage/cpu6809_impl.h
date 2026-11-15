/* SPDX-License-Identifier: MIT
 * The 6809's instructions (cpu6809.h has the state and says how this is included). */
#pragma once
#include "cpu6809.h"

#define CPU (&h->cpu)
#define RD(a) cpu_bus_rd(h, (uint16_t)(a))
#define WR(a, v) cpu_bus_wr(h, (uint16_t)(a), (uint8_t)(v))
#define D_() ((uint16_t)(CPU->a << 8 | CPU->b))

static inline void cpu_set_d(CPU_HOST *h, uint16_t v) { CPU->a = (uint8_t)(v >> 8); CPU->b = (uint8_t)v; }
static inline uint16_t cpu_rd16(CPU_HOST *h, uint16_t a) { uint16_t hi = RD(a); return (uint16_t)(hi << 8 | RD(a + 1)); }
static inline void cpu_wr16(CPU_HOST *h, uint16_t a, uint16_t v) { WR(a, v >> 8); WR(a + 1, v); }
static inline uint8_t cpu_fetch(CPU_HOST *h) { return RD(CPU->pc++); }
static inline uint16_t cpu_fetch16(CPU_HOST *h) { uint16_t v = cpu_rd16(h, CPU->pc); CPU->pc += 2; return v; }
static inline void cpu_push_s(CPU_HOST *h, uint8_t v) { WR(--CPU->s, v); }
static inline void cpu_push_s16(CPU_HOST *h, uint16_t v) { cpu_push_s(h, (uint8_t)v); cpu_push_s(h, (uint8_t)(v >> 8)); }
static inline uint8_t cpu_pull_s(CPU_HOST *h) { return RD(CPU->s++); }
static inline uint16_t cpu_pull_s16(CPU_HOST *h) { uint16_t hi = cpu_pull_s(h); return (uint16_t)(hi << 8 | cpu_pull_s(h)); }
static inline void cpu_push_u(CPU_HOST *h, uint8_t v) { WR(--CPU->u, v); }
static inline uint8_t cpu_pull_u(CPU_HOST *h) { return RD(CPU->u++); }

static inline void cpu_set_irq(CPU_HOST *h, int on) { CPU->irq = (uint8_t)(on != 0); }
static inline void cpu_set_firq(CPU_HOST *h, int on) { CPU->firq = (uint8_t)(on != 0); }
static inline void cpu_set_nmi(CPU_HOST *h, int on)
{
    if (on && !CPU->nmi_line) CPU->nmi_pending = 1;
    CPU->nmi_line = (uint8_t)(on != 0);
}

static void cpu_reset(CPU_HOST *h)
{
    CPU->dp = 0;
    CPU->cc = CC_I | CC_F;
    CPU->cwai = CPU->sync = 0;
    CPU->nmi_armed = 0;
    CPU->nmi_pending = 0;
    CPU->illegal = 0;
    CPU->pc = cpu_rd16(h, 0xFFFE);
}

/* ------------------------------------------------------------------ flags and arithmetic */
static inline void nz8(CPU_HOST *h, uint8_t r)
{
    CPU->cc &= (uint8_t)~(CC_N | CC_Z);
    if (r & 0x80) CPU->cc |= CC_N;
    if (!r) CPU->cc |= CC_Z;
}
static inline void nz16(CPU_HOST *h, uint16_t r)
{
    CPU->cc &= (uint8_t)~(CC_N | CC_Z);
    if (r & 0x8000) CPU->cc |= CC_N;
    if (!r) CPU->cc |= CC_Z;
}

static uint8_t add8(CPU_HOST *h, uint8_t x8, uint8_t y8, int c)
{
    unsigned r = (unsigned)x8 + y8 + (unsigned)c;
    CPU->cc &= (uint8_t)~(CC_H | CC_N | CC_Z | CC_V | CC_C);
    if (((x8 & 0x0F) + (y8 & 0x0F) + c) & 0x10) CPU->cc |= CC_H;
    if (r & 0x100) CPU->cc |= CC_C;
    if (~(x8 ^ y8) & (x8 ^ r) & 0x80) CPU->cc |= CC_V;
    nz8(h, (uint8_t)r);
    return (uint8_t)r;
}

static uint8_t sub8(CPU_HOST *h, uint8_t x8, uint8_t y8, int c)
{
    unsigned r = (unsigned)x8 - y8 - (unsigned)c;
    CPU->cc &= (uint8_t)~(CC_N | CC_Z | CC_V | CC_C);
    if (r & 0x100) CPU->cc |= CC_C;
    if ((x8 ^ y8) & (x8 ^ r) & 0x80) CPU->cc |= CC_V;
    nz8(h, (uint8_t)r);
    return (uint8_t)r;
}

static uint16_t add16(CPU_HOST *h, uint16_t x16, uint16_t y16)
{
    uint32_t r = (uint32_t)x16 + y16;
    CPU->cc &= (uint8_t)~(CC_N | CC_Z | CC_V | CC_C);
    if (r & 0x10000) CPU->cc |= CC_C;
    if (~(x16 ^ y16) & (x16 ^ r) & 0x8000) CPU->cc |= CC_V;
    nz16(h, (uint16_t)r);
    return (uint16_t)r;
}

static uint16_t sub16(CPU_HOST *h, uint16_t x16, uint16_t y16)
{
    uint32_t r = (uint32_t)x16 - y16;
    CPU->cc &= (uint8_t)~(CC_N | CC_Z | CC_V | CC_C);
    if (r & 0x10000) CPU->cc |= CC_C;
    if ((x16 ^ y16) & (x16 ^ r) & 0x8000) CPU->cc |= CC_V;
    nz16(h, (uint16_t)r);
    return (uint16_t)r;
}

/* the read-modify-write family, by the opcode's low nibble */
static uint8_t rmw(CPU_HOST *h, int op, uint8_t v)
{
    uint8_t r = v, c;
    switch (op & 0x0F) {
    case 0x2: /* undocumented: NEG if C clear, else COM */
        if (CPU->cc & CC_C) goto com;
        /* fall through */
    case 0x0: case 0x1: /* NEG */
        r = (uint8_t)(0 - v);
        CPU->cc &= (uint8_t)~(CC_N | CC_Z | CC_V | CC_C);
        if (v == 0x80) CPU->cc |= CC_V;
        if (v != 0) CPU->cc |= CC_C;
        nz8(h, r);
        break;
    case 0x3: /* COM */
    com:
        r = (uint8_t)~v;
        CPU->cc &= (uint8_t)~CC_V;
        CPU->cc |= CC_C;
        nz8(h, r);
        break;
    case 0x4: case 0x5: /* LSR */
        CPU->cc &= (uint8_t)~(CC_N | CC_Z | CC_C);
        if (v & 1) CPU->cc |= CC_C;
        r = (uint8_t)(v >> 1);
        if (!r) CPU->cc |= CC_Z;
        break;
    case 0x6: /* ROR */
        c = (CPU->cc & CC_C) ? 0x80 : 0;
        CPU->cc &= (uint8_t)~CC_C;
        if (v & 1) CPU->cc |= CC_C;
        r = (uint8_t)((v >> 1) | c);
        nz8(h, r);
        break;
    case 0x7: /* ASR */
        CPU->cc &= (uint8_t)~CC_C;
        if (v & 1) CPU->cc |= CC_C;
        r = (uint8_t)((v >> 1) | (v & 0x80));
        nz8(h, r);
        break;
    case 0x8: /* ASL/LSL */
        r = (uint8_t)(v << 1);
        CPU->cc &= (uint8_t)~(CC_V | CC_C);
        if (v & 0x80) CPU->cc |= CC_C;
        if ((v ^ (v << 1)) & 0x80) CPU->cc |= CC_V;
        nz8(h, r);
        break;
    case 0x9: /* ROL */
        c = (CPU->cc & CC_C) ? 1 : 0;
        r = (uint8_t)((v << 1) | c);
        CPU->cc &= (uint8_t)~(CC_V | CC_C);
        if (v & 0x80) CPU->cc |= CC_C;
        if ((v ^ (v << 1)) & 0x80) CPU->cc |= CC_V;
        nz8(h, r);
        break;
    case 0xA: case 0xB: /* DEC */
        r = (uint8_t)(v - 1);
        CPU->cc &= (uint8_t)~CC_V;
        if (v == 0x80) CPU->cc |= CC_V;
        nz8(h, r);
        break;
    case 0xC: /* INC */
        r = (uint8_t)(v + 1);
        CPU->cc &= (uint8_t)~CC_V;
        if (v == 0x7F) CPU->cc |= CC_V;
        nz8(h, r);
        break;
    case 0xD: /* TST */
        CPU->cc &= (uint8_t)~CC_V;
        nz8(h, v);
        break;
    case 0xF: /* CLR */
        r = 0;
        CPU->cc &= (uint8_t)~(CC_N | CC_V | CC_C);
        CPU->cc |= CC_Z;
        break;
    default: break; /* 0xE is JMP, handled by the caller */
    }
    return r;
}

static inline int cpu_cond(CPU_HOST *h, int c)
{
    const int C = (CPU->cc & CC_C) != 0, Z = (CPU->cc & CC_Z) != 0, N = (CPU->cc & CC_N) != 0, V = (CPU->cc & CC_V) != 0;
    switch (c & 0xF) {
    case 0x0: return 1;
    case 0x1: return 0;
    case 0x2: return !(C || Z);
    case 0x3: return C || Z;
    case 0x4: return !C;
    case 0x5: return C;
    case 0x6: return !Z;
    case 0x7: return Z;
    case 0x8: return !V;
    case 0x9: return V;
    case 0xA: return !N;
    case 0xB: return N;
    case 0xC: return N == V;
    case 0xD: return N != V;
    case 0xE: return !Z && (N == V);
    default:  return Z || (N != V);
    }
}

/* ------------------------------------------------------------------ addressing */
static inline uint16_t *idx_reg(CPU_HOST *h, int r)
{
    switch (r & 3) {
    case 0: return &CPU->x;
    case 1: return &CPU->y;
    case 2: return &CPU->u;
    default: return &CPU->s;
    }
}

static inline uint16_t ea_direct(CPU_HOST *h) { uint16_t lo = cpu_fetch(h); return (uint16_t)(CPU->dp << 8 | lo); }
static inline uint16_t ea_extended(CPU_HOST *h) { return cpu_fetch16(h); }

static uint16_t ea_indexed(CPU_HOST *h)
{
    const uint8_t pb = cpu_fetch(h);
    uint16_t *R = idx_reg(h, pb >> 5);
    uint16_t ea = 0;
    if (!(pb & 0x80)) { /* 5-bit offset */
        int off = pb & 0x1F;
        if (off & 0x10) off -= 0x20;
        CPU->extra += 1;
        return (uint16_t)(*R + off);
    }
    switch (pb & 0x0F) {
    case 0x0: ea = *R; (*R)++; CPU->extra += 2; break;
    case 0x1: ea = *R; *R += 2; CPU->extra += 3; break;
    case 0x2: (*R)--; ea = *R; CPU->extra += 2; break;
    case 0x3: *R -= 2; ea = *R; CPU->extra += 3; break;
    case 0x4: ea = *R; break;
    case 0x5: ea = (uint16_t)(*R + (int8_t)CPU->b); CPU->extra += 1; break;
    case 0x6: ea = (uint16_t)(*R + (int8_t)CPU->a); CPU->extra += 1; break;
    case 0x8: { int8_t o = (int8_t)cpu_fetch(h); ea = (uint16_t)(*R + o); CPU->extra += 1; break; }
    case 0x9: { uint16_t o = cpu_fetch16(h); ea = (uint16_t)(*R + o); CPU->extra += 4; break; }
    case 0xB: ea = (uint16_t)(*R + D_()); CPU->extra += 4; break;
    case 0xC: { int8_t o = (int8_t)cpu_fetch(h); ea = (uint16_t)(CPU->pc + o); CPU->extra += 1; break; }
    case 0xD: { uint16_t o = cpu_fetch16(h); ea = (uint16_t)(CPU->pc + o); CPU->extra += 5; break; }
    case 0xF: ea = cpu_fetch16(h); CPU->extra += 2; break;
    default: ea = *R; break; /* 0x7, 0xA, 0xE: undefined */
    }
    if (pb & 0x10) {
        ea = cpu_rd16(h, ea);
        CPU->extra += 3;
    }
    return ea;
}

static inline uint16_t ea_mode(CPU_HOST *h, int mode)   /* 1 direct, 2 indexed, 3 extended */
{
    switch (mode) {
    case 1: return ea_direct(h);
    case 2: return ea_indexed(h);
    default: return ea_extended(h);
    }
}

/* ------------------------------------------------------------------ registers */
static uint16_t get_reg(CPU_HOST *h, int code)
{
    switch (code & 0xF) {
    case 0x0: return D_();
    case 0x1: return CPU->x;
    case 0x2: return CPU->y;
    case 0x3: return CPU->u;
    case 0x4: return CPU->s;
    case 0x5: return CPU->pc;
    case 0x8: return (uint16_t)(0xFF00 | CPU->a);
    case 0x9: return (uint16_t)(0xFF00 | CPU->b);
    case 0xA: return (uint16_t)(0xFF00 | CPU->cc);
    case 0xB: return (uint16_t)(0xFF00 | CPU->dp);
    default:  return 0xFFFF;
    }
}

static void set_reg(CPU_HOST *h, int code, uint16_t v)
{
    switch (code & 0xF) {
    case 0x0: cpu_set_d(h, v); break;
    case 0x1: CPU->x = v; break;
    case 0x2: CPU->y = v; break;
    case 0x3: CPU->u = v; break;
    case 0x4: CPU->s = v; CPU->nmi_armed = 1; break;
    case 0x5: CPU->pc = v; break;
    case 0x8: CPU->a = (uint8_t)v; break;
    case 0x9: CPU->b = (uint8_t)v; break;
    case 0xA: CPU->cc = (uint8_t)v; break;
    case 0xB: CPU->dp = (uint8_t)v; break;
    default: break;
    }
}

static inline void psh8(CPU_HOST *h, int use_u, uint8_t v) { if (use_u) cpu_push_u(h, v); else cpu_push_s(h, v); }
static inline void psh16(CPU_HOST *h, int use_u, uint16_t v) { psh8(h, use_u, (uint8_t)v); psh8(h, use_u, (uint8_t)(v >> 8)); }
static inline uint8_t pul8(CPU_HOST *h, int use_u) { return use_u ? cpu_pull_u(h) : cpu_pull_s(h); }
static inline uint16_t pul16(CPU_HOST *h, int use_u) { uint16_t hi = pul8(h, use_u); return (uint16_t)(hi << 8 | pul8(h, use_u)); }

static int psh(CPU_HOST *h, int use_u, uint8_t m)
{
    int n = 0;
    if (m & 0x80) { psh16(h, use_u, CPU->pc); n += 2; }
    if (m & 0x40) { psh16(h, use_u, use_u ? CPU->s : CPU->u); n += 2; }
    if (m & 0x20) { psh16(h, use_u, CPU->y); n += 2; }
    if (m & 0x10) { psh16(h, use_u, CPU->x); n += 2; }
    if (m & 0x08) { psh8(h, use_u, CPU->dp); n++; }
    if (m & 0x04) { psh8(h, use_u, CPU->b); n++; }
    if (m & 0x02) { psh8(h, use_u, CPU->a); n++; }
    if (m & 0x01) { psh8(h, use_u, CPU->cc); n++; }
    return n;
}

static int pul(CPU_HOST *h, int use_u, uint8_t m)
{
    int n = 0;
    if (m & 0x01) { CPU->cc = pul8(h, use_u); n++; }
    if (m & 0x02) { CPU->a = pul8(h, use_u); n++; }
    if (m & 0x04) { CPU->b = pul8(h, use_u); n++; }
    if (m & 0x08) { CPU->dp = pul8(h, use_u); n++; }
    if (m & 0x10) { CPU->x = pul16(h, use_u); n += 2; }
    if (m & 0x20) { CPU->y = pul16(h, use_u); n += 2; }
    if (m & 0x40) {
        if (use_u) { CPU->s = pul16(h, use_u); CPU->nmi_armed = 1; }
        else CPU->u = pul16(h, use_u);
        n += 2;
    }
    if (m & 0x80) { CPU->pc = pul16(h, use_u); n += 2; }
    return n;
}

static void push_all(CPU_HOST *h)
{
    cpu_push_s16(h, CPU->pc);
    cpu_push_s16(h, CPU->u);
    cpu_push_s16(h, CPU->y);
    cpu_push_s16(h, CPU->x);
    cpu_push_s(h, CPU->dp);
    cpu_push_s(h, CPU->b);
    cpu_push_s(h, CPU->a);
    cpu_push_s(h, CPU->cc);
}

static inline int take_interrupts(CPU_HOST *h)
{
    int cyc;
    if (!(CPU->nmi_pending | CPU->firq | CPU->irq)) return 0;   /* (most instructions: no line up) */
    if (CPU->nmi_pending && CPU->nmi_armed) {
        CPU->nmi_pending = 0;
        cyc = 19;
        if (!CPU->cwai) { CPU->cc |= CC_E; push_all(h); } else cyc = 7;
        CPU->cwai = CPU->sync = 0;
        CPU->cc |= CC_I | CC_F;
        CPU->pc = cpu_rd16(h, 0xFFFC);
        return cyc;
    }
    if (CPU->firq && !(CPU->cc & CC_F)) {
        cyc = 10;
        if (!CPU->cwai) {
            CPU->cc &= (uint8_t)~CC_E;
            cpu_push_s16(h, CPU->pc);
            cpu_push_s(h, CPU->cc);
        } else cyc = 7;
        CPU->cwai = CPU->sync = 0;
        CPU->cc |= CC_I | CC_F;
        CPU->pc = cpu_rd16(h, 0xFFF6);
        return cyc;
    }
    if (CPU->irq && !(CPU->cc & CC_I)) {
        cyc = 19;
        if (!CPU->cwai) { CPU->cc |= CC_E; push_all(h); } else cyc = 7;
        CPU->cwai = CPU->sync = 0;
        CPU->cc |= CC_I;
        CPU->pc = cpu_rd16(h, 0xFFF8);
        return cyc;
    }
    return 0;
}

/* ------------------------------------------------------------------ execution */
/* base cycle counts for page-0 opcodes (indexed modes add `extra`) */
static const uint8_t kCycles0[256] = {
 /* 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 3, 6, /* 0x */
    0, 0, 2, 4, 2, 2, 5, 9, 2, 2, 3, 2, 3, 2, 8, 6, /* 1x */
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, /* 2x */
    4, 4, 4, 4, 5, 5, 5, 5, 2, 5, 3, 6,20,11, 2,19, /* 3x */
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, /* 4x */
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, /* 5x */
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 3, 6, /* 6x */
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4, 7, /* 7x */
    2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 4, 7, 3, 2, /* 8x */
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 6, 7, 5, 5, /* 9x */
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 6, 7, 5, 5, /* Ax */
    5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 5, 7, 8, 6, 6, /* Bx */
    2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 3, 2, /* Cx */
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, /* Dx */
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, /* Ex */
    5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, /* Fx */
};

static int cpu_illegal(CPU_HOST *h, int back)
{
    CPU->illegal = 1;
    CPU->illegal_pc = (uint16_t)(CPU->pc - back);
    return 2;
}

static int exec0(CPU_HOST *h, uint8_t op)
{
    const int cyc = kCycles0[op];
    const int hi = op >> 4;

    /* read-modify-write: memory (0x, 6x, 7x) and accumulators (4x, 5x) */
    if (hi == 0x0 || hi == 0x6 || hi == 0x7) {
        const uint16_t ea = hi == 0 ? ea_direct(h) : hi == 6 ? ea_indexed(h) : ea_extended(h);
        uint8_t v, r;
        if ((op & 0xF) == 0xE) { CPU->pc = ea; return cyc; }   /* JMP */
        v = RD(ea);
        r = rmw(h, op, v);
        if ((op & 0xF) != 0xD) WR(ea, r);                      /* TST doesn't write */
        return cyc;
    }
    if (hi == 0x4) { if ((op & 0xF) != 0xE) CPU->a = rmw(h, op, CPU->a); return cyc; }
    if (hi == 0x5) { if ((op & 0xF) != 0xE) CPU->b = rmw(h, op, CPU->b); return cyc; }

    if (hi == 0x2) {   /* branches */
        const int8_t off = (int8_t)cpu_fetch(h);
        if (cpu_cond(h, op)) CPU->pc = (uint16_t)(CPU->pc + off);
        return cyc;
    }

    if (hi >= 0x8) {   /* accumulator and 16-bit operations */
        const int isB = hi >= 0xC;
        const int mode = hi & 3;   /* 0 immediate, 1 direct, 2 indexed, 3 extended */
        const int lo = op & 0xF;
        uint8_t *acc = isB ? &CPU->b : &CPU->a;
        const int wide = lo == 0x3 || lo == 0xC || lo == 0xE || (isB && lo == 0xD && mode != 0) || lo == 0xF;
        uint16_t e;
        uint8_t v;

        if (lo == 0x7) {   /* STA / STB */
            if (mode == 0) return cpu_illegal(h, 1);
            e = ea_mode(h, mode);
            WR(e, *acc);
            CPU->cc &= (uint8_t)~CC_V;
            nz8(h, *acc);
            return cyc;
        }
        if (!isB && lo == 0xD) {   /* BSR / JSR */
            if (mode == 0) {
                const int8_t off = (int8_t)cpu_fetch(h);
                cpu_push_s16(h, CPU->pc);
                CPU->pc = (uint16_t)(CPU->pc + off);
                return cyc;
            }
            e = ea_mode(h, mode);
            cpu_push_s16(h, CPU->pc);
            CPU->pc = e;
            return cyc;
        }
        if (!isB && lo == 0xF) {   /* STX */
            if (mode == 0) return cpu_illegal(h, 1);
            e = ea_mode(h, mode);
            cpu_wr16(h, e, CPU->x);
            CPU->cc &= (uint8_t)~CC_V;
            nz16(h, CPU->x);
            return cyc;
        }
        if (isB && lo == 0xD) {   /* STD */
            if (mode == 0) return cpu_illegal(h, 1);
            e = ea_mode(h, mode);
            cpu_wr16(h, e, D_());
            CPU->cc &= (uint8_t)~CC_V;
            nz16(h, D_());
            return cyc;
        }
        if (isB && lo == 0xF) {   /* STU */
            if (mode == 0) return cpu_illegal(h, 1);
            e = ea_mode(h, mode);
            cpu_wr16(h, e, CPU->u);
            CPU->cc &= (uint8_t)~CC_V;
            nz16(h, CPU->u);
            return cyc;
        }

        if (wide) {
            uint16_t w = mode == 0 ? cpu_fetch16(h) : cpu_rd16(h, ea_mode(h, mode));
            switch (lo) {
            case 0x3: cpu_set_d(h, isB ? add16(h, D_(), w) : sub16(h, D_(), w)); break;          /* ADDD / SUBD */
            case 0xC:
                if (isB) { cpu_set_d(h, w); CPU->cc &= (uint8_t)~CC_V; nz16(h, w); }       /* LDD */
                else sub16(h, CPU->x, w);                                                   /* CMPX */
                break;
            case 0xE:                                                                       /* LDU / LDX */
                if (isB) CPU->u = w; else CPU->x = w;
                CPU->cc &= (uint8_t)~CC_V;
                nz16(h, w);
                break;
            default: break;
            }
            return cyc;
        }

        v = mode == 0 ? cpu_fetch(h) : RD(ea_mode(h, mode));
        switch (lo) {
        case 0x0: *acc = sub8(h, *acc, v, 0); break;                                   /* SUB */
        case 0x1: sub8(h, *acc, v, 0); break;                                          /* CMP */
        case 0x2: *acc = sub8(h, *acc, v, (CPU->cc & CC_C) ? 1 : 0); break;            /* SBC */
        case 0x4: *acc &= v; CPU->cc &= (uint8_t)~CC_V; nz8(h, *acc); break;           /* AND */
        case 0x5: CPU->cc &= (uint8_t)~CC_V; nz8(h, (uint8_t)(*acc & v)); break;       /* BIT */
        case 0x6: *acc = v; CPU->cc &= (uint8_t)~CC_V; nz8(h, *acc); break;            /* LD */
        case 0x8: *acc ^= v; CPU->cc &= (uint8_t)~CC_V; nz8(h, *acc); break;           /* EOR */
        case 0x9: *acc = add8(h, *acc, v, (CPU->cc & CC_C) ? 1 : 0); break;            /* ADC */
        case 0xA: *acc |= v; CPU->cc &= (uint8_t)~CC_V; nz8(h, *acc); break;           /* OR */
        case 0xB: *acc = add8(h, *acc, v, 0); break;                                   /* ADD */
        default: cpu_illegal(h, 1); break;
        }
        return cyc;
    }

    switch (op) {   /* 1x / 3x */
    case 0x12: return cyc;                                     /* NOP */
    case 0x13: CPU->sync = 1; return cyc;                      /* SYNC */
    case 0x16: { uint16_t o = cpu_fetch16(h); CPU->pc = (uint16_t)(CPU->pc + o); return cyc; }   /* LBRA */
    case 0x17: { uint16_t o = cpu_fetch16(h); cpu_push_s16(h, CPU->pc); CPU->pc = (uint16_t)(CPU->pc + o); return cyc; }   /* LBSR */
    case 0x19: {   /* DAA */
        const uint8_t msn = CPU->a & 0xF0, lsn = CPU->a & 0x0F;
        unsigned cf = 0, r;
        if (lsn > 9 || (CPU->cc & CC_H)) cf |= 0x06;
        if (msn > 0x80 && lsn > 9) cf |= 0x60;
        if (msn > 0x90 || (CPU->cc & CC_C)) cf |= 0x60;
        r = (unsigned)CPU->a + cf;
        CPU->cc &= (uint8_t)~CC_V;
        if (r & 0x100) CPU->cc |= CC_C;
        CPU->a = (uint8_t)r;
        nz8(h, CPU->a);
        return cyc;
    }
    case 0x1A: CPU->cc |= cpu_fetch(h); return cyc;            /* ORCC */
    case 0x1C: CPU->cc &= cpu_fetch(h); return cyc;            /* ANDCC */
    case 0x1D: CPU->a = (CPU->b & 0x80) ? 0xFF : 0x00; CPU->cc &= (uint8_t)~CC_V; nz16(h, D_()); return cyc;   /* SEX */
    case 0x1E: {   /* EXG */
        const uint8_t pb = cpu_fetch(h);
        const uint16_t r1 = get_reg(h, pb >> 4), r2 = get_reg(h, pb & 0xF);
        set_reg(h, pb >> 4, r2);
        set_reg(h, pb & 0xF, r1);
        return cyc;
    }
    case 0x1F: { const uint8_t pb = cpu_fetch(h); set_reg(h, pb & 0xF, get_reg(h, pb >> 4)); return cyc; }   /* TFR */
    case 0x30: CPU->x = ea_indexed(h); CPU->cc &= (uint8_t)~CC_Z; if (!CPU->x) CPU->cc |= CC_Z; return cyc;   /* LEAX */
    case 0x31: CPU->y = ea_indexed(h); CPU->cc &= (uint8_t)~CC_Z; if (!CPU->y) CPU->cc |= CC_Z; return cyc;   /* LEAY */
    case 0x32: CPU->s = ea_indexed(h); CPU->nmi_armed = 1; return cyc;   /* LEAS */
    case 0x33: CPU->u = ea_indexed(h); return cyc;                       /* LEAU */
    case 0x34: return cyc + psh(h, 0, cpu_fetch(h));
    case 0x35: return cyc + pul(h, 0, cpu_fetch(h));
    case 0x36: return cyc + psh(h, 1, cpu_fetch(h));
    case 0x37: return cyc + pul(h, 1, cpu_fetch(h));
    case 0x39: CPU->pc = cpu_pull_s16(h); return cyc;          /* RTS */
    case 0x3A: CPU->x = (uint16_t)(CPU->x + CPU->b); return cyc;   /* ABX */
    case 0x3B:   /* RTI */
        CPU->cc = cpu_pull_s(h);
        if (CPU->cc & CC_E) {
            CPU->a = cpu_pull_s(h);
            CPU->b = cpu_pull_s(h);
            CPU->dp = cpu_pull_s(h);
            CPU->x = cpu_pull_s16(h);
            CPU->y = cpu_pull_s16(h);
            CPU->u = cpu_pull_s16(h);
            CPU->pc = cpu_pull_s16(h);
            return 15;
        }
        CPU->pc = cpu_pull_s16(h);
        return 6;
    case 0x3C:   /* CWAI */
        CPU->cc &= cpu_fetch(h);
        CPU->cc |= CC_E;
        push_all(h);
        CPU->cwai = 1;
        return cyc;
    case 0x3D: {   /* MUL */
        const uint16_t r = (uint16_t)(CPU->a * CPU->b);
        cpu_set_d(h, r);
        CPU->cc &= (uint8_t)~(CC_Z | CC_C);
        if (!r) CPU->cc |= CC_Z;
        if (r & 0x80) CPU->cc |= CC_C;
        return cyc;
    }
    case 0x3F:   /* SWI */
        CPU->cc |= CC_E;
        push_all(h);
        CPU->cc |= CC_I | CC_F;
        CPU->pc = cpu_rd16(h, 0xFFFA);
        return cyc;
    default:
        return cpu_illegal(h, 1);
    }
}

static int exec10(CPU_HOST *h, uint8_t op)
{
    static const int base[4][2] = {{5, 4}, {7, 6}, {7, 6}, {8, 7}};   /* [mode][0: compare, 1: load/store] */
    const int hi = op >> 4, lo = op & 0xF, mode = hi & 3;
    uint16_t v, e;
    if (op >= 0x21 && op <= 0x2F) {   /* long conditional branches */
        const uint16_t o = cpu_fetch16(h);
        if (cpu_cond(h, op)) { CPU->pc = (uint16_t)(CPU->pc + o); return 6; }
        return 5;
    }
    if (op == 0x3F) { CPU->cc |= CC_E; push_all(h); CPU->pc = cpu_rd16(h, 0xFFF4); return 20; }   /* SWI2 */

    if (hi >= 0x8 && hi <= 0xB) {
        switch (lo) {
        case 0x3: v = mode ? cpu_rd16(h, ea_mode(h, mode)) : cpu_fetch16(h); sub16(h, D_(), v); return base[mode][0];   /* CMPD */
        case 0xC: v = mode ? cpu_rd16(h, ea_mode(h, mode)) : cpu_fetch16(h); sub16(h, CPU->y, v); return base[mode][0]; /* CMPY */
        case 0xE:                                                                                                      /* LDY */
            v = mode ? cpu_rd16(h, ea_mode(h, mode)) : cpu_fetch16(h);
            CPU->y = v; CPU->cc &= (uint8_t)~CC_V; nz16(h, v);
            return base[mode][1];
        case 0xF:                                                                                                      /* STY */
            if (!mode) break;
            e = ea_mode(h, mode);
            cpu_wr16(h, e, CPU->y); CPU->cc &= (uint8_t)~CC_V; nz16(h, CPU->y);
            return base[mode][1];
        default: break;
        }
    } else if (hi >= 0xC) {
        switch (lo) {
        case 0xE:                                                                                                      /* LDS */
            v = mode ? cpu_rd16(h, ea_mode(h, mode)) : cpu_fetch16(h);
            CPU->s = v; CPU->nmi_armed = 1; CPU->cc &= (uint8_t)~CC_V; nz16(h, v);
            return base[mode][1];
        case 0xF:                                                                                                      /* STS */
            if (!mode) break;
            e = ea_mode(h, mode);
            cpu_wr16(h, e, CPU->s); CPU->cc &= (uint8_t)~CC_V; nz16(h, CPU->s);
            return base[mode][1];
        default: break;
        }
    }
    return cpu_illegal(h, 2);
}

static int exec11(CPU_HOST *h, uint8_t op)
{
    static const int c[4] = {5, 7, 7, 8};
    const int hi = op >> 4, lo = op & 0xF, mode = hi & 3;
    if (op == 0x3F) { CPU->cc |= CC_E; push_all(h); CPU->pc = cpu_rd16(h, 0xFFF2); return 20; }   /* SWI3 */
    if (hi >= 0x8 && hi <= 0xB && (lo == 0x3 || lo == 0xC)) {   /* CMPU / CMPS */
        const uint16_t v = mode == 0 ? cpu_fetch16(h) : cpu_rd16(h, ea_mode(h, mode));
        sub16(h, lo == 0x3 ? CPU->u : CPU->s, v);
        return c[mode];
    }
    return cpu_illegal(h, 2);
}

/* one instruction, or one interrupt taken; returns the cycles used */
static int cpu_step(CPU_HOST *h)
{
    int cyc;
    uint8_t op;
    if (!(CPU->sync | CPU->cwai | CPU->nmi_pending | CPU->firq | CPU->irq)) goto run;   /* (most instructions) */
    if (CPU->sync) {   /* SYNC: wait for any interrupt line; a masked one just resumes */
        if (CPU->nmi_pending || CPU->firq || CPU->irq) {
            CPU->sync = 0;
            if ((cyc = take_interrupts(h))) { CPU->total += (uint64_t)cyc; return cyc; }
        } else { CPU->total += 1u; return 1; }
    }
    if ((cyc = take_interrupts(h))) { CPU->total += (uint64_t)cyc; return cyc; }
    if (CPU->cwai) { CPU->total += 1u; return 1; }

run:
    CPU->extra = 0;
    op = cpu_fetch(h);
    if (op == 0x10) cyc = exec10(h, cpu_fetch(h));
    else if (op == 0x11) cyc = exec11(h, cpu_fetch(h));
    else cyc = exec0(h, op);
    cyc += CPU->extra;
    CPU->total += (uint64_t)cyc;
    return cyc;
}

#undef CPU
#undef RD
#undef WR
#undef D_
