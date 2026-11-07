/* SPDX-License-Identifier: GPL-3.0-only */
/* SMK-37 Pro FADER BANK screen (firmware/src/ui_fbank.c) on the UI harness: banks wrap, what each fader sets,
 * closes by itself; renders DIR/fbank-a.ppm, fbank-b.ppm. */
#define UI_PAGES_HARNESS_ONLY 1
#include "ui_pages_test.c"
static uint8_t smk_fbank, smk_fbank_new;           /* (ui_input.c, SMK build) */
static char knob_digit(uint32_t role) { return role == EN_SELECT ? '2' : '5'; }   /* (panel.c, SMK build: KNOB LAYOUT FM-1) */
#include "../firmware/src/ui_fbank.c"

static void fb_shot(const char *name)
{
    ui.frame++;
    fb_draw();
    ppm(name);
}

int main(int argc, char **argv)
{
    uint32_t i;
    int16_t *vp;
    outdir = argc > 1 ? argv[1] : "build/host";
    panel = PANEL_DEFAULT;
    layers_init();
    settings.palette = 4;
    palette_set(4);
    host_tracks_init();
    for (i = 0; i < NPART; i++) { set_engine_of(&trk[i], TRK_DEF[i][0]); apply_preset_to(&trk[i], TRK_DEF[i][1]); trk[i].engine = trk[i].eng_req; }
    go_home(); ui.force = 1; frame();

    fb_set(smk_fbank + 1u);
    check(fb_shown() && smk_fbank == 1 && smk_fbank_new, "FADER BANK: the next bank, its screen up");
    fb_set(smk_fbank + 3u);
    check(smk_fbank == 0, "SELECT back: bank A");
    fb_set(3u + 1u);
    check(smk_fbank == 0, "D -> A wraps");
    check(fb_param(0, TRK_DRUM, &vp) == &GP[G_DRLVL] && vp == &song.g[G_DRLVL], "bank A, fader 4: the drums' level");
    check(fb_param(1, TRK_DRUM, &vp) == &TP[P_TFLT] && vp == &TDRUM->p[P_TFLT], "bank B, fader 4: the drum track's filter");
    check(fb_param(2, 0, &vp) == &TP[P_REV] && vp == &trk[0].p[P_REV], "bank C, fader 1: track 1's reverb");
    ui.force = 1; fb_shot("fbank-a");
    trk[1].p[P_TFLT] = -30; trk[2].p[P_TFLT] = 20;
    fb_set(1); ui.force = 1; fb_shot("fbank-b");
    ui.layer = LY_STEP;
    check(!fb_shown(), "hidden while a layer is held");
    ui.layer = LY_PLAY;
    fm1_ms += 60000u;
    check(fb_shown(), "stays up until FADER BANK / PATCH");
    fb_set(3); fb_set(smk_fbank + 1u);
    check(smk_fbank == 0, "K2 turns the bank round (D -> A)");
    fb_close();
    check(!fb_shown(), "closed");
    /* organ mode: the selected track plays DRAWBAR: seven banks, E..G its bars, PERC, DRIVE, ROTOR */
    song.sel = 0;
    set_engine_of(&trk[0], ENGI_DRAWBAR); apply_preset_to(&trk[0], 0); trk[0].engine = trk[0].eng_req;
    check(fb_nbanks() == 7u, "organ: seven banks");
    fb_set(6u + 1u);
    check(smk_fbank == 0, "organ: G -> A wraps");
    fb_set(smk_fbank + fb_nbanks() - 1u);
    check(smk_fbank == 6, "organ: A back to G");
    {
        uint32_t b2 = drw_bar(&trk[0], 2);
        fb_organ_set(0, 0, 127);                        /* bank E, fader 1: the 16' bar full */
        check(trk[0].p[P_E0] == DRW_CUSTOM && drw_bar(&trk[0], 0) == 8u, "organ: a bar fader -> REG CUSTM, 16' at 8");
        check(drw_bar(&trk[0], 2) == b2, "organ: the other bars as they sounded");
        fb_organ_set(1, 3, 0);                          /* bank F, fader 4: 1 1/3' out */
        check(drw_bar(&trk[0], 7) == 0u, "organ: 1 1/3' at 0");
        fb_organ_set(2, 3, 127);
        check(trk[0].p[P_E7] == 2, "organ: bank G fader 4 up: ROTOR FAST");
        fb_organ_set(2, 3, 64);
        check(trk[0].p[P_E7] == 1, "organ: ROTOR fader middle: SLOW");
        fb_organ_set(2, 1, 127);
        check(trk[0].p[P_E4] == 6, "organ: PERC fader up: the last PERC");
        {
            int16_t e1 = drw_pack(&trk[0], 0), e2 = drw_pack(&trk[0], 1), e3 = drw_pack(&trk[0], 2);
            uint8_t keep[DRW_NP];
            memcpy(keep, drw_bars[0], sizeof keep);
            memset(drw_bars[0], 0, sizeof drw_bars[0]);
            trk[0].p[P_E2] = 3;
            check(drw_unpack(&trk[0], 0, e1) == 0 && drw_unpack(&trk[0], 2, e3) == 0, "organ: unpack the offsets");
            check(drw_unpack(&trk[0], 1, (int16_t)((e2 & ~31) | (-5 & 31))) == -5, "organ: a negative offset");
            check(!memcmp(keep, drw_bars[0], sizeof keep), "organ: the bars back out of a project");
        }
        smk_fbank = 5; ui.force = 1; fb_shot("fbank-organ");
        {   /* the ROTOR pages' values: through a project's spare bits and back; zeros (older projects): the defaults */
            int16_t e[8] = {DRW_CUSTOM, 0, 0, 0, 6, 127, 100, 2}, z[8] = {4, 0, 0, 0, 1, 40, 0, 1};
            static const int16_t W[RT_N] = {13, 92, 5, 127, 50, -64, 150, 0};
            uint32_t k2, ok = 1;
            memcpy(drw_rot[0], W, sizeof W);
            drw_rot_pack(&trk[0], e);
            check(drw_e_low(0, e[0]) == DRW_CUSTOM && drw_e_low(4, e[4]) == 6 && drw_e_low(5, e[5]) == 127 &&
                  drw_e_low(6, e[6]) == 100 && drw_e_low(7, e[7]) == 2, "rotor: REG, PERC, CLICK, DRV, ROTR kept under it");
            for (k2 = 0; k2 < RT_N; k2++)
                drw_rot[0][k2] = DRW_ROT_DESC[k2].def;
            drw_rot_unpack(&trk[0], e);
            for (k2 = 0; k2 < RT_N; k2++)
                ok &= drw_rot[0][k2] == W[k2];
            check(ok, "rotor: the eight values back out of a project");
            drw_rot_unpack(&trk[0], z);
            for (ok = 1, k2 = 0; k2 < RT_N; k2++)
                ok &= drw_rot[0][k2] == DRW_ROT_DESC[k2].def;
            check(ok, "rotor: an older project: the defaults");
        }
        {   /* FADER SCREEN: 4 S, 10 S, HOLD; a fader move keeps it up */
            fb_screen = 0; fb_set(5);
            fm1_ms += 3000u; fb_touch(); fm1_ms += 3000u; fb_expire();
            check(fb_shown(), "4 S: a fader move keeps the screen up");
            fm1_ms += 4500u; fb_expire();
            check(!fb_shown(), "4 S: closes 4 s after the last move");
            fb_screen = 1; fb_set(5); fm1_ms += 8000u; fb_expire();
            check(fb_shown(), "10 S: up after 8 s");
            fm1_ms += 3000u; fb_expire();
            check(!fb_shown(), "10 S: closed after 11 s");
            fb_screen = 2; fb_set(5); fm1_ms += 600000u; fb_expire();
            check(fb_shown(), "HOLD: up after 10 min");
            fb_close(); fb_screen = 0;
        }
    }
    set_engine_of(&trk[0], TRK_DEF[0][0]); trk[0].engine = trk[0].eng_req;
    check(fb_nbanks() == 4u, "not organ: four banks");
    printf("fbank: %s\n", fails ? "FAIL" : "ok");
    return fails != 0;
}
