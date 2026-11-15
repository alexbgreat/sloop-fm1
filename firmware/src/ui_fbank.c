/* SPDX-License-Identifier: GPL-3.0-only
 * SMK-37 Pro: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* The FADER BANK screen: the four banks, the active one lit, what faders 1..4 set (track colours).
 * FADER BANK opens it and then steps the bank, as < > and SELECT (K2, or K6) do; PATCH closes it, or it closes by itself. */
#define FB_NORM 4u                                 /* banks A..D */
#define FB_ORGAN 7u                                /* organ mode (the selected track plays DRAWBAR): E..G as well */
static uint32_t fb_nbanks(void) { return drw_organ(TSEL) ? FB_ORGAN : FB_NORM; }
static const char *const FB_NAME[FB_ORGAN] = {"LEVEL", "FILTER", "REVERB", "DELAY", "DRAWBARS 16' - 4'",
                                              "DRAWBARS 2 2/3' - 1 1/3'", "1'  PERC  DRIVE  ROTOR"};
/* organ banks E..G: fader i of bank 4 + g is bar 4g + i, but bank G's 2..4: PERC, DRIVE, ROTOR (DRAWBAR's E4, E6, E7) */
static const char *const FB_BAR[DRW_NP] = {"16'", "5 1/3'", "8'", "4'", "2 2/3'", "2'", "1 3/5'", "1 1/3'", "1'"};
static const uint8_t FB_OPAR[3] = {P_E4, P_E6, P_E7};
#define FB_SHOW_MS 4000u                           /* up this long after the last change (FADER SCREEN 4 S) */
/* HOME menu > FADERS > FADER SCREEN (kept in the SMK-37 settings): how long the screen stays up after the last bank
 * change or fader move; HOLD: until PATCH closes it, or another screen (the pad picker, a layer) takes its place */
enum { FBS_4S, FBS_10S, FBS_HOLD };                /* (ui_menu.c FBS_N, FBS_NAME) */
static uint8_t fb_screen;
static uint8_t fb_open;
static uint32_t fb_t, fb_frame;                    /* last action (ms); the frame drawn last */

static int fb_shown(void)
{
    return fb_open && !ui.menu && ui.layer == LY_PLAY && !ui.hold_kind;
}
static void fb_set(uint32_t b)                     /* bank b, the screen up */
{
    smk_fbank = (uint8_t)(b % fb_nbanks());
    smk_fbank_new = 1;
    fb_open = 1;
    fb_t = fm1_ms;
    ui.force = 1;
}
static void fb_close(void) { fb_open = 0; }
static void fb_expire(void)                        /* up FADER SCREEN's time after the last change or fader move */
{
    uint32_t ms = fb_screen == FBS_10S ? 10000u : FB_SHOW_MS;
    if (fb_open && fb_screen != FBS_HOLD && fm1_ms - fb_t > ms)
        fb_open = 0, ui.force = 1;
}
static void fb_touch(void)                         /* a fader moved (main.c smk_faders): the screen stays up */
{
    if (fb_open)
        fb_t = fm1_ms;
}
static int fb_left(void)                           /* drawn last frame, not now: the page needs a redraw */
{
    int l = fb_frame && fb_frame + 1u == ui.frame;
    if (l)
        fb_frame = 0;
    return l;
}

/* what fader i sets in bank b (main.c smk_faders) */
static const param_desc_t *fb_param(uint32_t b, uint32_t i, int16_t **vp)
{
    static const uint8_t TID[4] = {P_LEVEL, P_TFLT, P_REV, P_DLY}, GID[4] = {G_DRLVL, 0, G_DRREV, G_DRDLY};
    if (i == TRK_DRUM && b != 1u) {
        *vp = &song.g[GID[b & 3u]];
        return &GP[GID[b & 3u]];
    }
    *vp = &trk[i].p[TID[b & 3u]];
    return &TP[TID[b & 3u]];
}

/* organ mode: fader i of organ bank g (0..2) on the selected track; its label, value (text), where it stands (0..1000) */
static int32_t fb_organ(uint32_t g, uint32_t i, const char **lab, char *val, const char **unit)
{
    const track_t *t = TSEL;
    uint32_t k = 4u * g + i;
    *unit = "";
    if (k < DRW_NP) {
        uint32_t v = drw_bar(t, k);
        *lab = FB_BAR[k];
        val[0] = (char)('0' + v), val[1] = 0;
        return (int32_t)v * 1000 / 8;
    } else {
        const param_desc_t *d = &ENG_DRAWBAR.edit[FB_OPAR[(k - DRW_NP) % 3u] - P_E0];
        int16_t v = t->p[FB_OPAR[(k - DRW_NP) % 3u]];
        *lab = d->label;
        param_format(d, v, val, unit);
        return RATIO(d, v);
    }
}
/* a fader moved to v (0..127) in organ bank g: the selected track's bar, or PERC / DRIVE / ROTOR (main.c smk_faders) */
static void fb_organ_set(uint32_t g, uint32_t i, uint32_t v)
{
    track_t *t = TSEL;
    uint32_t k = 4u * g + i;
    if (k < DRW_NP) {
        drw_bar_set(t, k, (v * 8u + 63u) / 127u);
    } else {
        const param_desc_t *d = &ENG_DRAWBAR.edit[FB_OPAR[(k - DRW_NP) % 3u] - P_E0];
        int32_t n = d->max - d->min + 1;
        t->p[FB_OPAR[(k - DRW_NP) % 3u]] = (int16_t)(d->fmt == F_ENUM ? d->min + (int32_t)v * n / 128 :
                                                     d->min + (int32_t)v * (d->max - d->min) / 127);
    }
}

