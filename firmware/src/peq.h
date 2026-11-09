/* SPDX-License-Identifier: GPL-3.0-only */
/* SMK-37: the 5-band parametric EQ, the last filter of the master bus before the MASTER volume (fx.c mix_block, after
 * the DJ filter). No hardware here: tests/peq_test.c runs this file as it is.
 *
 * Five bands, each one of BELL, LOW SHELF, HIGH SHELF, LOW CUT (high-pass), HIGH CUT (low-pass) or OFF, its FREQ on
 * 117 steps of 1/40 decade (20 Hz .. 16 kHz, peq_tab.h), GAIN +-12 dB in half dB (the bell and the shelves), Q 0.3 .. 8
 * (29 steps; the shelves' slope, the cuts' resonance). Each band is a Simper (TPT) state-variable filter,
 * y = m0 x + m1 v1 + m2 v2 (Simper's "SvfLinearTrapOptimised2" set of m0..m2), worked out from the tables with integers
 * only (eq_calc). Every product in the audio loop is 32 x 32 -> 64 bit, one multiply on the target; the states carry
 * 8 fractional bits (without them a low band has a dead band of a few LSB in which its state never decays).
 *
 * Moves are smooth: a band's FREQ, GAIN and Q step one table step a block towards what the dials ask (a full GAIN
 * swing ~18 ms), its TYPE changes through a fade (out with the old filter, in with the new, ~6 ms each), and EQ OFF
 * fades every band out. A band that is OFF, or a bell / shelf at 0 dB, is out of the path (no cost, its states
 * cleared); with nothing in the path the signal is untouched, bit for bit.
 *
 * eq_p[][] (the dials) and eq_off are written by the UI on the main loop and read by the audio ISR at the start of a
 * block: 16-bit values, no torn read. The rest of the screen's helpers (the response curve) are here too, so the
 * host test checks them against the filter itself. */
#pragma once
#include <stdint.h>
#include "peq_tab.h"

#define EQ_N 5u                                  /* bands */
enum { EQT_OFF, EQT_BELL, EQT_LSHF, EQT_HSHF, EQT_LCUT, EQT_HCUT, EQT_N };
enum { EQP_TYPE, EQP_FREQ, EQP_GAIN, EQP_Q, EQP_N };   /* a band's four values (the four dials) */
#define EQ_HMAX 24                               /* GAIN: -24 .. +24 half dB */
#define EQ_FRAC 8                                /* the states' fractional bits */
#define EQ_SMAX ((int64_t)1 << 30)               /* a state's limit (+-2^22 in samples) */
#define EQ_XMAX ((int32_t)1 << 21)               /* an input sample's (64 x full scale; -2^22 .. -3 x 2^20 would read as
                                                  * ROM addresses to tools/build.py's check) */
#define EQ_WSTEP 32u                             /* the fade: 16 / 128 a block (8 blocks, ~6 ms) */

typedef struct {
    int32_t g, k;                                /* tan(pi f / FS) (the shelves': scaled by sqrt A), damping; Q28 */
    int32_t a1, a2, a3, m0, m1, m2;              /* the SVF, Q28 */
} eqc_t;

typedef struct {
    eqc_t c;
    int32_t s[4];                                /* left ic1, ic2, right ic1, ic2 (Q8) */
    uint8_t type, f, q, w;                       /* now: the filter, its FREQ / Q steps, the fade (0 .. 128 = all in) */
    int8_t e;                                    /* GAIN now, half dB */
} eqb_t;

/* the defaults: flat, five bands spread out: LOW SHELF 100 Hz, BELL 250 Hz / 1 kHz / 4 kHz, HIGH SHELF 10 kHz */
static const int16_t EQ_DEF[EQ_N][EQP_N] = {
    {EQT_LSHF, 28, 0, 7}, {EQT_BELL, 44, 0, 10}, {EQT_BELL, 68, 0, 10}, {EQT_BELL, 92, 0, 10}, {EQT_HSHF, 108, 0, 7}};

static int16_t eq_p[EQ_N][EQP_N] = {
    {EQT_LSHF, 28, 0, 7}, {EQT_BELL, 44, 0, 10}, {EQT_BELL, 68, 0, 10}, {EQT_BELL, 92, 0, 10}, {EQT_HSHF, 108, 0, 7}};
static uint8_t eq_off;                           /* 1: EQ OFF (faded out, the bands kept) */
static eqb_t eq_b[EQ_N];
static uint8_t eq_live;                          /* the bands in the path in the last block */
static uint8_t eq_cur;                           /* the UI's band (0..4) */

static inline int32_t eq_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int eq_has_gain(uint32_t type) { return type == EQT_BELL || type == EQT_LSHF || type == EQT_HSHF; }
/* a band's values as the dials hold them, in range */
static void eq_get(uint32_t b, uint32_t *type, uint32_t *f, int32_t *e, uint32_t *q)
{
    *type = (uint32_t)eq_clamp(eq_p[b][EQP_TYPE], 0, EQT_N - 1);
    *f = (uint32_t)eq_clamp(eq_p[b][EQP_FREQ], 0, EQ_NF - 1);
    *e = eq_clamp(eq_p[b][EQP_GAIN], -EQ_HMAX, EQ_HMAX);
    *q = (uint32_t)eq_clamp(eq_p[b][EQP_Q], 0, EQ_NQ - 1);
}
/* a band that changes nothing: OFF, or a bell / shelf at 0 dB */
static inline int eq_flat(uint32_t type, int32_t e) { return type == EQT_OFF || (eq_has_gain(type) && e == 0); }

