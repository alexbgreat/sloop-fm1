/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* I2S output (ALNK0 -> external codec) and the audio ISR: each half buffer is
 * rendered in blocks of CTL samples by mix_block (fx.c: events -> each synth part
 * -> dist -> level / pan -> sends -> drums -> buses -> master), then scaled to 24-bit stereo. */
/* registers: hal/fm1_audio.h */
#define HALF_WORDS (HALF_FRAMES * 2u)
#define OUT_SHIFT 7               /* Q15 -> 24-bit, -6 dBFS ceiling */

static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));

/* diagnostics, kept across resets and UBOOT entry: read with `fm1t memr` */
#define DBG_MAGIC 0x44424731u                       /* "DBG1" */
struct felucca_dbg {
    uint32_t magic, halves, max_us, nested;
    volatile uint32_t in_audio;           /* TIMER5 reads it to decide what may nest in the render */
    uint32_t late, timer_irqs, ui_frames;
    uint32_t last_us, cpu_q8, boots;
    uint32_t stage, page, home;           /* where the main loop is (breadcrumbs) */
    uint32_t prev_stage, prev_page, prev_home, prev_rst, prev_frames;   /* as found at boot */
} felucca_dbg __attribute__((section(".noinit")));
static volatile uint32_t audio_halves, audio_max_us;
static volatile uint32_t t5_nested_ticks;              /* TIMER4 ticks TIMER5 spent nested in this ISR (main.c) */
#define SCOPE_N 512u
static int16_t scope_buf[SCOPE_N], scope_bufr[SCOPE_N];   /* left, right (the visualiser): the mix at full level */
static uint32_t scope_w;

#if FELUCCA_UAC_OUT
/* the USB audio output (usb.c): the host's frames at the I2S clock. spk_half, when a render starts, steers the
 * resampler's step (Q16) from the ring's fill (PI, filtered: the fill there is past the render's saw tooth, the
 * packets' 44 / 45 frames and the poll's phase remain); spk_mix adds the stream to the master (after the USB input's
 * tap: the computer does not record its own sound), at the MASTER knob's level */
static uint32_t spk_frac, spk_step = 65536u, spk_gain;
static int32_t spk_ef, spk_int;
static uint8_t spk_go;
static void spk_half(void)
{
    uint32_t w = spk_w, fill = w - spk_r, need;
    int32_t st;
    if (!spk_alt || fill > SPK_N) {                     /* no stream: the ring follows the host's writes */
        spk_r = w;
        spk_go = 0;
        return;
    }
    if (fill > SPK_N - HALF_FRAMES - SPK_MAXP / 4u) {   /* too far behind: back to the target */
        spk_r = w - SPK_TGT;
        fill = SPK_TGT;
    }
    if (!spk_go) {
        if (fill < SPK_TGT)
            return;                                     /* priming */
        spk_go = 1;
        spk_gain = 0;
        spk_frac = 0;
        spk_ef = 0;
    }
    need = ((HALF_FRAMES * spk_step) >> 16) + 2u;
    if (fill < need) {                                  /* the host stopped (or fell behind): prime again */
        spk_go = 0;
        return;
    }
    spk_ef += (((int32_t)fill - (int32_t)SPK_TGT) * 16 - spk_ef) >> 5;   /* Q4, ~190 ms */
    spk_int += spk_ef >> 4;
    if (spk_int > (1 << 20))
        spk_int = 1 << 20;
    if (spk_int < -(1 << 20))
        spk_int = -(1 << 20);
    st = 65536 + (spk_ef >> 4) + (spk_int >> 10);
    if (st > 65536 + 1311)
        st = 65536 + 1311;                              /* +-2 % */
    if (st < 65536 - 1311)
        st = 65536 - 1311;
    spk_step = (uint32_t)st;
}
static void spk_mix(int32_t *out, uint32_t n)
{
    uint32_t i, r, f, g, m;
    if (!spk_go)
        return;
    r = spk_r, f = spk_frac, g = spk_gain;
    m = song.master_q12 > 4096u ? 4096u : song.master_q12;
    for (i = 0; i < n; i++) {
        uint32_t a = spk_ring[r & (SPK_N - 1u)], b = spk_ring[(r + 1u) & (SPK_N - 1u)];
        int32_t fl = (int32_t)(f >> 1), k, l = (int16_t)a, rr = (int16_t)(a >> 16), v;
        l += ((int32_t)(int16_t)b - l) * fl >> 15;
        rr += ((int32_t)(int16_t)(b >> 16) - rr) * fl >> 15;
        if (g < 4096u)
            g++;                                        /* ~90 ms fade in */
        k = (int32_t)((m * g) >> 12);
        v = out[2u * i] + ((l * k) >> 12);
        out[2u * i] = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
        v = out[2u * i + 1u] + ((rr * k) >> 12);
        out[2u * i + 1u] = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
        f += spk_step;
        r += f >> 16;
        f &= 0xFFFFu;
    }
    spk_r = r, spk_frac = f, spk_gain = g;
}
#endif

