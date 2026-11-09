/* SPDX-License-Identifier: GPL-3.0-only */
/* SMK-37: GLO > EQ, the screen and the controls of the 5-band parametric EQ (peq.h; params.c has the page and its
 * descriptors). The screen: the live spectrum of the mix (ui_vis.c eq_spec_update: the visualiser's FFT), the EQ's
 * response curve over it (white; the picked band's own in its colour) and the five bands as handles at their
 * FREQ / GAIN, 20 Hz .. 20 kHz across, +-12 dB up and down. One band is picked (eq_cur) and is on the dials:
 *   pads 1..5   the five bands (top row), lit in their colours (dim while flat or OFF, white while picked)
 *   SELECT      the next / previous band (K2, or K6 with KNOB LAYOUT = SMK; the tempo is on the TRACKS page)
 *   K5..K8      TYPE, FREQ, GAIN, Q of the picked band (K1..K4 with KNOB LAYOUT = SMK)
 *   OCT-        the picked band back to its default (flat); twice quickly: every band
 *   OCT+        EQ ON / OFF (lit while on); a dial turned while it is OFF turns it on
 * The bands are kept with the SMK-37 settings (project.c smkset), saved a few seconds after the last change. */
static int fb_shown(void);                          /* ui_fbank.c */
static void draw_column(uint32_t c, const char *label, const char *val, const char *unit, uint16_t vc, int32_t ratio,
                        uint32_t icon);   /* ui_draw.c */
static void eq_spec_update(uint8_t *h, uint32_t hmax);   /* ui_vis.c: the spectrum, a height per column */

static const char *const EQT_LONG[EQT_N] = {"OFF", "BELL", "LOW SHELF", "HIGH SHELF", "LOW CUT", "HIGH CUT"};
static const uint8_t EQ_PADC[EQ_N][3] = {{40, 124, 255}, {30, 204, 112}, {255, 198, 24}, {255, 98, 26}, {190, 96, 255}};
static uint16_t eq_col(uint32_t b) { return b < 4u ? TE_COL[b] : RGB(190, 96, 255); }

static uint32_t eq_save_t, eq_flat_t;               /* the last change (ms) / the last OCT- (ms) */
static uint8_t eq_save_due;
static uint8_t eq_spec[EQ_COLS];                    /* the spectrum's heights (px), falling */
static int16_t eq_cv[EQ_COLS], eq_cvb[EQ_COLS];     /* the curve, all bands / the picked one (1/16 dB) */
static uint32_t eq_cv_sig = 1;

static int eq_page(void) { return !ui.home && !ui.menu && cur_page()->scope == SC_EQ; }
/* the pads are the EQ's: its page up, nothing else claiming them (a layer, the sound picker, the FADER BANK screen) */
static int eq_pads_on(void)
{
    return eq_page() && ui.layer == LY_PLAY && !ui.hold_kind && !rec_wait && !ft_on && !pick_shown() && !fb_shown();
}
static void eq_touch(void)
{
    eq_save_t = fm1_ms;
    eq_save_due = 1;
    ui.force = 1;
}
static void eq_tick(void)                           /* once a frame: the settings, a few seconds after the last change */
{
    if (eq_save_due && fm1_ms - eq_save_t > 3000u) {
        eq_save_due = 0;
        settings_save();
    }
}
static void eq_say(void)
{
    static char n[14];
    n[0] = (char)('1' + eq_cur % EQ_N), n[1] = ' ', n[2] = 0;
    str_cpy(n + 2, EQT_LONG[eq_clamp(eq_p[eq_cur][EQP_TYPE], 0, EQT_N - 1)], 11);
    ui_say("EQ ", n);
}
static void eq_goto(uint32_t b)                     /* the picked band, on the dials */
{
    eq_cur = (uint8_t)(b % EQ_N);
    eq_say();
    ui.force = 1;
}
static void eq_pick(uint32_t pad)                   /* pads 1..5 (the top row's first five) */
{
    if (pad < EQ_N)
        eq_goto(pad);
}
static void eq_scroll(int32_t s) { eq_goto((uint32_t)(((int32_t)eq_cur + (s > 0 ? 1 : -1) + (int32_t)EQ_N) % (int32_t)EQ_N)); }

