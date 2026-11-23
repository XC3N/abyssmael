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

static void render_block(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    seq_block(n);
    engines_block(n);
    engines_run(n);
    fx_block(lr, n, master_q12);
    engines_flush();
}

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

