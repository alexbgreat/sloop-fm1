/* SPDX-License-Identifier: GPL-3.0-only */
/* SMK-37: GLO > EQ, the screen and the controls of the 16-band graphic EQ (geq.h; params.c has the page and its
 * descriptors). The page shows the 16 sliders as bars; the four dials set four neighbouring bands (eq_win .. +3, in the
 * dials' colours); one band is the current one (eq_cur, white mark):
 *   pads        the 16 pads are the 16 bands, left to right and top row first (reading order): a pad picks its band
 *               (and the dials move to it); lit by their gain: green up, red down, the current one white, the four on
 *               the dials with a blue tint
 *   SELECT      scrolls the band by band (K2, or K6 with KNOB LAYOUT = SMK; the tempo is on the TRACKS page)
 *   K5..K8      the gain of the four bands on the dials, 0.5 dB a detent (K1..K4 with KNOB LAYOUT = SMK)
 *   OCT-        the current band to 0 dB; twice quickly: every band to 0 dB
 *   OCT+        EQ ON / OFF (lit while on); a dial turned while it is OFF turns it on
 * The sliders are kept with the SMK-37 settings (project.c smkset), saved a few seconds after the last change. */
static int fb_shown(void);                          /* ui_fbank.c */
static void draw_column(uint32_t c, const char *label, const char *val, const char *unit, uint16_t vc, int32_t ratio,
                        uint32_t icon);   /* ui_draw.c */

static uint32_t eq_save_t, eq_flat_t;               /* the last change (ms) / the last OCT- (ms) */
static uint8_t eq_save_due;

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
static void eq_goto(uint32_t b)                     /* the current band; the dials move to it */
{
    eq_cur = (uint8_t)(b % EQ_N);
    eq_win = (uint8_t)(eq_cur > EQ_N - 4u ? EQ_N - 4u : eq_cur);
    ui_say("EQ ", EQ_DESC[eq_cur].label);
    ui.force = 1;
}
static void eq_pick(uint32_t pad) { eq_goto(pad); }  /* pad p = band p */
static void eq_scroll(int32_t s) { eq_goto((uint32_t)clamp((int32_t)eq_cur + s, 0, (int32_t)EQ_N - 1)); }

static void eq_edit(uint32_t slot, int32_t steps)   /* a dial turned */
{
    uint32_t b = (eq_win + slot) % EQ_N;
    int32_t v = clamp(eq_t[b] + accel(EN_K1 + slot, steps, 2 * EQ_HMAX), -EQ_HMAX, EQ_HMAX);
    if (eq_off) {
        eq_off = 0;
        ui_message("EQ ON");
    }
    if (v != eq_t[b])
        eq_t[b] = (int16_t)v;
    eq_cur = (uint8_t)b;
    eq_touch();
}

static void eq_buttons(uint32_t *pressed)           /* OCT- / OCT+ on the EQ page */
{
    uint32_t dn = 1u << panel.btn[B_OCTDN], up = 1u << panel.btn[B_OCTUP], i;
    if (!eq_pads_on() || !(*pressed & (dn | up)))
        return;
    if (*pressed & dn) {
        if (eq_flat_t && fm1_ms - eq_flat_t < 450u) {      /* twice quickly: all of them */
            for (i = 0; i < EQ_N; i++)
                eq_t[i] = 0;
            eq_flat_t = 0;
            ui_message("EQ FLAT");
        } else {
            eq_t[eq_cur] = 0;
            eq_flat_t = fm1_ms;
            ui_say("EQ ", EQ_DESC[eq_cur].label);
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

static void eq_leds(void)                           /* the pads: gain green up / red down, the dials' four tinted, the current white */
{
    uint32_t p;
    for (p = 0; p < EQ_N; p++) {
        int32_t v = eq_t[p], a = v < 0 ? -v : v;
        int32_t r = v < 0 ? 30 + a * 7 : 0, g = v > 0 ? 30 + a * 7 : 0, b = 0;
        if (!v)
            r = g = b = 10;
        if (p >= eq_win && p < eq_win + 4u) {
            b = 80;
            if (!v)
                r = g = 0;
        }
        if (p == eq_cur)
            r = g = b = 200;
        if (eq_off)
            r >>= 2, g >>= 2, b >>= 2;
        padled_set(p, (uint8_t)(r > 255 ? 255 : r), (uint8_t)(g > 255 ? 255 : g), (uint8_t)(b > 255 ? 255 : b));
    }
}

/* the graph: 16 bars around the 0 dB line (+-12 dB = +-36 px); the four on the dials in the dials' colours, the current
 * one marked below. Canvas rows 0..99 (draw_graph has set cv_oy) */
UI_SMALL static void graph_eq(void)
{
    uint32_t i;
    const int32_t base = 44;
    cv_rect(2, base, 236, 1, C_LINE);
    cv_rect(2, base - 36, 4, 1, C_LINE);
    cv_rect(234, base - 36, 4, 1, C_LINE);
    cv_rect(2, base + 36, 4, 1, C_LINE);
    cv_rect(234, base + 36, 4, 1, C_LINE);
    for (i = 0; i < EQ_N; i++) {
        int32_t x = 4 + (int32_t)i * 14, v = eq_t[i], h = v * 3 / 2;
        int in_win = i >= eq_win && i < eq_win + 4u;
        uint16_t col = eq_off ? C_DIM : in_win ? TE_COL[(i - eq_win) & 3u] : C_GRAY;
        if (v > 0)
            cv_rect(x, base - h, 10, h, col);
        else if (v < 0)
            cv_rect(x, base + 1, 10, -h, col);
        else
            cv_rect(x, base - 1, 10, 3, in_win && !eq_off ? col : C_DIM);
        if (i == eq_cur)
            cv_rect(x, 87, 10, 3, C_WHITE);
    }
    cv_rect(4 + (int32_t)eq_win * 14, 83, 52, 1, C_GRAY);   /* the four on the dials */
}

static uint32_t eq_signature(void)                  /* what the graph shows, for graph_signature */
{
    uint32_t h = eq_cur * 131u + eq_win * 17u + eq_off * 7919u, i;
    for (i = 0; i < EQ_N; i++)
        h = (h ^ (uint32_t)(eq_t[i] + 64)) * 16777619u;
    return h;
}

UI_SMALL static void eq_columns(void)               /* the four columns: band, gain; the current band's value white */
{
    uint32_t c;
    for (c = 0; c < 4u; c++) {
        uint32_t b = (eq_win + c) % EQ_N;
        const param_desc_t *d = &EQ_DESC[b];
        char val[12];
        const char *unit;
        param_format(d, eq_t[b], val, &unit);
        draw_column(c, d->label, val, unit, eq_off ? C_DIM : b == eq_cur ? C_WHITE : VAL(c), RATIO(d, eq_t[b]), ICON_NONE);
    }
}