static void eq_edit(uint32_t slot, int32_t steps)   /* a dial turned: a value of the picked band */
{
    const param_desc_t *d = &EQ_DESC[slot & 3u];
    int16_t *v = &eq_p[eq_cur][slot & 3u];
    int32_t n = clamp(*v + accel(EN_K1 + slot, steps, d->max - d->min), d->min, d->max);
    if (eq_off) {
        eq_off = 0;
        ui_message("EQ ON");
    }
    if (n != *v) {
        *v = (int16_t)n;
        if (slot == EQP_TYPE)
            eq_say();
    }
    eq_touch();
}

static void eq_buttons(uint32_t *pressed)           /* OCT- / OCT+ on the EQ page */
{
    uint32_t dn = 1u << panel.btn[B_OCTDN], up = 1u << panel.btn[B_OCTUP];
    if (!eq_pads_on() || !(*pressed & (dn | up)))
        return;
    if (*pressed & dn) {
        if (eq_flat_t && fm1_ms - eq_flat_t < 450u) {      /* twice quickly: every band */
            memcpy(eq_p, EQ_DEF, sizeof eq_p);
            eq_flat_t = 0;
            ui_message("EQ FLAT");
        } else {
            memcpy(eq_p[eq_cur], EQ_DEF[eq_cur], sizeof eq_p[0]);
            eq_flat_t = fm1_ms;
            eq_say();
        }
        eq_touch();
    }
    if (*pressed & up) {
        eq_off = (uint8_t)!eq_off;
        ui_message(eq_off ? "EQ OFF" : "EQ ON");
        eq_touch();
    }
    *pressed &= ~(dn | up);
}

static int eq_band_flat(uint32_t b) { return eq_flat((uint32_t)eq_clamp(eq_p[b][EQP_TYPE], 0, EQT_N - 1), eq_p[b][EQP_GAIN]); }

static void eq_leds(void)                           /* pads 1..5: the bands in their colours, the picked one white */
{
    uint32_t p;
    for (p = 0; p < 16u; p++) {
        uint32_t r = 0, g = 0, b = 0;
        if (p < EQ_N) {
            uint32_t k = eq_band_flat(p) ? 5u : 1u;     /* flat / OFF: dim */
            r = EQ_PADC[p][0] / k, g = EQ_PADC[p][1] / k, b = EQ_PADC[p][2] / k;
            if (p == eq_cur)
                r = g = b = 210;
            if (eq_off)
                r >>= 2, g >>= 2, b >>= 2;
        }
        padled_set(p, (uint8_t)r, (uint8_t)g, (uint8_t)b);
    }
}

/* the graph: canvas rows 0..99 (draw_graph has set cv_oy). The title on top, then 20 Hz .. 20 kHz across (a column
 * 1/80 decade), 0 dB at row 56, 3 px a dB (+-12 dB: rows 20 .. 92); the spectrum rises from the bottom */
#define EQ_Y0 56
#define EQ_YTOP 18
static int32_t eq_y(int32_t db16) { return eq_clamp(EQ_Y0 - db16 * 3 / 16, EQ_YTOP, 99); }
static uint32_t eq_x(uint32_t f) { return (f * 239u + 60u) / 120u; }   /* a FREQ step (1/40 decade) -> its column */

static uint32_t eq_params_sig(void)
{
    uint32_t h = 2166136261u + eq_cur * 131u + eq_off * 7919u, b, k;
    for (b = 0; b < EQ_N; b++)
        for (k = 0; k < EQP_N; k++)
            h = (h ^ (uint32_t)(eq_p[b][k] + 1000)) * 16777619u;
    return h;
}
UI_SMALL static void eq_curve(void)                 /* the curve, when a value moved */
{
    uint32_t sig = eq_params_sig(), b, x;
    if (sig == eq_cv_sig)
        return;
    eq_cv_sig = sig;
    memset(eq_cv, 0, sizeof eq_cv);
    memset(eq_cvb, 0, sizeof eq_cvb);
    for (b = 0; b < EQ_N; b++) {
        uint32_t type, f, q;
        int32_t e;
        eqc_t c;
        eq_get(b, &type, &f, &e, &q);
        if (eq_flat(type, e))
            continue;
        eq_calc(&c, type, f, e, q);
        for (x = 0; x < EQ_COLS; x++) {
            int32_t d = eq_db16(&c, type, x);
            eq_cv[x] = (int16_t)eq_clamp(eq_cv[x] + d, -30000, 30000);
            if (b == eq_cur)
                eq_cvb[x] = (int16_t)d;
        }
    }
}

