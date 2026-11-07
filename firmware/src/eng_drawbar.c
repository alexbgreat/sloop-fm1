/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* DRAWBAR: a tonewheel-style additive organ. Felucca's own design.
 *
 * Partials: nine sines per voice at the classic drawbar footages, 16' 5 1/3' 8' 4' 2 2/3' 2'
 * 1 3/5' 1 1/3' 1' (0.5, 1.5, 1, 2, 3, 4, 5, 6, 8 times the note), each with a level 0..8
 * (about 3 dB a step). One shared sine table (plain lookup), one free-running integer phase
 * per partial: a note starts wherever its wheels are, as on the real generators. A partial
 * above ~6 kHz folds back an octave (as the top of a tonewheel set does), so nothing aliases.
 * Partials at level 0 are skipped (their phase still runs).
 *
 * Registration: 8 knobs cannot hold 9 drawbars, so REG picks one of 16 drawbar settings
 * (DRW_REG: our list of common registrations) and SUB / BODY / TOP add -8..+8 to its sub
 * bars (16', 5 1/3'), its body (8', 4') and its upper bars (2 2/3' .. 1'). The louder the
 * registration, the less each bar gets (a soft normalisation: full bars are louder than one
 * bar, but they do not stack up to clipping).
 *
 * PERC: a decaying partial on the 2nd (4') or 3rd (2 2/3') harmonic, normal or soft, fast
 * or slow. Single trigger: it only strikes on a note that starts while no other key of the
 * part is held (legato playing gets none). With PERC on the 1' bar is silent and, at normal
 * level, the bars drop by ~3 dB, as on the real circuit.
 * CLICK: a short burst of high-passed noise at the key contact (half as loud at the release).
 * DRIVE: a per-voice soft overdrive (tanh); for chord growl add the track's DIST.
 * ROTR: a cheap rotary speaker effect: two rotors (a low one and a high one, split by partial
 * frequency at ~800 Hz) that spin SLOW or FAST and ramp between the speeds (the high rotor in
 * ~1 s, the low one in ~4 s); each gives its partials an amplitude and a pitch (Doppler)
 * modulation whose depth follows its speed. All of it is folded into the per-block partial
 * gains and increments: no delay line, no per-sample cost. OFF fades it out and stops them.
 *
 * Envelope: an organ gate (the presets set A 0, S 127, a short R); the track ADSR still applies.
 * Cost: per sample and partial one table lookup and a multiply-add (a gain ramp only for the
 * partials whose gain moved: percussion, rotary, knobs); the bar levels, the rotor and the
 * percussion run once per block. State: the per-track rotor and bar gains (drw_trk) and the
 * per-voice phases / gains (drw_vc) live here, indexed by part and voice; voice_t is unused. */
#define DRW_NP 9                 /* partials */
#define DRW_FOLD 584366000u      /* ~6 kHz as a phase increment: partials above fold down an octave */
#define DRW_XOVER 77915000u      /* ~800 Hz: below the low rotor, above the high rotor (ROTOR 2 > XOVER's default) */
#define DRW_XOVER10 973937u      /* 10 Hz as a phase increment */

/* drawbar level 0..8 -> Q15, 3 dB a step */
static const uint16_t DRW_LV[9] = {0, 2920, 4125, 5827, 8231, 11627, 16423, 23198, 32767};

/* the 16 registrations, one digit per bar: 16' 5 1/3' 8' 4' 2 2/3' 2' 1 3/5' 1 1/3' 1' */
static const char *const N_DRW_REG[] = {"FLUTE", "MELLO", "HOLLW", "SMOOT", "3BAR", "BLUES", "GOSPL", "ROCK",
                                        "TOPS", "CLARI", "REED", "STRNG", "CHAPL", "BRITE", "BASS", "FULL", "CUSTM"};
/* REG CUSTM (SMK-37 organ fader banks): the nine bars are drw_bars[part], set one by one (drw_bar_set); SUB / BODY /
 * TOP still add to them. A project keeps them packed into E1..E3 (project.c drw_pack / drw_unpack) */