static void audio_block(int32_t *out, uint32_t n)       /* mix (fx.c), then Q15 -> 24 bit */
{
    uint32_t i;
    mix_block(out, n);
#if FELUCCA_UAC
    uac_tap(usb_full_now ? usb_out : out, n);           /* the USB audio input: the master output, or at full level */
#endif
#if FELUCCA_UAC_OUT
    spk_mix(out, n);                                    /* the USB audio output: the computer's sound */
#endif
    for (i = 0; i < n; i++) {
        if (i & 1u)
        {
            scope_bufr[scope_w & (SCOPE_N - 1u)] = vis_tap[2u * i + 1u];   /* (fx.c: MASTER all the way up) */
            scope_buf[scope_w++ & (SCOPE_N - 1u)] = vis_tap[2u * i];
        }
        out[2u * i] <<= OUT_SHIFT;
        out[2u * i + 1u] <<= OUT_SHIFT;
    }
}

/* overload: shed_voice (voice.c) */
static volatile uint8_t shed_req;
static uint8_t shed_over;                      /* bit k: the half k halves ago was over 85 % */

void fm1_alnk0_irq(void)                       /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p;
    uint32_t t0;
    felucca_dbg.in_audio = 1;                   /* first: TIMER5 nests from here on (main.c) */
    p = fm1_audio_pending();
    t0 = fm1_ticks();
    t5_nested_ticks = 0;
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half(), b, us;
        int32_t *o = &abuf[half * HALF_WORDS];
        if (shed_req) {
            shed_req = 0;
            shed_voice();
        }
#if FELUCCA_UAC
        uac_render_start();
        usb_full_now = (uint8_t)(usb_full && uac.feed);  /* menu USB AUDIO = FULL, while the computer records */
#if FELUCCA_UAC_OUT
        spk_half();
#endif
#endif
        for (b = 0; b < HALF_FRAMES; b += CTL)
            audio_block(o + 2u * b, CTL);
        fm1_audio_ack_half();
        audio_halves++;
#if FELUCCA_CPU1
        cpu1_load_half(HALF_FRAMES * 1000000u / FS);     /* core 1's load and core 0's wait for it (fx.c) */
#endif
        {   /* the deadline sees the render and TIMER5 nested in it (the USB audio stream); the load
             * figures the render alone (after Felucca 1.0) */
            uint32_t all = fm1_ticks() - t0;
            us = (all - t5_nested_ticks) / FM1_TICKS_PER_US;
            shed_over = (uint8_t)(shed_over << 1 | (all / FM1_TICKS_PER_US * 100u > (HALF_FRAMES * 1000000u / FS) * 85u));
        }
        if (us > audio_max_us)
            audio_max_us = us;
        if ((shed_over & 3u) == 3u)
            shed_req = 1;                               /* two in a row */
        song.cpu_q8 = (song.cpu_q8 * 15u + (us * 256u) / (HALF_FRAMES * 1000000u / FS)) / 16u;
        if (fm1_audio_free_half() != half)
            felucca_dbg.late++;                         /* the DMA moved on while we rendered */
        felucca_dbg.halves++;
        felucca_dbg.last_us = us;
        if (us > felucca_dbg.max_us)
            felucca_dbg.max_us = us;
        felucca_dbg.cpu_q8 = song.cpu_q8;
    }
    felucca_dbg.in_audio = 0;
}
extern void isr_alnk0(void);

static void audio_init(void)                   /* hal/fm1_audio.h */
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
}
