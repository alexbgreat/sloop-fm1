/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the SMK-37's USB audio output descriptors (src/usb.c FELUCCA_UAC_OUT): the configuration parsed as a
 * host does: lengths, four interfaces, the AC header's collection (1 2 3) and its wTotalLength, terminals 3 (USB
 * streaming) -> 4 (speaker), interface 3's alt 0 / alt 1, its type I format (2 ch 16 bit 44.1) and EP 0x03
 * (isochronous adaptive, 192 B), next to the input's EP 0x84. The SIE is never touched. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifndef HALF_FRAMES
#define HALF_FRAMES 256
#endif
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define FELUCCA_OTA 0
#define FELUCCA_CDC 0
#define FELUCCA_UAC 1
#define FELUCCA_UAC_OUT 1
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#include "../firmware/src/usb.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("%-64s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        fails++;
}

static int walk(uint32_t mode)
{
    const uint8_t *c, *d;
    uint16_t cl, dl;
    uint32_t off, ac_sum = 0, ac_wt = 0, nif_alt0 = 0;
    int in_ac = 0, lens = 1, coll = 0, it3 = 0, ot4 = 0, if3a0 = 0, if3a1 = 0, fmt = 0, ep04 = 0, ep84 = 0, csep = 0;
    int cur_if = -1, cur_alt = -1;
    char name[96];
    usb_rate_mode = (uint8_t)mode;
    get_desc(0x0100, &d, &dl);
    get_desc(0x0200, &c, &cl);
    check("device: 18 bytes, bcdDevice with the output (+0.40)", dl == 18 && (d[12] & 0x40u));
    for (off = 0; off < cl; off += c[off]) {
        const uint8_t *p = c + off;
        if (p[0] < 2u || off + p[0] > cl) {
            lens = 0;
            break;
        }
        if (p[1] == 4u) {
            cur_if = p[2], cur_alt = p[3];
            in_ac = p[5] == 1u && p[6] == 1u;
            if (!p[3])
                nif_alt0++;
            if (p[2] == 3u && p[3] == 0u && p[4] == 0u && p[5] == 1u && p[6] == 2u)
                if3a0 = 1;
            if (p[2] == 3u && p[3] == 1u && p[4] == 1u && p[5] == 1u && p[6] == 2u)
                if3a1 = 1;
        } else if (p[1] == 0x24u && in_ac) {
            ac_sum += p[0];
            if (p[2] == 1u) {
                ac_wt = p[5] | (uint32_t)p[6] << 8;
                coll = p[0] == 11u && p[7] == 3u && p[8] == 1u && p[9] == 2u && p[10] == 3u;
            }
            if (p[2] == 2u && p[3] == 3u && p[4] == 0x01u && p[5] == 0x01u && p[7] == 2u)
                it3 = 1;
            if (p[2] == 3u && p[3] == 4u && p[4] == 0x01u && p[5] == 0x03u && p[7] == 3u)
                ot4 = 1;
        } else if (p[1] == 0x24u && cur_if == 3 && cur_alt == 1) {
            if (p[2] == 2u)
                fmt = p[0] == 11u && p[3] == 1u && p[4] == 2u && p[5] == 2u && p[6] == 16u && p[7] == 1u &&
                      (p[8] | p[9] << 8 | p[10] << 16) == 44100;
        } else if (p[1] == 5u) {
            if (p[2] == SPK_EP)
                ep04 = cur_if == 3 && cur_alt == 1 && p[0] == 9u && p[3] == 0x09u && (p[4] | p[5] << 8) == SPK_MAXP &&
                       p[6] == 1u;
            if (p[2] == 0x84u)
                ep84 = cur_if == 2 && cur_alt == 1;
        } else if (p[1] == 0x25u && cur_if == 3) {
            csep = p[0] == 7u && p[3] == 0u;
        }
    }
    snprintf(name, sizeof name, "USB RATE %u: lengths add up to wTotalLength (%u)", (unsigned)mode, (unsigned)cl);
    check(name, lens && off == cl && (c[2] | c[3] << 8) == cl);
    check("  four interfaces", c[4] == 4u && nif_alt0 == 4u);
    check("  AC header: collection 1 2 3, wTotalLength = its descriptors", coll && ac_wt == ac_sum);
    check("  input terminal 3: USB streaming, 2 ch", it3);
    check("  output terminal 4: speaker, from 3", ot4);
    check("  IF3 alt 0 (no endpoint), alt 1 (one)", if3a0 && if3a1);
    check("  IF3 alt 1: type I, 2 ch, 16 bit, 44.1 kHz", fmt);
    check("  EP 0x03 OUT: isochronous adaptive, 192 B, 1 ms; CS EP no controls", ep04 && csep);
    check("  EP 0x84 IN still on IF2 alt 1", ep84);
    return 0;
}

int main(void)
{
    uint32_t m;
    for (m = 0; m < 3u; m++)
        walk(m);
    printf(fails ? "SPK DESC TEST FAILED (%d)\n" : "spk desc: all ok\n", fails);
    return fails != 0;
}