UI_SMALL static void graph_eq(void)
{
    static const uint16_t GRID[3] = {56, 135, 215};    /* 100 Hz, 1 kHz, 10 kHz */
    uint32_t i, b;
    int32_t px = 0;
    uint16_t cc = eq_col(eq_cur);
    eq_spec_update(eq_spec, 99u - EQ_YTOP);
    eq_curve();
    {   /* the title: the picked band and its type; EQ OFF on the right */
        char t[16];
        t[0] = (char)('1' + eq_cur), t[1] = ' ', t[2] = 0;
        str_cpy(t + 2, EQT_LONG[eq_clamp(eq_p[eq_cur][EQP_TYPE], 0, EQT_N - 1)], 12);
        cv_text(4, 0, &FONT_S, t, eq_off ? C_DIM : cc);
        if (eq_off)
            cv_text(236 - text_w(&FONT_S, "EQ OFF"), 0, &FONT_S, "EQ OFF", C_AMB);
    }
    for (i = 0; i < 3u; i++)
        cv_rect(GRID[i], EQ_YTOP, 1, 99 - EQ_YTOP, C_LINE);
    for (i = 0; i < EQ_COLS; i++)                       /* the spectrum */
        if (eq_spec[i])
            cv_rect((int32_t)i, 99 - eq_spec[i], 1, eq_spec[i], C_DIM);
    cv_rect(0, EQ_Y0, 240, 1, C_GRAY);                  /* 0 dB, +-6, +-12 */
    for (i = 0; i < 240u; i += 4u) {
        cv_pset((int32_t)i, EQ_Y0 - 18, C_LINE), cv_pset((int32_t)i, EQ_Y0 + 18, C_LINE);
        cv_pset((int32_t)i, EQ_Y0 - 36, C_LINE), cv_pset((int32_t)i, EQ_Y0 + 36, C_LINE);
    }
    if (!eq_off) {
        for (i = 0; i < EQ_COLS; i++) {                 /* the picked band's own curve, then the sum */
            int32_t y = eq_y(eq_cvb[i]);
            if (i)
                cv_line((int32_t)i - 1, px, (int32_t)i, y, cc);
            px = y;
        }
        for (i = 0; i < EQ_COLS; i++) {
            int32_t y = eq_y(eq_cv[i]);
            if (i) {
                cv_line((int32_t)i - 1, px, (int32_t)i, y, C_HI);
                cv_line((int32_t)i - 1, px + 1, (int32_t)i, y + 1, C_HI);
            }
            px = y;
        }
    }
    for (b = 0; b < EQ_N; b++) {                        /* the handles, the picked one last (on top) */
        uint32_t k = (b + eq_cur + 1u) % EQ_N, type, f, q, x;
        int32_t e, y;
        eq_get(k, &type, &f, &e, &q);
        if (type == EQT_OFF)
            continue;
        x = eq_x(f);
        y = eq_y(eq_has_gain(type) ? e * 8 : -48);    /* a cut: at its -3 dB point */
        if (k == eq_cur) {
            cv_rect((int32_t)x - 4, y - 4, 9, 9, eq_off ? C_DIM : C_WHITE);
            cv_rect((int32_t)x - 3, y - 3, 7, 7, eq_off ? C_BLACK : cc);
        } else {
            cv_rect((int32_t)x - 2, y - 2, 5, 5, eq_off ? C_DIM : eq_col(k));
        }
    }
}

static uint32_t eq_signature(void)                  /* what the graph shows, for graph_signature: the spectrum moves */
{
    return eq_params_sig() ^ (ui.frame / 2u) * 2654435761u;
}

UI_SMALL static void eq_columns(void)               /* the four columns: TYPE FREQ GAIN Q of the picked band */
{
    uint32_t c;
    for (c = 0; c < 4u; c++) {
        const param_desc_t *d = &EQ_DESC[c];
        int32_t v = eq_p[eq_cur][c];
        uint32_t type = (uint32_t)eq_clamp(eq_p[eq_cur][EQP_TYPE], 0, EQT_N - 1);
        int unused = type == EQT_OFF ? c != EQP_TYPE : c == EQP_GAIN && !eq_has_gain(type);
        char val[12];
        const char *unit = "";
        if (unused) {
            str_cpy(val, "--", 3);
        } else {
            param_format(d, v, val, &unit);
        }
        draw_column(c, c == EQP_TYPE ? EQ_BAND_L[eq_cur] : d->label, val, unit,
                    eq_off || unused ? C_DIM : VAL(c), unused ? -1 : RATIO(d, v), ICON_NONE);
    }
}