#define DRW_CUSTOM 16
static uint8_t drw_bars[NPART][DRW_NP];

/* The ROTOR pages (EDIT, DRAWBAR only: params.c SC_ROTOR): the rotary speaker's own settings, per part, kept with the
 * project in E0 and E4..E7's spare bits (drw_rot_pack). Each at its default sounds exactly as before they existed.
 *   SLOW, FAST  the horn's speeds (0.1 Hz)        ACCEL  how fast the rotors change speed (64: horn ~1 s, drum ~4 s)
 *   DEPTH       the amplitude modulation          DRUM   the drum's speed, % of the horn's
 *   BAL         horn (+) / drum (-) level         XOVER  where the horn takes over from the drum (10 Hz)
 *   DOPL        the Doppler pitch modulation */
enum { RT_SLOW, RT_FAST, RT_ACCEL, RT_DEPTH, RT_DRUM, RT_BAL, RT_XOVER, RT_DOPL, RT_N };
static const param_desc_t DRW_ROT_DESC[RT_N] = {
    {"SLOW", F_HZD10, 2, 20, 8, 0, 0},
    {"FAST", F_HZD10, 30, 100, 67, 0, 0},
    {"ACCEL", F_PCT, 0, 127, 64, 0, 0},
    {"DEPTH", F_PCT, 0, 127, 64, 0, 0},
    {"DRUM", F_INT, 50, 120, 87, 0, "%"},
    {"BAL", F_BIPCT, -64, 63, 0, 0, 0},
    {"XOVER", F_HZX10, 30, 150, 80, 0, 0},
    {"DOPL", F_PCT, 0, 127, 64, 0, 0},
};
static const uint8_t DRW_ROT_BITS[RT_N] = {5, 7, 7, 7, 7, 7, 7, 7};   /* (54 bits in a project) */
static int16_t drw_rot[NPART][RT_N] = {
    {8, 67, 64, 64, 87, 0, 80, 64}, {8, 67, 64, 64, 87, 0, 80, 64}, {8, 67, 64, 64, 87, 0, 80, 64}};
static uint8_t drw_wheel_vib;    /* SMK-37 HOME menu > FADERS > ORGAN WHEEL: 0 ROTOR (the mod wheel is the rotor switch),
                                  * 1 VIBRATO (as on the other engines) */
static const char DRW_REG[16][DRW_NP + 1] = {
    "008000000", /* FLUTE  one soft flute */
    "008400000", /* MELLO  flute and octave */
    "800800000", /* HOLLW  sub and octave, hollow */
    "838000000", /* SMOOT  smooth comping */
    "888000000", /* 3BAR   the first three bars out */
    "888800000", /* BLUES  three bars and the octave */
    "888600006", /* GOSPL  body with a little shimmer */
    "888000888", /* ROCK   body and the top three */
    "800000888", /* TOPS   sub and the top only */
    "008080800", /* CLARI  odd harmonics, reedy-hollow */
    "008765432", /* REED   a falling slope, brassy */
    "006876540", /* STRNG  bright and thin */
    "004432000", /* CHAPL  soft, churchy */
    "008888000", /* BRITE  even bars, bright */
    "858000000", /* BASS   for the left hand */
    "888888888", /* FULL   everything */
};
static const char *const N_DRW_PERC[] = {"OFF", "2ND", "3RD", "2SOFT", "3SOFT", "2SLOW", "3SLOW"};
static const char *const N_DRW_ROTR[] = {"OFF", "SLOW", "FAST"};

/* partial frequency / note frequency, as a shift-and-add of the note's increment; FOLD_LIM:
 * the note increment above which the partial would pass DRW_FOLD */
static const uint32_t DRW_FOLD_LIM[DRW_NP] = {
    DRW_FOLD * 2u, DRW_FOLD / 3u * 2u, DRW_FOLD, DRW_FOLD / 2u, DRW_FOLD / 3u, DRW_FOLD / 4u, DRW_FOLD / 5u,
    DRW_FOLD / 6u, DRW_FOLD / 8u,
};
static inline uint32_t drw_ratio(uint32_t k, uint32_t x)
{
    switch (k) {
    case 0: return x >> 1;
    case 1: return x + (x >> 1);
    case 2: return x;
    case 3: return x << 1;
    case 4: return x * 3u;
    case 5: return x << 2;
    case 6: return x * 5u;
    case 7: return x * 6u;
    default: return x << 3;
    }
}

