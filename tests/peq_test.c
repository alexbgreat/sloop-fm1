/* SPDX-License-Identifier: GPL-3.0-only */
/* The 5-band parametric EQ (firmware/src/peq.h): flat means untouched, each type does what it says at its frequency,
 * a half-dB step is a half dB, the screen's response curve matches the filter, odd settings stay stable and silent
 * when the input stops, TYPE changes and EQ OFF fade without a click.
 *   cc -O2 -Ifirmware/src -o peq_test tests/peq_test.c -lm */
#define _USE_MATH_DEFINES
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "peq.h"

#define FSR 44100.0
#define BLK 32
static int fails;
static void check(int ok, const char *what)
{
    printf("peq: %-76s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static double hz(uint32_t f) { return EQ_HZ10[f] / 10.0; }
static double col_hz(uint32_t x) { return 20.0 * pow(1000.0, x / (double)(EQ_COLS - 1)); }

static void reset(void)
{
    memcpy(eq_p, EQ_DEF, sizeof eq_p);
    memset(eq_b, 0, sizeof eq_b);
    eq_off = eq_live = 0;
}
static void only(uint32_t type, uint32_t f, int32_t e, uint32_t q)   /* band 0 set, the others OFF */
{
    uint32_t b;
    reset();
    for (b = 0; b < EQ_N; b++)
        eq_p[b][EQP_TYPE] = EQT_OFF;
    eq_p[0][EQP_TYPE] = (int16_t)type, eq_p[0][EQP_FREQ] = (int16_t)f, eq_p[0][EQP_GAIN] = (int16_t)e;
    eq_p[0][EQP_Q] = (int16_t)q;
}
static void settle(uint32_t blocks)
{
    static int32_t l[BLK], r[BLK];
    uint32_t i;
    for (i = 0; i < blocks; i++) {
        memset(l, 0, sizeof l), memset(r, 0, sizeof r);
        eq_process(l, r, BLK);
    }
}
/* gain in dB of a sine at f: 1.0 s in, the last 0.4 s measured; *lr: left and right equal */
static double gain_at(double f, double amp, int *lr)
{
    static int32_t l[BLK], r[BLK];
    const uint32_t total = (uint32_t)(1.0 * FSR) / BLK * BLK, meas = (uint32_t)(0.4 * FSR) / BLK * BLK;
    double si = 0, so = 0;
    uint32_t n, i;
    int same = 1;
    for (n = 0; n < total; n += BLK) {
        double xin[BLK];
        for (i = 0; i < BLK; i++) {
            xin[i] = amp * sin(2 * M_PI * f * (double)(n + i) / FSR);
            l[i] = r[i] = (int32_t)lrint(xin[i]);
        }
        eq_process(l, r, BLK);
        for (i = 0; i < BLK; i++)
            same &= l[i] == r[i];
        if (n >= total - meas)
            for (i = 0; i < BLK; i++) {
                si += xin[i] * xin[i];
                so += (double)l[i] * l[i];
            }
    }
    if (lr)
        *lr = same;
    return 10 * log10((so + 1e-9) / si);
}
static double g1(double f) { return gain_at(f, 8000, 0); }

int main(void)
{
    static int32_t l[BLK], r[BLK], l0[BLK];
    uint32_t i, n, b;
    int ok, lr;
    double w;

    reset();
    srand(1);
    for (n = 0, ok = 1; n < 400; n++) {
        for (i = 0; i < BLK; i++)
            l[i] = r[i] = l0[i] = (int32_t)(rand() % 60001) - 30000;
        eq_process(l, r, BLK);
        ok &= memcmp(l, l0, sizeof l) == 0 && memcmp(r, l0, sizeof r) == 0 && eq_live == 0;
    }
    check(ok, "the defaults (every band at 0 dB): bit exact, no band in the path");

    {   /* BELL +-12 dB at its frequency, across the range */
        static const uint32_t F[] = {12, 28, 52, 68, 84, 100, 112};
        double worst = 0;
        for (i = 0; i < sizeof F / sizeof F[0]; i++)
            for (n = 0; n < 2; n++) {
                double want = n ? -12 : 12, d;
                only(EQT_BELL, F[i], n ? -24 : 24, 10);
                settle(80);
                d = fabs(g1(hz(F[i])) - want);
                if (d > worst)
                    worst = d;
            }
        printf("peq:   BELL +-12 dB, Q 1, 40 Hz .. 12.5 kHz: worst %.2f dB off\n", worst);
        check(worst < 0.3, "BELL +-12 dB: that gain at its frequency (+-0.3 dB)");
    }
    {   /* every half-dB step of a BELL at 1 kHz */
        double worst = 0, prev = 0;
        for (n = 0; n <= 48; n++) {
            int32_t e = (int32_t)n - 24;
            double d;
            only(EQT_BELL, 68, e, 10);
            settle(80);
            d = g1(1000.0);
            if (n && fabs(d - prev - 0.5) > worst)
                worst = fabs(d - prev - 0.5);
            prev = d;
        }
        printf("peq:   the 48 half-dB steps of a BELL at 1 kHz: worst %.3f dB off 0.5\n", worst);
        check(worst < 0.1, "a half-dB GAIN step is half a dB (+-0.1)");
    }
    {   /* Q: a narrow bell is narrow */
        double wide, narrow;
        only(EQT_BELL, 68, 24, 2); settle(80); wide = g1(1000.0 * 1.5);       /* Q 0.4 */
        only(EQT_BELL, 68, 24, 28); settle(80); narrow = g1(1000.0 * 1.5);    /* Q 8 */
        printf("peq:   BELL +12 dB at 1 kHz, at 1.5 kHz: Q 0.4 %.1f dB, Q 8 %.1f dB\n", wide, narrow);
        check(wide > 9 && narrow < 1.5, "Q: wide at 0.4, narrow at 8");
    }
    {   /* the shelves */
        double lo, hi, lo2, hi2;
        only(EQT_LSHF, 44, 12, 7); settle(80); lo = g1(30); hi = g1(8000);
        only(EQT_HSHF, 92, -12, 7); settle(80); lo2 = g1(100); hi2 = g1(15000);
        printf("peq:   LOW SHELF +6 dB 250 Hz: 30 Hz %.2f, 8 kHz %.2f; HIGH SHELF -6 dB 4 kHz: 100 Hz %.2f, 15 kHz %.2f\n",
               lo, hi, lo2, hi2);
        check(fabs(lo - 6) < 0.4 && fabs(hi) < 0.2 && fabs(lo2) < 0.2 && fabs(hi2 + 6) < 0.6, "the shelves: their gain below / above, flat on the other side");
    }
    {   /* the cuts: -3 dB at FREQ (Q 0.71), steep beyond */
        double c, far, pass;
        only(EQT_LCUT, 44, 0, 7); settle(80); c = g1(250); far = g1(60); pass = g1(4000);
        printf("peq:   LOW CUT 250 Hz: %.2f dB there, %.1f at 60 Hz, %.2f at 4 kHz\n", c, far, pass);
        ok = fabs(c + 3) < 0.4 && far < -20 && fabs(pass) < 0.1;
        only(EQT_HCUT, 80, 0, 7); settle(80); c = g1(2000); far = g1(8000); pass = g1(200);
        printf("peq:   HIGH CUT 2 kHz: %.2f dB there, %.1f at 8 kHz, %.2f at 200 Hz\n", c, far, pass);
        check(ok && fabs(c + 3) < 0.4 && far < -20 && fabs(pass) < 0.1, "LOW CUT / HIGH CUT: -3 dB at FREQ, steep beyond, flat in the pass band");
    }
    {   /* the screen's curve (eq_db16) against the filter, every type, several columns */
        static const uint32_t T[] = {EQT_BELL, EQT_LSHF, EQT_HSHF, EQT_LCUT, EQT_HCUT, EQT_BELL};
        static const uint32_t F[] = {68, 36, 96, 28, 100, 6};
        static const int32_t E[] = {18, -16, 20, 0, 0, -24};
        static const uint32_t Q[] = {16, 7, 3, 20, 12, 10};
        static const uint32_t X[] = {10, 40, 70, 100, 130, 160, 190, 215};
        double worst = 0;
        uint32_t k, j;
        for (k = 0; k < sizeof T / sizeof T[0]; k++) {
            only(T[k], F[k], E[k], Q[k]);
            settle(80);
            for (j = 0; j < sizeof X / sizeof X[0]; j++) {
                double meas = g1(col_hz(X[j])), curve = eq_db16(&eq_b[0].c, T[k], X[j]) / 16.0, d;
                if (meas < -30 && curve < -30)
                    continue;                               /* deep in a cut: both say so */
                d = fabs(meas - curve);
                if (d > worst)
                    worst = d;
            }
        }
        printf("peq:   the response curve against the filter: worst %.2f dB\n", worst);
        check(worst < 0.5, "the screen's response curve is the filter's (+-0.5 dB)");
    }
    {   /* five bands, random settings moving under loud noise: bounded, channels equal */
        int bad = 0;
        double peak_in = 0, peak_out = 0;
        reset();
        srand(7);
        for (n = 0; n < 6000; n++) {
            if (n % 40 == 0)
                for (b = 0; b < EQ_N; b++) {
                    eq_p[b][EQP_TYPE] = (int16_t)(rand() % EQT_N);
                    eq_p[b][EQP_FREQ] = (int16_t)(rand() % EQ_NF);
                    eq_p[b][EQP_GAIN] = (int16_t)(rand() % 49 - 24);
                    eq_p[b][EQP_Q] = (int16_t)(rand() % EQ_NQ);
                }
            for (i = 0; i < BLK; i++) {
                l[i] = r[i] = (int32_t)(rand() % 60001) - 30000;
                if (abs(l[i]) > peak_in)
                    peak_in = abs(l[i]);
            }
            eq_process(l, r, BLK);
            for (i = 0; i < BLK; i++) {
                bad |= l[i] != r[i];
                if (abs(l[i]) > peak_out)
                    peak_out = abs(l[i]);
            }
        }
        {   /* five bells at +12 dB on one frequency are +60 dB (x1000) and a Q-8 cut peaks ~+18 dB: what is legitimate.
             * Stable: the output stays below the states' limit, and with the input gone it dies away */
            int32_t last = 0;
            for (n = 0; n < (uint32_t)(4 * FSR / BLK); n++) {
                memset(l, 0, sizeof l), memset(r, 0, sizeof r);
                eq_process(l, r, BLK);
            }
            for (i = 0; i < BLK; i++)
                if (abs(l[i]) > last)
                    last = abs(l[i]);
            printf("peq:   random bands under noise: peak in %.0f, out %.0f; 4 s after the input stops %d\n", peak_in,
                   peak_out, last);
            check(!bad && peak_out < (double)EQ_XMAX * 4 && last <= 1,
                  "random bands moving under loud noise: stable (dies away), channels equal");
        }
    }
    {   /* a 20 Hz bell +12 and a resonant cut: the tail is silent after the input stops */
        int32_t last = 0;
        reset();
        eq_p[0][EQP_TYPE] = EQT_BELL, eq_p[0][EQP_FREQ] = 0, eq_p[0][EQP_GAIN] = 24, eq_p[0][EQP_Q] = 28;
        eq_p[1][EQP_TYPE] = EQT_LCUT, eq_p[1][EQP_FREQ] = 4, eq_p[1][EQP_Q] = 28;
        settle(80);
        srand(3);
        for (n = 0; n < 2000; n++) {
            for (i = 0; i < BLK; i++)
                l[i] = r[i] = (int32_t)(rand() % 20001) - 10000;
            eq_process(l, r, BLK);
        }
        for (n = 0; n < (uint32_t)(8 * FSR / BLK); n++) {
            memset(l, 0, sizeof l), memset(r, 0, sizeof r);
            eq_process(l, r, BLK);
        }
        for (i = 0; i < BLK; i++)
            if (abs(l[i]) > last)
                last = abs(l[i]);
        check(last <= 1, "low resonant bands: after the input stops the tail is gone (<= 1 LSB)");
    }
    {   /* EQ OFF fades out and leaves the path; ON comes back */
        only(EQT_BELL, 68, 12, 10);
        settle(80);
        ok = eq_live == 1;
        eq_off = 1;
        settle(20);
        ok &= eq_live == 0;
        for (i = 0; i < BLK; i++)
            l[i] = r[i] = l0[i] = (int32_t)(i * 997 % 20000) - 10000;
        eq_process(l, r, BLK);
        ok &= memcmp(l, l0, sizeof l) == 0;
        eq_off = 0;
        settle(40);
        check(ok && eq_live == 1 && fabs(g1(1000) - 6) < 0.3, "EQ OFF: faded out and bit exact; ON again: back");
    }
    {   /* no click: a sine through TYPE changes, a GAIN swing, a FREQ sweep, EQ OFF / ON */
        double worst = 0, steady = 0;
        int32_t prev = 0;
        only(EQT_BELL, 68, 0, 10);
        settle(80);
        for (n = 0; n < 3000; n++) {
            uint32_t ph = n % 600;
            if (ph == 100) eq_p[0][EQP_GAIN] = 24;
            if (ph == 200) eq_p[0][EQP_TYPE] = EQT_LCUT;
            if (ph == 300) eq_p[0][EQP_FREQ] = 100, eq_p[0][EQP_TYPE] = EQT_HSHF;
            if (ph == 400) eq_off = 1;
            if (ph == 450) eq_off = 0;
            if (ph == 500) eq_p[0][EQP_TYPE] = EQT_BELL, eq_p[0][EQP_FREQ] = 68, eq_p[0][EQP_GAIN] = 0;
            for (i = 0; i < BLK; i++)
                l[i] = r[i] = (int32_t)lrint(8000 * sin(2 * M_PI * 300.0 * (n * BLK + i) / FSR));
            eq_process(l, r, BLK);
            for (i = 0; i < BLK; i++) {
                double d = fabs((double)l[i] - prev);
                if (n < 80 && d > steady)
                    steady = d;
                if (n >= 80 && d > worst)
                    worst = d;
                prev = l[i];
            }
        }
        printf("peq:   sample-to-sample step: steady %.0f, worst while moving %.0f\n", steady, worst);
        check(worst < 4.5 * steady, "TYPE, GAIN, FREQ and EQ OFF changes are click-free");
    }
    w = gain_at(1000, 8000, &lr);
    (void)w;
    check(lr, "left and right the same");
    printf(fails ? "peq: FAILED\n" : "peq: all passed\n");
    return fails != 0;
}
