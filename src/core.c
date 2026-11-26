/* SPDX-License-Identifier: MPL-2.0 */
/* CORE: the scene being played, the shared state, and the audio render: a block is the keys and the clock (seq), the
 * voices (engines), then the effects (fx). The UI (app.c) edits the scene; the render reads it. */
#define AMB_ENGINE 1
#include "plat.h"
#include "app.h"
#include "abyssmael.h"

scene_t sc;                              /* the scene playing: the main loop edits it, the render reads it */
volatile uint8_t playing = 1, cur_trk;
volatile uint32_t notes_played, drift_count;
volatile uint8_t trk_active[NTRK];       /* voices sounding per track (the screen) */

/* each stage's share of the block's time, in percent: prof_pct smoothed, prof_max a peak that decays over a few seconds
 * (the STATS page shows both; five timer reads a block, about nothing) */
volatile uint8_t prof_pct[5], prof_max[5];   /* seq, engines_block, engines_run (both cores, wall), fx, and [4] the whole app_render call */
static float prof_acc[5], prof_pk[5];

static void prof_add(uint32_t i, float now)
{
    prof_acc[i] += (now - prof_acc[i]) * 0.02f;
    prof_pct[i] = (uint8_t)(prof_acc[i] > 255.0f ? 255.0f : prof_acc[i]);
    prof_pk[i] = now > prof_pk[i] ? now : prof_pk[i] * 0.998f;
    prof_max[i] = (uint8_t)(prof_pk[i] > 255.0f ? 255.0f : prof_pk[i]);
}

/* STRESS TEST (stress_set in app.c): every 2 s, 3 voices on each of the 6 tracks, with fixed notes and gates, so the voices
 * always sit at the cap with all the effects on (gates of 3.5 s overlap the next chord). The same load every time: CPU readings can be compared between builds. */
volatile uint8_t amb_stress;
static uint32_t st_cnt, st_n;

static void stress_tick(uint32_t n)
{
    static const uint8_t BASE[6] = {36, 48, 55, 60, 64, 67};   /* (indexed modulo 6: any NTRK up to 6 works) */
    uint32_t t, j;
    if (!amb_stress) {
        st_cnt = 0;
        st_n = 0;
        return;
    }
    if (st_cnt == 0u) {
        for (t = 0; t < NTRK; t++)
            for (j = 0; j < 3u; j++)
                voice_start(t, (int32_t)BASE[t % 6u] + (int32_t)((st_n + j) % 3u) * 5, 0.8f, (int32_t)(3.5f * SR));
        st_n++;
    }
    st_cnt += n;
    if (st_cnt >= 2u * (uint32_t)SR)
        st_cnt = 0;
}

static void render_block(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t t0 = plat_ticks24(), t1, t2, t3, t4, i;
    float span = (float)n * (24000000.0f / SR), w[4];
    stress_tick(n);
    seq_block(n);
    t1 = plat_ticks24();
    engines_block(n);
    t2 = plat_ticks24();
    engines_run(n);
    t3 = plat_ticks24();
    fx_block(lr, n, master_q12);
    t4 = plat_ticks24();
    engines_flush();
    w[0] = (float)(t1 - t0); w[1] = (float)(t2 - t1); w[2] = (float)(t3 - t2); w[3] = (float)(t4 - t3);
    for (i = 0; i < 4u; i++)
        prof_add(i, 100.0f * w[i] / span);
}

/* ------------------------------------------------------------ sleep timer --- */
volatile uint8_t amb_asleep;
static uint32_t sl_left;                     /* samples to go; 0 off */
static uint8_t sl_last = 0xFFu;              /* GP_SLEEP as last seen; the first call only adopts it (no timer at boot) */

uint32_t sleep_minutes(uint32_t v) { return v == 0u ? 0u : ((v > 100u ? 100u : v) * 24u + 99u) / 100u * 5u; }
uint32_t sleep_left_s(void) { return (sl_left + (uint32_t)SR - 1u) / (uint32_t)SR; }

/* count n samples; the fade for this block (1 .. 0, a squared ramp); 0 when asleep */
static float sleep_tick(uint32_t n)
{
    uint8_t v = sc.gp[GP_SLEEP];
    uint32_t fade = SLEEP_FADE_S * (uint32_t)SR;
    float g;
    if (v != sl_last) {
        sl_last = v;
        sl_left = sleep_minutes(v) * 60u * (uint32_t)SR;
    }
    if (amb_asleep)
        return 0.0f;
    if (sl_left == 0u)
        return 1.0f;
    sl_left = n >= sl_left ? 0u : sl_left - n;
    if (sl_left == 0u) {
        amb_asleep = 1;
        return 0.0f;
    }
    if (sl_left >= fade)
        return 1.0f;
    g = (float)sl_left / (float)fade;
    return g * g;
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    float g = sleep_tick(n);
    uint32_t t0 = plat_ticks24(), n0 = n;
    if (amb_asleep) {                        /* the engine is stopped: silence, no render work */
        uint32_t i;
        for (i = 0; i < 2u * n; i++)
            lr[i] = 0;
        return;
    }
    master_q12 = (uint32_t)((float)master_q12 * g);
    while (n) {
        uint32_t b = n < APP_BLOCK ? n : APP_BLOCK;
        render_block(lr, b, master_q12);
        lr += 2u * b;
        n -= b;
    }
    {
        float pct = 100.0f * (float)(plat_ticks24() - t0) / ((float)n0 * (24000000.0f / SR));
        prof_add(4, pct);
        engines_load((uint32_t)pct);
    }
}

void app_panic(void)
{
    engines_panic();
    seq_panic();
}