static void fb_draw(void)
{
    static uint32_t shown;
    uint32_t nb = fb_nbanks(), b = smk_fbank % nb, i, half, sig = b * 7919u + song.sel;
    int16_t *vp;
    if (fb_frame + 1u != ui.frame) {               /* another screen was up: all of it */
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.force = 1;
    }
    fb_frame = ui.frame;
    for (i = 0; i < 4u; i++) {
        if (b >= FB_NORM) {
            char v[12];
            const char *l, *u;
            sig = sig * 31u + (uint32_t)(fb_organ(b - FB_NORM, i, &l, v, &u) + 200);
        } else {
            fb_param(b, i, &vp);
            sig = sig * 31u + (uint32_t)(*vp + 200);
        }
    }
    if (!ui.force && sig == shown)
        return;
    shown = sig;
    for (half = 0; half < 2u; half++) {
        cv_begin(240, 120, C_BLACK);
        if (!half) {
            cv_text(4, 2, &FONT_L, "FADER BANK", C_WHITE);
            if (nb == FB_NORM) {
                for (i = 0; i < 4u; i++) {         /* the banks */
                    int32_t x = (int32_t)i * 60 + 2, on = i == b;
                    char l[2] = {(char)('A' + i), 0};
                    cv_rect(x, 42, 56, 74, on ? C_WHITE : TE_G2);
                    if (!on)
                        cv_rect(x + 1, 43, 54, 72, TE_G1);
                    cv_text(x + 28 - text_w(&FONT_L, l) / 2, 48, &FONT_L, l, on ? C_BLACK : TE_G3);
                    cv_text(x + 28 - text_w(&FONT_S, FB_NAME[i]) / 2, 90, &FONT_S, FB_NAME[i], on ? C_BLACK : TE_G3);
                }
            } else {                               /* organ mode: seven banks, E..G the organ's (the engine colour) */
                for (i = 0; i < nb; i++) {
                    int32_t x = (int32_t)i * 34 + 1, on = i == b;
                    char l[2] = {(char)('A' + i), 0};
                    uint16_t c = i >= FB_NORM ? ENG_DRAWBAR.color : C_WHITE;
                    cv_rect(x, 40, 32, 44, on ? c : TE_G2);
                    if (!on)
                        cv_rect(x + 1, 41, 30, 42, TE_G1);
                    cv_text(x + 16 - text_w(&FONT_L, l) / 2, 50, &FONT_L, l, on ? C_BLACK : i >= FB_NORM ? c : TE_G3);
                }
                cv_text((240 - text_w(&FONT_S, FB_NAME[b])) / 2, 94, &FONT_S, FB_NAME[b],
                        b >= FB_NORM ? ENG_DRAWBAR.color : C_WHITE);
            }
        } else {
            for (i = 0; b >= FB_NORM && i < 4u; i++) {   /* organ: F1 16' ... value, a bar (the selected track) */
                int32_t y = 4 + (int32_t)i * 24, w;
                char val[12], v2[20], f[3] = {'F', (char)('1' + i), 0};
                const char *lab, *unit;
                w = fb_organ(b - FB_NORM, i, &lab, val, &unit) * 204 / 1000;
                str_cpy(v2, val, sizeof v2);
                str_cpy(v2 + str_len(v2), unit, sizeof v2 - str_len(v2));
                cv_text(4, y, &FONT_S, f, ENG_DRAWBAR.color);
                cv_text(32, y, &FONT_S, lab, C_WHITE);
                cv_text(236 - text_w(&FONT_S, v2), y, &FONT_S, v2, C_WHITE);
                cv_rect(32, y + 17, 204, 3, TE_G1);
                cv_rect(32, y + 17, w < 1 ? 1 : w, 3, ENG_DRAWBAR.color);
            }
            for (i = 0; b < FB_NORM && i < 4u; i++) {   /* the faders: F1 TRACK 1 ... value, a bar */
                int32_t y = 4 + (int32_t)i * 24, w;
                char lab[10] = "TRACK 1", val[12], v2[20];
                const char *unit = "";
                const param_desc_t *d = fb_param(b, i, &vp);
                char f[3] = {'F', (char)('1' + i), 0};
                if (i == TRK_DRUM)
                    str_cpy(lab, "DRUMS", sizeof lab);
                else
                    lab[6] = (char)('1' + i);
                param_format(d, *vp, val, &unit);
                str_cpy(v2, val, sizeof v2);
                str_cpy(v2 + str_len(v2), unit, sizeof v2 - str_len(v2));
                cv_text(4, y, &FONT_S, f, TE_COL[i]);
                cv_text(32, y, &FONT_S, lab, TE_COL[i]);
                cv_text(236 - text_w(&FONT_S, v2), y, &FONT_S, v2, C_WHITE);
                w = RATIO(d, *vp) * 204 / 1000;
                cv_rect(32, y + 17, 204, 3, TE_G1);
                cv_rect(32, y + 17, w < 1 ? 1 : w, 3, TE_COL[i]);
            }
            {
                char h[24] = "< >  K2  FADER BANK";
                h[6] = knob_digit(EN_SELECT);          /* (SELECT: K2, or K6, HOME menu > SCREEN > KNOB LAYOUT) */
                cv_text((240 - text_w(&FONT_S, h)) / 2, 100, &FONT_S, h, TE_G3);
            }
        }
        cv_blit(0, half * 120u);
    }
}
