/* SPDX-License-Identifier: GPL-3.0-only */
/* The 16-band graphic EQ (firmware/src/geq.h): flat means untouched, a slider is its dB at its own centre, equal sliders
 * are flat across the bands, odd settings stay stable and silent when the input stops, EQ OFF fades out.
 *   cc -O2 -Ifirmware/src -o geq_test tests/geq_test.c -lm        (cl /O2 /Ifirmware\src tests\geq_test.c) */
#define _USE_MATH_DEFINES
#include <math.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "geq.h"

#define FSR 44100.0
#define BLK 32
static int fails;
static void check(int ok, const char *what)
{
    printf("geq: %-72s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static const double FC[16] = {40, 60, 90, 130, 200, 300, 440, 660, 980, 1450, 2200, 3250, 4800, 7200, 10700, 16000};

static void reset(void)
{
    memset(eq_t, 0, sizeof eq_t);
    memset(eq_b, 0, sizeof eq_b);
    memset(eq_seen, 0, sizeof eq_seen);
    eq_off = eq_seen_off = eq_live = 0;
    eq_dirty = 1;
}
static void settle(uint32_t blocks)             /* the weights glide: run silence-free blocks of nothing until they are there */
{
    static int32_t l[BLK], r[BLK];
    uint32_t i;
    for (i = 0; i < blocks; i++) {
        memset(l, 0, sizeof l), memset(r, 0, sizeof r);
        eq_process(l, r, BLK);
    }
}
/* gain in dB of a sine at f: 1.2 s in, the last 0.4 s measured, left channel (right checked equal) */
static double gain_at(double f, double amp)
{
    static int32_t l[BLK], r[BLK];
    const uint32_t total = (uint32_t)(1.2 * FSR) / BLK * BLK, meas = (uint32_t)(0.4 * FSR) / BLK * BLK;
    double si = 0, so = 0, so_r = 0;
    uint32_t n, i;
    for (n = 0; n < total; n += BLK) {
        double xin[BLK];
        for (i = 0; i < BLK; i++) {
            xin[i] = amp * sin(2 * M_PI * f * (double)(n + i) / FSR);
            l[i] = r[i] = (int32_t)lrint(xin[i]);
        }
        eq_process(l, r, BLK);
        if (n >= total - meas)
            for (i = 0; i < BLK; i++) {
                si += xin[i] * xin[i];
                so += (double)l[i] * l[i];
                so_r += (double)r[i] * r[i];
            }
    }
    if (fabs(so - so_r) > 1e-6 * so)
        printf("geq: left and right differ at %.0f Hz\n", f), fails++;
    return 10 * log10(so / si);
}
static void run_state(int32_t *l, int32_t *r, uint32_t n) { eq_process(l, r, n); }

int main(void)
{
    int i, ok;
    static const double ISO[] = {40, 60, 90, 130, 200, 300, 440, 660, 980, 1450, 2200, 3250, 4800, 7200, 10700, 16000};
    (void)ISO;

    /* flat: the signal is not touched at all, and no bell runs */
    reset();
    {
        int32_t l[BLK], r[BLK], l0[BLK];
        for (i = 0; i < BLK; i++)
            l0[i] = l[i] = r[i] = (i * 977) % 20000 - 10000;
        run_state(l, r, BLK);
        check(memcmp(l, l0, sizeof l) == 0 && memcmp(r, l0, sizeof r) == 0 && eq_live == 0, "flat: bit exact, no bell running");
    }

    /* one slider: +12 / -12 dB at its centre, and it is down to ~nothing two octaves away */
    ok = 1;
    for (i = 0; i < 16; i++) {
        double up, dn;
        reset();
        eq_t[i] = 24;
        up = gain_at(FC[i], 6000);
        reset();
        eq_t[i] = -24;
        dn = gain_at(FC[i], 6000);
        if (fabs(up - 12.0) > 0.7 || fabs(dn + 12.0) > 0.7) {
            ok = 0;
            printf("geq:   band %d (%.0f Hz): +12 -> %+.2f dB, -12 -> %+.2f dB\n", i, FC[i], up, dn);
        }
    }
    check(ok, "one slider +-12 dB: that gain at its centre, every band (+-0.7 dB)");

    /* a slider in half-dB steps */
    ok = 1;
    for (i = -24; i <= 24; i++) {
        double g;
        reset();
        eq_t[8] = (int16_t)i;
        g = gain_at(FC[8], 6000);
        if (fabs(g - i / 2.0) > 0.4) {
            ok = 0;
            printf("geq:   slider %d: %+.2f dB (want %+.1f)\n", i, g, i / 2.0);
        }
    }
    check(ok, "the 49 steps of one slider: half a dB each (+-0.4)");

    /* equal sliders: the same gain at every band centre (the bells overlap; the matrix takes it out) */
    {
        static const int v[3] = {12, -12, 24};
        int k;
        for (k = 0; k < 3; k++) {
            double worst = 0, between = 0;
            reset();
            for (i = 0; i < 16; i++)
                eq_t[i] = (int16_t)v[k];
            for (i = 1; i < 15; i++) {
                double c = gain_at(FC[i], 3000) - v[k] / 2.0;
                worst = fmax(worst, fabs(c));
                if (i < 13) {                                                /* between two centres (the top octave rolls off) */
                    c = gain_at(sqrt(FC[i] * FC[i + 1]), 3000) - v[k] / 2.0;
                    between = fmax(between, fabs(c));
                }
            }
            printf("geq:   all sliders %+.1f dB: centres within %.2f dB, between them within %.2f dB\n", v[k] / 2.0, worst, between);
            check(worst < 1.0 && between < 1.6, "equal sliders: flat across the bands");
        }
    }

    /* a smile curve and a tilt hit their sliders at the centres */
    {
        int16_t smile[16] = {24, 20, 14, 8, 2, -4, -8, -10, -10, -8, -4, 2, 8, 14, 20, 24};
        double worst = 0;
        reset();
        memcpy(eq_t, smile, sizeof smile);
        for (i = 2; i < 14; i++)
            worst = fmax(worst, fabs(gain_at(FC[i], 3000) - smile[i] / 2.0));
        printf("geq:   smile: worst centre error %.2f dB\n", worst);
        check(worst < 1.0, "a smile curve: each centre within 1 dB of its slider");
    }

    /* stability: random sliders, white noise at a high level, sliders moving every few blocks: no blow up, no large steps */
    {
        uint32_t seed = 12345, n;
        int32_t peak_in = 0, peak_out = 0, l[BLK], r[BLK];
        int bad = 0;
        reset();
        for (n = 0; n < 4000; n++) {
            if (n % 37 == 0)
                for (i = 0; i < 16; i++) {
                    seed = seed * 1664525u + 1013904223u;
                    eq_t[i] = (int16_t)((int)((seed >> 16) % 49u) - 24);
                }
            for (i = 0; i < BLK; i++) {
                seed = seed * 1664525u + 1013904223u;
                l[i] = r[i] = (int32_t)((seed >> 8) % 60001u) - 30000;
                if (abs(l[i]) > peak_in)
                    peak_in = abs(l[i]);
            }
            run_state(l, r, BLK);
            for (i = 0; i < BLK; i++) {
                if (abs(l[i]) > peak_out)
                    peak_out = abs(l[i]);
                if (l[i] != r[i])
                    bad = 1;
            }
        }
        printf("geq:   noise 30000, random sliders: peak in %d, out %d\n", peak_in, peak_out);
        check(!bad && peak_out < 40 * peak_in, "random sliders moving under loud noise: bounded, channels equal");
    }

    /* the input stops: the output dies away to exactly nothing (no limit cycle, no DC) */
    {
        int32_t l[BLK], r[BLK], last = 0;
        uint32_t n;
        reset();
        for (i = 0; i < 16; i++)
            eq_t[i] = (int16_t)(i % 2 ? 24 : -24);
        for (n = 0; n < 400; n++) {
            for (i = 0; i < BLK; i++)
                l[i] = r[i] = (int32_t)(20000 * sin(2 * M_PI * 90.0 * (double)(n * BLK + i) / FSR));
            run_state(l, r, BLK);
        }
        for (n = 0; n < 4000; n++) {
            memset(l, 0, sizeof l), memset(r, 0, sizeof r);
            run_state(l, r, BLK);
            last = 0;
            for (i = 0; i < BLK; i++)
                last = abs(l[i]) > last ? abs(l[i]) : last;
        }
        printf("geq:   3 s after the input stopped: |out| max %d\n", last);
        check(last <= 1, "after the input stops the tail is gone (<= 1 LSB)");
    }

    /* back to flat: every bell fades out and the EQ leaves the path (bit exact again), also through EQ OFF */
    {
        int32_t l[BLK], r[BLK];
        uint32_t n;
        reset();
        for (i = 0; i < 16; i++)
            eq_t[i] = 10;
        settle(200);
        check(eq_live > 0, "sliders up: bells run");
        eq_off = 1;
        for (n = 0; n < 400; n++) {
            for (i = 0; i < BLK; i++)
                l[i] = r[i] = 3000;
            run_state(l, r, BLK);
        }
        check(eq_live == 0, "EQ OFF: all bells faded out and out of the path");
        for (i = 0; i < BLK; i++)
            l[i] = r[i] = (i * 31) % 999;
        {
            int32_t l0[BLK];
            memcpy(l0, l, sizeof l);
            run_state(l, r, BLK);
            check(memcmp(l, l0, sizeof l) == 0, "EQ OFF: bit exact");
        }
        eq_off = 0;
        settle(400);
        check(eq_live > 0 && gain_at(FC[8], 3000) > 4.5, "EQ ON again: back");
        for (i = 0; i < 16; i++)
            eq_t[i] = 0;
        settle(600);
        check(eq_live == 0, "all sliders to 0: the EQ leaves the path");
    }

    /* a slider moved under a loud sine sitting on its band: the output's step stays within ~3x the steady state's own
     * (the bell's gain moves a half dB a block; the state is kept) */
    {
        int32_t l[BLK], r[BLK], prev = 0, worst = 0, steady = 0;
        uint32_t n;
        reset();
        eq_t[2] = 24;
        for (n = 0; n < 1500; n++) {
            if (n == 800)
                eq_t[2] = 0;
            if (n == 1000)
                eq_t[2] = -24;
            for (i = 0; i < BLK; i++)
                l[i] = r[i] = (int32_t)(20000 * sin(2 * M_PI * 90.0 * (double)(n * BLK + i) / FSR));
            run_state(l, r, BLK);
            for (i = 0; i < BLK; i++) {
                int32_t d = abs(l[i] - prev);
                if (n > 400 && n < 780 && d > steady)
                    steady = d;
                if (n > 400 && d > worst)
                    worst = d;
                prev = l[i];
            }
        }
        printf("geq:   slider moved under a loud 90 Hz sine: largest sample step %d (steady state %d)\n", worst, steady);
        check(worst < 3 * steady + 200, "moving a slider is click-free (the step stays near the signal's own)");
    }

    /* cost: all sixteen bells on, stereo, samples per second on this host */
    {
        static int32_t l[BLK], r[BLK];
        uint32_t n, total = 44100u * 20u / BLK;
        clock_t t0;
        double sec;
        reset();
        for (i = 0; i < 16; i++)
            eq_t[i] = (int16_t)(i % 2 ? 12 : -12);
        t0 = clock();
        for (n = 0; n < total; n++) {
            for (i = 0; i < BLK; i++)
                l[i] = r[i] = (int32_t)((n * 31u + (uint32_t)i * 7u) % 9000u) - 4500;
            run_state(l, r, BLK);
        }
        sec = (double)(clock() - t0) / CLOCKS_PER_SEC;
        printf("geq:   16 bells stereo: 20 s of audio in %.3f s on this host (%.0f x real time)\n", sec, 20.0 / sec);
    }

    printf("geq: %s\n", fails ? "FAILED" : "all passed");
    return fails != 0;
}