static uint32_t eq_div52(uint32_t den)           /* 2^52 / den (den >= 2^24) by shift and subtract: no 64-bit divide here */
{
    uint32_t r = 1, q = 0, n;
    for (n = 0; n < 52u; n++) {
        uint32_t top = r >> 31;
        r <<= 1;
        q <<= 1;
        if (top || r >= den) {
            r -= den;
            q |= 1u;
        }
    }
    return q;
}

/* the coefficients of a band (Q28): A = 10^(dB / 40) (Simper: bell k = 1 / (Q A); shelves g / or * sqrt A) */
static void eq_calc(eqc_t *c, uint32_t type, uint32_t f, int32_t e, uint32_t q)
{
    const int64_t one = (int64_t)1 << 28;
    int64_t G = EQ_G[f], QK = EQ_QK[q], A = EQ_P[2 * e + 96], A2 = EQ_P[4 * e + 96];
    int64_t g = G, k = QK, m0 = one, m1 = 0, m2 = 0;
    uint32_t den;
    switch (type) {
    case EQT_BELL:
        k = (QK * EQ_P[-2 * e + 96]) >> 28;                 /* 1 / (Q A) */
        m1 = (k * (A2 - one)) >> 28;
        break;
    case EQT_LSHF:
        g = (G * EQ_P[-e + 96]) >> 28;                      /* g / sqrt A */
        m1 = (k * (A - one)) >> 28;
        m2 = A2 - one;
        break;
    case EQT_HSHF:
        g = (G * EQ_P[e + 96]) >> 28;                       /* g sqrt A */
        m0 = A2;
        m1 = (((k * (one - A)) >> 28) * A) >> 28;
        m2 = one - A2;
        break;
    case EQT_LCUT:
        m1 = -k;
        m2 = -one;
        break;
    case EQT_HCUT:
        m0 = 0;
        m2 = one;
        break;
    default:                                                /* OFF: a wire */
        m0 = one;
        break;
    }
    den = (uint32_t)(((int64_t)1 << 24) + ((g * (g + k)) >> 32));   /* 1 + g (g + k), Q24 (< 2^32: g < 3.3, k < 7) */
    c->g = (int32_t)g;
    c->k = (int32_t)k;
    c->a1 = (int32_t)eq_div52(den);
    c->a2 = (int32_t)((g * c->a1) >> 28);
    c->a3 = (int32_t)((g * c->a2) >> 28);
    c->m0 = (int32_t)m0;
    c->m1 = (int32_t)m1;
    c->m2 = (int32_t)m2;
}

#define EQ_M(a, b) ((int64_t)(int32_t)(a) * (int32_t)(b))   /* 32 x 32 -> 64: one multiply */
static inline int32_t eq_sat(int64_t v) { return (int32_t)(v < -EQ_SMAX ? -EQ_SMAX : v > EQ_SMAX ? EQ_SMAX : v); }
/* one channel through one band; w0 -> w1: the fade over the block, sample by sample (128 = all of the filter) */
static void eq_run(const eqc_t *c, int32_t *s, int32_t *x, uint32_t n, uint32_t w0, uint32_t w1)
{
    const int32_t a1 = c->a1, a2 = c->a2, a3 = c->a3, m0 = c->m0, m1 = c->m1, m2 = c->m2;
    const int64_t rnd = (int64_t)1 << 27, orn = (int64_t)1 << (27 + EQ_FRAC);
    const int fade = w0 < 128u || w1 < 128u;
    int32_t ic1 = s[0], ic2 = s[1], wq = (int32_t)(w0 << 16), dw = n ? (((int32_t)w1 - (int32_t)w0) << 16) / (int32_t)n : 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t xi = eq_clamp(x[i], -EQ_XMAX, EQ_XMAX), xs = xi << EQ_FRAC, v3 = eq_sat((int64_t)xs - ic2), v1, v2, y;
        v1 = eq_sat((EQ_M(a1, ic1) + EQ_M(a2, v3) + rnd) >> 28);
        v2 = eq_sat(ic2 + ((EQ_M(a2, ic1) + EQ_M(a3, v3) + rnd) >> 28));
        ic1 = eq_sat(2 * (int64_t)v1 - ic1);
        ic2 = eq_sat(2 * (int64_t)v2 - ic2);
        y = (int32_t)((EQ_M(m0, xs) + EQ_M(m1, v1) + EQ_M(m2, v2) + orn) >> (28 + EQ_FRAC));
        if (fade) {
            wq += dw;
            y = xi + (int32_t)(((int64_t)(y - xi) * wq) >> 23);
        }
        x[i] = y;
    }
    s[0] = ic1, s[1] = ic2;
}