/* rotor speeds as phase increments per block (CTL samples): Hz * CTL / FS * 2^32 */
#define DRW_HZ(h) ((int32_t)((h) * 3116457.0))
static const int32_t DRW_ROT_SPD[2][3] = {
    {0, DRW_HZ(0.8), DRW_HZ(6.7)},   /* high rotor: OFF (stops), SLOW, FAST */
    {0, DRW_HZ(0.7), DRW_HZ(5.8)},   /* low rotor */
};

typedef struct {                 /* per part, once per block (drawbar_block) */
    int32_t g[DRW_NP];           /* bar gains, Q15, normalised */
    int32_t pk, plev;            /* percussion: partial (DRW_NP = off), level Q15 */
    uint32_t pdec;               /* its decay per block, Q16 */
    int32_t clk;                 /* click level Q15 */
    int32_t dg, dcomp;           /* drive gain, Q8; output compensation, Q8 (0 = no drive) */
    uint32_t rph[2];             /* rotor phases (0 high, 1 low) */
    int32_t rsp[2];              /* rotor speeds (phase increment per block) */
    int32_t rwet;                /* rotary amount, Q15 (ramps on / off) */
    int32_t am[2];               /* this block's amplitude, Q15, per rotor */
    int32_t fm[2];               /* this block's pitch offset, Q16 of the increment, per rotor */
    uint32_t xover;              /* the crossover as a phase increment (ROTOR 2 > XOVER) */
} drw_trk_t;

typedef struct {                 /* per voice */
    uint32_t ph[DRW_NP];
    int32_t gp[DRW_NP];          /* each partial's gain at the end of the last block (the ramp start) */
    int32_t perc;                /* percussion envelope, Q15 */
    int32_t clk;                 /* click envelope, Q15 */
    int32_t nz, hp;              /* click noise state, its high-pass state */
    uint8_t gate, init;
} drw_vc_t;

/* sloop-lab: the part's DRAWBAR state lives in its engine arena (engines.c eng_arena_of, as GRAIN, FM6 and PHYS
 * do): 2.5 KB less in RAM. The arena is zeroed when another arena engine takes the part, so the wheels' phases
 * spread again (init) when DRAWBAR comes back after GRAIN, FM6 or PHYS; after any other engine they stay as they
 * were, as before. */
#define ENGI_DRAWBAR 7u                 /* ENGINES[] (engines.c) */
typedef struct { drw_trk_t t; drw_vc_t v[NVOICE]; } drw_part_t;
static drw_part_t *drw_of(const track_t *t) { return (drw_part_t *)eng_arena_of(t, ENGI_DRAWBAR); }

/* SUB / BODY / TOP: which of them moves bar k */
static uint32_t drw_grp(uint32_t k) { return k < 2u ? P_E1 : k < 4u ? P_E2 : P_E3; }
/* the bar level 0..8 of partial k of part pi: the registration (or CUSTM's bars) plus SUB / BODY / TOP */
static int32_t drw_level(const int16_t *p, uint32_t pi, uint32_t k)
{
    int32_t d = p[P_E0] == DRW_CUSTOM ? (int32_t)drw_bars[pi % NPART][k] : DRW_REG[(uint32_t)p[P_E0] & 15u][k] - '0';
    d += p[drw_grp(k)];
    return clamp(d, 0, 8);
}

/* the SMK-37's organ mode: the selected track plays DRAWBAR (main.c smk_faders, smk_wheels; ui_fbank.c) */
static int drw_organ(const track_t *t)
{
    return t >= trk && t < trk + NPART && t->eng_req == ENGI_DRAWBAR;
}
static uint32_t drw_bar(const track_t *t, uint32_t k) { return (uint32_t)drw_level(t->p, (uint32_t)(t - trk), k); }
/* bar k to level v (0..8), as heard: the first bar moved turns the registration into CUSTM (as it sounds, SUB / BODY
 * / TOP folded in) */
