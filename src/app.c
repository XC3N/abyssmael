/* SPDX-License-Identifier: MPL-2.0 */
/* ABYSSMAEL 0.4: six tracks of generative ambient on fm1-base.
 *
 *   SELECT  the track (1-6)        ALGO  its instrument        PRESET  the scene (1-4)
 *   HOME EDIT ENV LFO SEQ ARP FX GLO   a page of four values for K1-K4; press again for the next page
 *   PLAY start / stop the generators   REC latch   SEL scale mode   SAVE the scene   OCT- OCT+ the keys' octave
 *   Scale mode (on at power-on): the 16 white keys play the scale; the black keys are the screen's submenus, shown
 *   along the bottom of the screen: the first three the button's pages, the next two the track, the next five
 *   this screen's choices; the last one MIX, the mute and solo page (tap: open it; hold: only while held).
 *
 * This file is the unity build's entry and the platform contract (app_init, app_frame, app_expect); the rest of the
 * UI side is scene.c, ui.c and display.c, included below. The sound side is core.c, seq.c, engines.c and fx.c, each its
 * own -O2 unit (fm1.toml o2). See MODULES.md.
 *
 * Part of the unity build (device/unity.c, host/plat_host.c), after the platform: gfx.c and announce.c are in scope. */
#include "abyssmael.h"


static volatile uint8_t scale_mode = 1, latch;   /* the UI's modes (the render reads neither) */

#include "scene.c"
#include "ui.c"
#include "display.c"

void app_init(void)
{
    amb_two_cores = plat_cpu2_set(1) == 0;      /* the second core renders half the voices; else one core does all */
    if (plat_store_load(OBJ_SETTINGS, &am_store, sizeof am_store) != (int)sizeof am_store || am_store.magic != STORE_MAGIC) {
        am_store.magic = STORE_MAGIC;
        am_store.cur = 0;
        am_store.saved = 0;
    }
    am_seed ^= plat_ms() * 2654435761u + 1u;
    scene_use(am_store.cur);
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.page = PG_HOME;
    ui.hold_step = -1;
    ui.oct = 3;
    ui.redraw = 1;
}

void app_frame(void)
{
    static uint32_t last_n, last_t, last_key;
    uint32_t now = plat_ms();
    static uint8_t shown_asleep, woke;
    if (amb_asleep) {                        /* the sleep timer ran out: the engine is stopped; screen and lights off */
        if (!shown_asleep) {
            shown_asleep = 1;
            app_panic();
            playing = 0;
            lcd_fill(0, 0, 240, 240, C_BLACK);
            plat_lights(0, 0, 0, 0, 0);
            plat_lights_mid(0, 0);
        }
        if (plat_keys() | plat_buttons()) {  /* any key or button wakes it; the press itself does nothing */
            amb_asleep = 0;
            shown_asleep = 0;
            woke = 1;
            sc.gp[GP_SLEEP] = 0;
            ui.redraw = 1;
        }
        return;
    }
    if (woke) {                              /* wait for the waking press to end */
        if (plat_keys() | plat_buttons())
            return;
        woke = 0;
        ui.keys = ui.btn = 0;
    }
    input();
    ann_tick(now);
    if (notes_played != last_n || now - last_t > 500u || sc.gp[GP_KEY] != last_key ||
        (ui.msg_until && now >= ui.msg_until)) {
        last_n = notes_played;
        last_t = now;
        last_key = sc.gp[GP_KEY];
        if (now >= ui.msg_until)
            ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (ui.redraw) {
        ui.redraw = 0;
        draw();
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    uint32_t k, n = 0;
    if (!strcmp(what, "playing"))
        *got = playing;
    else if (!strcmp(what, "notes"))
        *got = (int)notes_played;
    else if (!strcmp(what, "track"))
        *got = cur_trk;
    else if (!strcmp(what, "page"))
        *got = ui.page * 10 + ui.sub[ui.page];
    else if (!strcmp(what, "inst"))
        *got = sc.tp[cur_trk][TP_INST];
    else if (!strcmp(what, "scale_mode"))
        *got = scale_mode;
    else if (!strcmp(what, "mute"))
        *got = sc.tp[cur_trk][TP_MUTE];
    else if (!strcmp(what, "dirty"))
        *got = sc_dirty;
    else if (!strcmp(what, "mutes") || !strcmp(what, "solos")) {
        for (k = 0; k < NTRK; k++)
            n |= (uint32_t)sc.tp[k][what[0] == 'm' ? TP_MUTE : TP_SOLO] << k;
        *got = (int)n;
    } else if (!strcmp(what, "mix_mode"))
        *got = ui.mix_mode;
    else if (!strcmp(what, "smode"))
        *got = sc.tp[cur_trk][TP_SMODE];
    else if (!strcmp(what, "note_edit"))
        *got = ui.note_edit;
    else if (!strcmp(what, "cursor"))
        *got = ui.cursor;
    else if (!strcmp(what, "hn0") || !strcmp(what, "hn1") || !strcmp(what, "sn0") || !strcmp(what, "sn1"))
        *got = (what[0] == 'h' ? sc.hn : sc.sn)[cur_trk][what[2] - '0'];
    else if (!strcmp(what, "son"))
        *got = sc.son[cur_trk];
    else if (!strcmp(what, "dejavu"))
        *got = sc.tp[cur_trk][TP_DEJAVU];
    else if (!strcmp(what, "arp"))
        *got = sc.tp[cur_trk][TP_ARPON] ? sc.tp[cur_trk][TP_ARPPAT] + 1 : 0;
    else if (!strcmp(what, "cd0") || !strcmp(what, "cd1"))   /* the condition of hit or step 0 / 1 */
        *got = sc.cd[cur_trk][what[2] - '0'];
    else if (!strcmp(what, "edrift"))
        *got = sc.tp[cur_trk][TP_EDRIFT];
    else if (!strcmp(what, "edir"))
        *got = sc.tp[cur_trk][TP_EDIR];
    else if (!strcmp(what, "arpsrc"))
        *got = sc.tp[cur_trk][TP_ARPSRC];
    else if (!strcmp(what, "asleep"))
        *got = amb_asleep;
    else if (!strcmp(what, "sleep_min"))
        *got = (int)sleep_minutes(sc.gp[GP_SLEEP]);
    else if (!strcmp(what, "sleep_left"))
        *got = (int)sleep_left_s();
    else if (!strcmp(what, "scene"))
        *got = am_store.cur;
    else if (!strcmp(what, "voices")) {
        for (k = 0; k < NVOICE; k++)
            n += vc[k].on;
        *got = (int)n;
    } else
        return 0;
    return 1;
}
#endif
