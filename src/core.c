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

#ifdef AMB_PROFILE
/* build with -DAMB_PROFILE: each stage's share of the block's time, in percent, smoothed (the footer shows it) */
volatile uint8_t prof_pct[4];                /* seq, engines_block, engines_run (both cores, wall), fx */
static float prof_acc[4];

static void render_block(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t t0 = plat_ticks24(), t1, t2, t3, t4, i;
    float span = (float)n * (24000000.0f / SR), w[4];
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
    for (i = 0; i < 4u; i++) {
        prof_acc[i] += (100.0f * w[i] / span - prof_acc[i]) * 0.02f;
        prof_pct[i] = (uint8_t)(prof_acc[i] > 255.0f ? 255.0f : prof_acc[i]);
    }
}
#else
static void render_block(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    seq_block(n);
    engines_block(n);
    engines_run(n);
    fx_block(lr, n, master_q12);
    engines_flush();
}
#endif

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    while (n) {
        uint32_t b = n < APP_BLOCK ? n : APP_BLOCK;
        render_block(lr, b, master_q12);
        lr += 2u * b;
        n -= b;
    }
}

void app_panic(void)
{
    engines_panic();
    seq_panic();
}