static void drw_bar_set(track_t *t, uint32_t k, uint32_t v)
{
    uint32_t pi = (uint32_t)(t - trk) % NPART, j;
    if (k >= DRW_NP)
        return;
    if (t->p[P_E0] != DRW_CUSTOM) {
        for (j = 0; j < DRW_NP; j++)
            drw_bars[pi][j] = (uint8_t)drw_level(t->p, pi, j);
        t->p[P_E1] = t->p[P_E2] = t->p[P_E3] = 0;
        t->p[P_E0] = DRW_CUSTOM;
    }
    drw_bars[pi][k] = (uint8_t)clamp((int32_t)(v > 8u ? 8u : v) - t->p[drw_grp(k)], 0, 8);
}
/* a project: the ROTOR values, each (value - min) ^ (default - min) (0: the default, as in older projects), in a 54-bit
 * stream over the spare bits of E0 (bits 5..15), E4 (3..15), E5 (7..15), E6 (7..15), E7 (2..15). The low bits stay
 * REG, PERC, CLICK, DRV, ROTR (drw_e_low takes them back out before the range check) */
static const uint8_t DRW_RSLOT[5] = {0, 4, 5, 6, 7}, DRW_RLOW[5] = {5, 3, 7, 7, 2};
static int32_t drw_e_low(uint32_t e, int32_t v)       /* E0..E7 of a stored DRAWBAR track -> the value alone */
{
    uint32_t i;
    for (i = 0; i < 5u; i++)
        if (DRW_RSLOT[i] == e)
            return (int32_t)((uint32_t)(uint16_t)v & ((1u << DRW_RLOW[i]) - 1u));
    return v;
}
/* bit by bit over the five words' spare bits: pack (e != 0: the values into e) or unpack (into drw_rot) */
static void drw_rot_bits(int16_t *r, int16_t *e, int pack)
{
    uint32_t k, b, w = 0, pos = DRW_RLOW[0];
    for (k = 0; k < RT_N; k++) {
        const param_desc_t *d = &DRW_ROT_DESC[k];
        uint32_t x = (uint32_t)(d->def - d->min), f = pack ? ((uint32_t)(clamp(r[k], d->min, d->max) - d->min) ^ x) : 0u;
        for (b = 0; b < DRW_ROT_BITS[k]; b++, pos++) {
            uint16_t *u;
            if (pos == 16u)
                pos = DRW_RLOW[++w];
            u = (uint16_t *)&e[DRW_RSLOT[w]];
            if (pack)
                *u = (uint16_t)((*u & ~(1u << pos)) | ((f >> b) & 1u) << pos);
            else
                f |= ((uint32_t)(*u >> pos) & 1u) << b;
        }
        if (!pack)
            r[k] = (int16_t)clamp(d->min + (int32_t)(f ^ x), d->min, d->max);
    }
}
static void drw_rot_pack(const track_t *t, int16_t *e) { drw_rot_bits(drw_rot[(uint32_t)(t - trk) % NPART], e, 1); }
static void drw_rot_unpack(track_t *t, const int16_t *e)
{
    int16_t c[8];
    memcpy(c, e, sizeof c);
    drw_rot_bits(drw_rot[(uint32_t)(t - trk) % NPART], c, 0);
}

/* a project's E1..E3 with CUSTM: each its offset (5 bits) | three bars in base 9 << 5 */
static int16_t drw_pack(const track_t *t, uint32_t g)
{
    const uint8_t *b = drw_bars[(uint32_t)(t - trk) % NPART] + 3u * g;
    return (int16_t)(((uint32_t)t->p[P_E1 + g] & 31u) | (uint32_t)(b[0] + 9u * b[1] + 81u * b[2]) << 5);
}
static int16_t drw_unpack(track_t *t, uint32_t g, int16_t e)
{
    uint8_t *b = drw_bars[(uint32_t)(t - trk) % NPART] + 3u * g;
    uint32_t w = ((uint32_t)(uint16_t)e >> 5) % 729u;
    b[0] = (uint8_t)(w % 9u), b[1] = (uint8_t)(w / 9u % 9u), b[2] = (uint8_t)(w / 81u);
    return (int16_t)((int32_t)((e & 31) ^ 16) - 16);       /* the offset, sign-extended */
}

