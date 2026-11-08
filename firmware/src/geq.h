/* SPDX-License-Identifier: GPL-3.0-only */
/* SMK-37: the 16-band graphic EQ, the last filter of the master bus before the MASTER volume (fx.c mix_block, after the
 * DJ filter). No hardware here: tests/geq_test.c runs this file as it is.
 *
 * Sixteen bells, 40 Hz .. 16 kHz about 0.58 octave apart, +-12 dB in half-dB steps. Each bell is a Simper (TPT)
 * state-variable filter: y = x + (A^2 - 1) k v1 with k = 1 / (A Q), A = 10^(dB / 40). The 64-bit products keep the
 * low bells exact (a 40 Hz bell has a3 of 8e-6), and the states carry 8 fractional bits (without them a low bell has
 * a dead band of a few LSB in which its state never decays: a DC offset left after the sound stops).
 * The bells run in series and overlap, so the sliders go through the inverse of their interaction matrix first
 * (eq_solve; tools/gen_eq16.py): equal sliders are flat across the bands and a slider is its dB at its own centre.
 * Only the bells that are not at 0 dB run; with every slider at 0 (or EQ OFF) the EQ is out of the path and the
 * signal is untouched. A bell's gain moves one half dB a block towards what the sliders ask (no zipper, no click:
 * 12 dB take ~18 ms), and a bell that has reached 0 dB restarts from silence.
 *
 * eq_t[] (the sliders, half dB) and eq_off are written by the UI on the main loop and read by the audio ISR at the start
 * of a block (eq_update): a slider is a 16-bit value, so a torn read is impossible; the matrix is solved in the ISR. */
#pragma once
#include <stdint.h>
#include "geq_tab.h"

#define EQ_N 16u
#define EQ_HMAX 24                               /* a slider: -24 .. +24 half dB */
#define EQ_FRAC 8                                /* the states' fractional bits */
#define EQ_SMAX ((int64_t)1 << 30)               /* a state's limit (+-2^22 in samples) */

typedef struct {
    int32_t a1, a2, a3;                          /* the SVF's coefficients for e, Q28 */
    int32_t c;                                   /* (A^2 - 1) k for e, Q28 */
    int32_t s[4];                                /* left ic1, ic2, right ic1, ic2 (Q8) */
    int8_t e, tgt;                               /* the bell's gain now / what the sliders ask, half dB */
} eqb_t;

static int16_t eq_t[EQ_N];                       /* the sliders (the UI's), half dB */
static uint8_t eq_off;                           /* 1: EQ OFF (bypassed, the sliders kept) */
static eqb_t eq_b[EQ_N];
static int16_t eq_seen[EQ_N];                    /* the sliders the bells were solved for */
static uint8_t eq_seen_off, eq_live;             /* .. and EQ OFF then; eq_live: some bell ran in the last block */
static uint8_t eq_dirty = 1;
static uint8_t eq_cur;                           /* the UI's band: the last picked or turned (0..15) */
static uint8_t eq_win;                           /* .. and the first of the four on the dials (0..12; ui_geq.c) */

static inline int32_t eq_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

static void eq_coef(eqb_t *b, uint32_t i, int32_t e)    /* the bell i at e half dB (e != 0) */
{
    int64_t g = EQ_G[i], k = EQ_K[e + EQ_EMAX];
    uint32_t den, r = 1, q = 0, n;
    b->e = (int8_t)e;
    b->c = EQ_BG[e + EQ_EMAX];
    den = (uint32_t)(((int64_t)1 << 28) + ((g * (g + k)) >> 28));    /* 1 + g (g + k), Q28: below 2^32 */
    for (n = 0; n < 56u; n++) {                      /* q = 2^56 / den by shift and subtract: the target has no 64-bit divide */
        uint32_t top = r >> 31;
        r <<= 1;
        q <<= 1;
        if (top || r >= den) {
            r -= den;
            q |= 1u;
        }
    }
    b->a1 = (int32_t)q;
    b->a2 = (int32_t)((g * b->a1) >> 28);
    b->a3 = (int32_t)((g * b->a2) >> 28);
}

/* the sliders -> the bells' targets (tgt = EQ_MINV t, rounded to a half dB), when a slider or EQ OFF moved */
static void eq_solve(void)
{
    uint32_t i, j;
    for (i = 0; i < EQ_N; i++) {
        int32_t acc = 0;
        if (!eq_off)
            for (j = 0; j < EQ_N; j++)
                acc += (int32_t)EQ_MINV[i][j] * eq_clamp(eq_t[j], -EQ_HMAX, EQ_HMAX);
        eq_b[i].tgt = (int8_t)eq_clamp((acc + 2048) >> 12, -EQ_EMAX, EQ_EMAX);
    }
}

static void eq_update(void)                     /* once a block, before eq_process */
{
    uint32_t i, same = eq_seen_off == eq_off;
    for (i = 0; i < EQ_N; i++)
        same &= eq_seen[i] == eq_t[i];
    if (same && !eq_dirty)
        return;
    for (i = 0; i < EQ_N; i++)
        eq_seen[i] = eq_t[i];
    eq_seen_off = eq_off;
    eq_dirty = 0;
    eq_solve();
}

static void eq_run(const eqb_t *b, int32_t *s, int32_t *x, uint32_t n)   /* one channel through one bell */
{
    const int64_t a1 = b->a1, a2 = b->a2, a3 = b->a3, c = b->c, rnd = (int64_t)1 << 27, orn = (int64_t)1 << (27 + EQ_FRAC);
    int64_t ic1 = s[0], ic2 = s[1];
    uint32_t i;
    for (i = 0; i < n; i++) {
        int64_t xi = x[i], v3 = (xi << EQ_FRAC) - ic2, v1, v2;
        v1 = (a1 * ic1 + a2 * v3 + rnd) >> 28;
        v2 = ic2 + ((a2 * ic1 + a3 * v3 + rnd) >> 28);
        ic1 = 2 * v1 - ic1;
        ic2 = 2 * v2 - ic2;
        ic1 = ic1 < -EQ_SMAX ? -EQ_SMAX : ic1 > EQ_SMAX ? EQ_SMAX : ic1;
        ic2 = ic2 < -EQ_SMAX ? -EQ_SMAX : ic2 > EQ_SMAX ? EQ_SMAX : ic2;
        x[i] = (int32_t)(xi + ((c * v1 + orn) >> (28 + EQ_FRAC)));
    }
    s[0] = (int32_t)ic1, s[1] = (int32_t)ic2;
}

/* l, r: n samples in place (the master mix, Q15 with headroom) */
static void eq_process(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t i, live = 0;
    eq_update();
    for (i = 0; i < EQ_N; i++) {
        eqb_t *b = &eq_b[i];
        if (b->e != b->tgt) {                           /* a half dB a block towards the target */
            int32_t e = b->e + (b->tgt > b->e ? 1 : -1);
            if (e == 0) {
                b->e = 0;
                b->s[0] = b->s[1] = b->s[2] = b->s[3] = 0;   /* out: the next start is from rest */
            } else {
                eq_coef(b, i, e);
            }
        }
        if (!b->e)
            continue;
        eq_run(b, b->s, l, n);
        eq_run(b, b->s + 2, r, n);
        live++;
    }
    eq_live = (uint8_t)live;
}
