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


static volatile uint8_t scale_mode = 1, latch;
static volatile uint8_t tag_drawn;   /* the build tag was drawn on GLO page 4 (a host test checks it: do not take the build off that page) */   /* the UI's modes (the render reads neither) */

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
        memset(am_store.name, 0, sizeof am_store.name);
    }
    if (am_store.cur >= NLIST)
        am_store.cur = 0;
    am_seed ^= plat_ms() * 2654435761u + 1u;
    scene_use(am_store.cur);
}

/* the stress test's scene: the saved factory scene with every track sounding, every instrument, the sends and the effects up */
void stress_set(uint32_t on)
{
    static uint8_t keep;                     /* the scene to go back to */
    uint32_t t;
    scene_t s;
    amb_stress = 0;
    app_panic();
    if (!on) {
        scene_use(keep);
        playing = 1;
        return;
    }
    keep = am_store.cur;
    scene_factory(&s, 0);                    /* the factory scene, not a saved slot: the same load every time */
    sc = s;
    sc_dirty = 0;
    for (t = 0; t < NTRK; t++) {
        sc.tp[t][TP_INST] = TRACK_ROLE[t];
        sc.tp[t][TP_LEVEL] = 55;
        sc.tp[t][TP_DSEND] = 70;
        sc.tp[t][TP_RSEND] = 70;
        sc.tp[t][TP_SUS] = 70;
        sc.tp[t][TP_REL] = 60;
        sc.tp[t][TP_FSUS] = 70;
        sc.tp[t][TP_FREL] = 60;
        sc.tp[t][TP_MUTE] = 0;
        sc.tp[t][TP_SOLO] = 0;
    }
    sc.gp[GP_DMIX] = 60;
    sc.gp[GP_DFB] = 50;
    sc.gp[GP_RSIZE] = 70;
    sc.gp[GP_RSHIM] = 60;
    sc.gp[GP_RMIX] = 70;
    sc.gp[GP_HISS] = 30;
    sc.gp[GP_CRACKLE] = 20;
    sc.gp[GP_WOW] = 30;
    sc.gp[GP_AGE] = 30;
    sc.gp[GP_SLEEP] = 0;
    playing = 0;                             /* the sequencers stay quiet: the render fires the notes */
    amb_stress = 1;
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
    {   /* the bell's METAL page moves: redraw about 14 times a second */
        static uint32_t last_anim;
        if (pic_moves() && now - last_anim >= 100u) {
            last_anim = now;
            if (!ui.redraw)
                ui.mid_only = 1;
            ui.redraw = 1;
        }
    }
    if (ui.redraw) {
        ui.redraw = 0;
        draw();
        ui.mid_only = 0;
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
    else if (!strcmp(what, "delay_ms"))
        *got = (int)(fx_delay_seconds() * 1000.0f + 0.5f);
    else if (!strcmp(what, "dsync"))
        *got = sc.gp[GP_DSYNC];
    else if (!strcmp(what, "page"))
        *got = ui.page * 10 + ui.sub[ui.page];
    else if (!strcmp(what, "inst"))
        *got = sc.tp[cur_trk][TP_INST];
    else if (!strcmp(what, "scale_mode"))
        *got = scale_mode;
    else if (!strcmp(what, "mute"))
        *got = sc.tp[cur_trk][TP_MUTE];
    else if (!strcmp(what, "proj"))
        *got = ui.proj;
    else if (!strcmp(what, "saved"))
        *got = am_store.saved;
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
    else if (!strcmp(what, "adv"))
        *got = ui.adv;
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
    else if (!strcmp(what, "fatk") || !strcmp(what, "fdec") || !strcmp(what, "fsus") || !strcmp(what, "frel"))   /* the filter ADSR */
        *got = sc.tp[cur_trk][TP_FATK + (what[1] == 'a' ? 0 : what[1] == 'd' ? 1 : what[1] == 's' ? 2 : 3)];
    else if (!strcmp(what, "buildtag"))
        *got = tag_drawn;
    else if (!strcmp(what, "stress"))
        *got = amb_stress;
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