/* once per block, before the voices: bar gains, percussion, click, drive, the rotors */
static void drawbar_block(track_t *t)
{
    drw_trk_t *T = &drw_of(t)->t;
    const int16_t *p = t->p;
    uint32_t k, perc = (uint32_t)p[P_E4] % 7u, rot = (uint32_t)p[P_E7] % 3u;
    int32_t s = 0, lv[DRW_NP];
    uint32_t norm;
    for (k = 0; k < DRW_NP; k++) {
        lv[k] = perc && k == DRW_NP - 1u ? 0 : DRW_LV[drw_level(p, (uint32_t)(t - trk), k)];   /* PERC on: the 1' bar is off */
        s += lv[k];
    }
    /* sum of the gains = 2.4 s / (s + 1.5) (s in units of a full bar): one bar 0.96, three 1.6,
     * all nine 2.06 */
    norm = (uint32_t)(2u * 39322u * 32768u) / (uint32_t)(s + 49152);
    if (perc == 1u || perc == 2u || perc == 5u || perc == 6u)
        norm = (norm * 23170u) >> 15;                   /* normal percussion: the bars -3 dB */
    for (k = 0; k < DRW_NP; k++)
        T->g[k] = (int32_t)(((uint32_t)lv[k] * norm) >> 15);
    T->pk = perc ? (int32_t)(3u + ((perc - 1u) & 1u)) : DRW_NP;   /* 2ND: 4', 3RD: 2 2/3' */
    T->plev = perc == 3u || perc == 4u ? 12000 : 24000;
    T->pdec = perc >= 5u ? 65457u : 65219u;             /* tau 0.6 s / 0.15 s */
    T->clk = p[P_E5] * 180;
    if (p[P_E6]) {                                      /* y = 6 / (2 + g) tanh(g x / 2) */
        T->dg = 256 + p[P_E6] * 6;                      /* 1 .. 4 */
        T->dcomp = (6 * 256 * 256) / (512 + T->dg);
    } else {
        T->dg = 0;
    }
    /* rotors: each speed eases to its target (high rotor tau ~0.37 s, low ~1.5 s at ACCEL 64); the ROTOR pages'
     * values at their defaults give exactly the table's speeds and the depths as before */
    {
        const int16_t *r = drw_rot[(uint32_t)(t - trk) % NPART];
        int32_t tgt[2] = {0, 0}, acc = r[RT_ACCEL] < 4 ? 4 : r[RT_ACCEL];
        if (rot) {
            uint32_t ri = rot == 1u ? RT_SLOW : RT_FAST;
            int32_t hv = r[ri], std = hv == DRW_ROT_DESC[ri].def;
            tgt[0] = std ? DRW_ROT_SPD[0][rot] : hv * DRW_HZ(0.1);
            tgt[1] = std && r[RT_DRUM] == 87 ? DRW_ROT_SPD[1][rot] : tgt[0] / 100 * r[RT_DRUM];
        }
        for (k = 0; k < 2u; k++) {
            int32_t sp, c, sn, dep;
            T->rsp[k] += (((tgt[k] - T->rsp[k]) >> (k ? 11 : 9)) * acc) >> 6;
            sp = T->rsp[k];
            T->rph[k] += (uint32_t)sp;
            sn = sine_i(T->rph[k]);
            c = sine_i(T->rph[k] + 0x40000000u);
            dep = ((k ? 7000 : 13000) * r[RT_DEPTH]) >> 6;   /* AM depth: low rotor 21 %, high 40 % (DEPTH 64) */
            T->am[k] = 32767 - mulq15(dep, (32767 - c) >> 1);
            T->fm[k] = (sn * (sp >> (k ? 17 : 16))) >> 15;  /* Doppler: +-0.5 % fast (high), +-0.2 % (low) */
            if (r[RT_DOPL] != 64)
                T->fm[k] = (T->fm[k] * r[RT_DOPL]) >> 6;
        }
        if (r[RT_BAL] > 0)                              /* BAL: + the drum quieter, - the horn quieter */
            T->am[1] = mulq15(T->am[1], 32767 - r[RT_BAL] * 511);
        else if (r[RT_BAL] < 0)
            T->am[0] = mulq15(T->am[0], 32767 + r[RT_BAL] * 511);
        T->xover = r[RT_XOVER] == 80 ? DRW_XOVER : (uint32_t)r[RT_XOVER] * DRW_XOVER10;
    }
    T->rwet += ((rot ? 32767 : 0) - T->rwet) >> 5;      /* ~25 ms in or out */
    for (k = 0; k < 2u; k++) {
        T->am[k] = 32767 - mulq15(T->rwet, 32767 - T->am[k]);
        T->fm[k] = mulq15(T->fm[k], T->rwet);
    }
}