static inline int32_t eq_toward(int32_t now, int32_t tgt) { return now + (tgt > now) - (tgt < now); }

/* l, r: n samples in place (the master mix, Q15 with headroom) */
static void eq_process(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t b, live = 0;
    for (b = 0; b < EQ_N; b++) {
        eqb_t *s = &eq_b[b];
        uint32_t tt, tf, tq, w0 = s->w, w1;
        int32_t te;
        eq_get(b, &tt, &tf, &te, &tq);
        if (eq_off)
            tt = EQT_OFF;
        if (s->type != tt) {                                /* a new TYPE: fade the old one out, then start the new */
            if (eq_flat(s->type, s->e))
                w0 = 0;                                     /* (nothing to fade) */
            w1 = w0 > EQ_WSTEP / 2u ? w0 - EQ_WSTEP / 2u : 0u;
        } else if (tt != EQT_OFF) {                         /* the same TYPE: a step a block towards the dials */
            int32_t f = eq_toward(s->f, (int32_t)tf), q = eq_toward(s->q, (int32_t)tq), e = eq_toward(s->e, te);
            if (f != s->f || q != s->q || e != s->e) {
                s->f = (uint8_t)f, s->q = (uint8_t)q, s->e = (int8_t)e;
                eq_calc(&s->c, tt, (uint32_t)f, e, (uint32_t)q);
            }
            w1 = w0 + EQ_WSTEP / 2u > 128u ? 128u : w0 + EQ_WSTEP / 2u;
        } else {
            w1 = 0;
        }
        if ((w0 || w1) && !eq_flat(s->type, s->e)) {       /* in the path, fading from w0 to w1 across the block */
            eq_run(&s->c, s->s, l, n, w0, w1);
            eq_run(&s->c, s->s + 2, r, n, w0, w1);
            live++;
        } else {                                            /* out of the path: from rest the next time */
            s->s[0] = s->s[1] = s->s[2] = s->s[3] = 0;
        }
        s->w = (uint8_t)w1;
        if (s->type != tt && !w1) {                         /* faded out: the new TYPE, from rest */
            s->type = (uint8_t)tt, s->f = (uint8_t)tf, s->q = (uint8_t)tq, s->e = (int8_t)te;
            s->s[0] = s->s[1] = s->s[2] = s->s[3] = 0;
            eq_calc(&s->c, tt, tf, te, tq);
        }
    }
    eq_live = (uint8_t)live;
}

/* ---- the screen's response curve (ui_peq.c): what the filter does at a column's frequency, from its coefficients.
 * The TPT SVF is the bilinear transform of the analogue prototype, so at t = tan(pi f / FS) its response is exactly
 * H = m0 + (m1 s g + m2 g^2) / (s^2 + k g s + g^2), s = j t (all over g^2: the analogue one at s / g). Integers:
 * t, g, k in Q20, the m in Q12, the products in Q40. */
static int32_t eq_log2(uint64_t v)              /* log2(v), Q12 (v >= 1) */
{
    uint32_t n = 0, top, i, fr;
    uint64_t u = v;
    while (u >> 1) {
        u >>= 1;
        n++;
    }
    top = (uint32_t)(n >= 12u ? v >> (n - 12u) : v << (12u - n)) & 4095u;   /* the 12 bits after the leading one */
    i = top >> 6, fr = top & 63u;
    return (int32_t)(n << 12) + EQ_L2[i] + (int32_t)(((EQ_L2[i + 1u] - EQ_L2[i]) * fr) >> 6);
}
static int32_t eq_log2sq(int64_t a, int64_t b) /* log2(a^2 + b^2), Q12 */
{
    uint64_t ua = (uint64_t)(a < 0 ? -a : a), ub = (uint64_t)(b < 0 ? -b : b), m = ua > ub ? ua : ub;
    uint32_t sh = 0;
    while ((m >> sh) >= ((uint64_t)1 << 30))
        sh++;
    ua >>= sh, ub >>= sh;
    return eq_log2(ua * ua + ub * ub + 1u) + (int32_t)(sh << 13);
}
/* a band's gain at the column x in 1/16 dB */
static int32_t eq_db16(const eqc_t *c, uint32_t type, uint32_t x)
{
    int64_t t = EQ_TX[x], g = c->g >> 8, k = c->k >> 8, m0 = c->m0 >> 16, m1 = c->m1 >> 16, m2 = c->m2 >> 16;
    int64_t gg = g * g, dre = gg - t * t, dim = ((k * g) >> 20) * t, gt = g * t, nre, nim;
    if (type == EQT_OFF)
        return 0;
    nre = ((m0 * dre) >> 12) + ((m2 * gg) >> 12);
    nim = ((m0 * dim) >> 12) + ((m1 * gt) >> 12);
    if (!nre && !nim)
        return -96 * 16;
    return (int32_t)(((int64_t)(eq_log2sq(nre, nim) - eq_log2sq(dre, dim)) * 771) >> 16);   /* x 10 log10(2) x 16 / 4096 */
}