static void drawbar_note_on(track_t *t, voice_t *v)
{
    uint32_t vi = (uint32_t)(v - t->v) % NVOICE, k, held = 0;
    drw_vc_t *V = &drw_of(t)->v[vi];
    for (k = 0; k < NVOICE; k++)                        /* single trigger: another key of the part held? */
        if (&t->v[k] != v && t->v[k].active && t->v[k].gate && t->v[k].stage != 4u)
            held = 1;
    if (!V->init) {                                     /* the wheels: spread start phases, then free running */
        for (k = 0; k < DRW_NP; k++)
            V->ph[k] = (k * 0x9E3779B9u) ^ (vi * 0x7F4A7C15u);
        V->nz = 0x13579BDF + (int32_t)vi;
        V->init = 1;
    }
    V->perc = held ? 0 : 32767;
    V->clk = 32767;
    V->hp = 0;
    V->gate = 1;
}

static void drawbar_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    drw_part_t *P = drw_of(t);
    const drw_trk_t *T = &P->t;
    drw_vc_t *V = &P->v[(uint32_t)(v - t->v) % NVOICE];
    int32_t acc[CTL], perc1, clk0, clk1;
    uint32_t i, k;
    if (n > CTL)
        n = CTL;
    for (i = 0; i < n; i++)
        acc[i] = 0;
    perc1 = (int32_t)(((uint32_t)V->perc * T->pdec) >> 16);
    for (k = 0; k < DRW_NP; k++) {
        uint32_t x = m->inc, inc, ph = V->ph[k];
        int32_t g1 = T->g[k], g0 = V->gp[k], r;
        if ((int32_t)k == T->pk)
            g1 += mulq15(T->plev, perc1);
        while (x > DRW_FOLD_LIM[k])                     /* fold back above ~6 kHz */
            x >>= 1;
        inc = drw_ratio(k, x);
        r = inc < T->xover;                             /* 1: the low rotor */
        g1 = mulq15(g1, T->am[r]);
        inc += (uint32_t)((int32_t)(inc >> 16) * T->fm[r]);
        V->gp[k] = g1;
        if (!g0 && !g1) {
            V->ph[k] = ph + inc * n;
            continue;
        }
        if (g0 == g1) {
            for (i = 0; i < n; i++) {
                ph += inc;
                acc[i] += (SINE[ph >> 22] * g1) >> 15;
            }
        } else {                                        /* gain ramp over the block (CTL = 32) */
            int32_t gq = g0 << 5, dg = g1 - g0;
            for (i = 0; i < n; i++) {
                ph += inc;
                gq += dg;
                acc[i] += (SINE[ph >> 22] * (gq >> 5)) >> 15;
            }
        }
        V->ph[k] = ph;
    }
    V->perc = perc1;
    /* key click: high-passed noise, at the key contact and (half) at the release */
    if (V->gate && !v->gate) {
        V->gate = 0;
        V->clk = 16384;
    }
    clk0 = mulq15(V->clk, T->clk);
    if (clk0 > 16) {
        int32_t nst = V->nz, hp = V->hp, dc;
        V->clk = (V->clk * 3) >> 2;                     /* tau ~2.5 ms */
        clk1 = mulq15(V->clk, T->clk);
        dc = clk1 - clk0;
        clk0 <<= 5;
        for (i = 0; i < n; i++) {
            int32_t z = (int32_t)(noise32(&nst) >> 17) - 16384, y = z - hp;
            hp += y >> 3;
            clk0 += dc;
            acc[i] += (y * (clk0 >> 5)) >> 15;
        }
        V->nz = nst;
        V->hp = hp;
    }
    if (T->dg) {                                        /* overdrive */
        int32_t dg = T->dg, dcomp = T->dcomp;
        for (i = 0; i < n; i++)
            acc[i] = (softclip((acc[i] * dg) >> 9) * dcomp) >> 8;
    }
    for (i = 0; i < n; i++)                             /* acc: Q15 with headroom to 4.0 */
        out[i] += mulq15(mulq15(acc[i] >> 1, amp_at(m, i)), VOICE_FS) << 1;
}

static const preset_t DRAWBAR_PRESETS[] = {
    /* name, {REG, SUB, BODY, TOP, PERC, CLICK, DRIVE, ROTR}, {A D S R}, fenv, mono */
    {"SOUL ORGAN", {3, 0, 0, 0, 0, 25, 10, 1}, {0, 64, 127, 40}, 0, 0, FX(0, 0, 0, 22)},
    {"GOSPEL", {6, 0, 0, 0, 1, 60, 40, 2}, {0, 64, 127, 45}, 0, 0, FX(0, 0, 0, 30)},
    {"JAZZ ORGAN", {4, 0, 0, 0, 2, 50, 8, 1}, {0, 64, 127, 40}, 0, 0, FX(0, 0, 0, 22)},
    {"DIRTY B3", {5, 0, 0, 0, 0, 60, 90, 2}, {0, 64, 127, 40}, 0, 0, FX(30, 0, 6, 20)},
    /* 90s house: the percussive organ that plays bass lines and chords */
    {"HOUSE ORGN", {13, 2, 0, 1, 1, 80, 22, 0}, {0, 60, 96, 22}, 0, 0, FX(0, 10, 12, 18)},
    /* SLOOP 2.5: {REG, SUB, BODY, TOP, PERC, CLICK, DRIVE, ROTR} */
    {"ROCK ORGAN", {7, 0, 0, 0, 0, 50, 80, 2}, {0, 64, 127, 40}, 0, 0, FX(20, 0, 6, 20)},
    {"CHAPEL", {12, 0, 0, 0, 0, 10, 0, 1}, {20, 64, 127, 60}, 0, 0, FX(0, 0, 0, 50)},
    {"REGGAE ORG", {2, 0, 0, 0, 0, 60, 10, 0}, {0, 52, 0, 24}, 0, 0, FX(0, 0, 10, 15)},
};

static const engine_t ENG_DRAWBAR = {
    "WHEEL", {"BARS", "TONE"},
    {
        {"REG", F_ENUM, 0, DRW_CUSTOM, 4, N_DRW_REG, 0},
        {"SUB", F_INT, -8, 8, 0, 0, 0},
        {"BODY", F_INT, -8, 8, 0, 0, 0},
        {"TOP", F_INT, -8, 8, 0, 0, 0},
        {"PERC", F_ENUM, 0, 6, 0, N_DRW_PERC, 0},
        {"CLICK", F_PCT, 0, 127, 40, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
        {"ROTR", F_ENUM, 0, 2, 1, N_DRW_ROTR, 0},
    },
    DRAWBAR_PRESETS, sizeof(DRAWBAR_PRESETS) / sizeof(DRAWBAR_PRESETS[0]), -1, drawbar_note_on, drawbar_render,
    0xBC1F, {P_E0, P_E4, P_E6, P_E7}, 0, 0, 0, drawbar_block,
};
