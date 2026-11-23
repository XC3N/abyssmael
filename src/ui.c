/* SPDX-License-Identifier: MPL-2.0 */
/* UI: buttons, encoders and keys: the pages and their values, the black-key submenus, the MIX page, note entry, the
 * lights. It edits the scene (sc) and the UI's own state (ui). Part of app.c's unity build (the UI side). */
/* --------------------------------------------------------------------- UI --- */
static struct {
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;
    uint8_t page, sub[NPG];              /* the page shown, and each button's page */
    uint8_t latched[NKEYS];
    uint8_t prev_page, mix_mode;         /* the page MIX came from; MIX's black keys: 0 mute, 1 solo */
    uint32_t mix_at;                     /* when the MIX key went down (held long: MIX only while held) */
    uint8_t note_edit, cursor;           /* SEQ 3: the white keys write notes, into this hit / step */
    int8_t hold_step;                    /* SEQ 3, STEP: the white key held (K1 sets its note), -1 none */
    uint8_t hold_turned;
    int32_t knote[NKEYS];                /* the note each key started (so a key's note off finds it) */
    uint8_t redraw;
} ui;

static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}
static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
    if (ann_on()) {
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

/* a page's value k: SEQ 2 and 3 show the track's mode's values */
static uint32_t page_ref(uint32_t pg, uint32_t sub, uint32_t k)
{
    if (pg == PG_SEQ && sub == 2u && sc.tp[cur_trk][TP_SMODE] == 1u) {   /* EUCL pattern: what shapes it as you watch */
        static const uint8_t PAT_REF[4] = {TP_EHITS, TP_EROT, TP_EDIR, TP_ECHANCE};
        return PAT_REF[k];
    }
    if (pg == PG_SEQ && sub > 0u)
        return MODE_REF[sc.tp[cur_trk][TP_SMODE]][k];
    return PG_REF[pg][sub][k];
}
static const char *page_title(uint32_t pg, uint32_t sub)
{
    static const char *const PT[3] = {"GENERATOR MOTIF", "EUCLID HITS", "STEP PATTERN"};
    if (pg == PG_SEQ && sub == 1u)
        return SEQ_MODE[sc.tp[cur_trk][TP_SMODE]];
    if (pg == PG_SEQ && sub == 2u)
        return ui.note_edit ? "NOTE ENTRY" : PT[sc.tp[cur_trk][TP_SMODE]];
    return PG_TITLE[pg][sub];
}
static const char *page_short(uint32_t pg, uint32_t sub)
{
    if (pg == PG_SEQ && sub == 1u)
        return SEQ_MODE_S[sc.tp[cur_trk][TP_SMODE]];
    return PG_SHORT[pg][sub];
}

/* a value's name and text */
static const pdesc_t *ref_desc(uint32_t ref) { return ref & 0x80u ? &GPD[ref & 0x7Fu] : &TPD[ref]; }
static uint8_t *ref_ptr(uint32_t ref) { return ref & 0x80u ? &sc.gp[ref & 0x7Fu] : &sc.tp[cur_trk][ref]; }
static const char *ref_name(uint32_t ref)
{
    const pdesc_t *d = ref_desc(ref);
    if (d->kind == K_INSTP)
        return INST_P[sc.tp[cur_trk][TP_INST]][ref - TP_P0];
    return d->name;
}
static void ref_text(uint32_t ref, char *t)
{
    const pdesc_t *d = ref_desc(ref);
    uint32_t v = *ref_ptr(ref);
    t[0] = 0;
    switch (d->kind) {
    case K_ENUM: text_cat(t, d->names[v]); break;
    case K_OCT: int_text((int32_t)v - 3, t); break;
    case K_NOTE: text_cat(t, NOTE_NAME[v % 12u]); break;
    case K_BPM: int_text((int32_t)v, t); text_cat(t, " BPM"); break;
    case K_CENT: int_text((int32_t)v - 50, t); text_cat(t, " CT"); break;
    default: int_text((int32_t)v, t); break;
    }
}
static uint32_t ref_frac(uint32_t ref)   /* 0..200: the bar */
{
    const pdesc_t *d = ref_desc(ref);
    return d->hi > d->lo ? (uint32_t)(*ref_ptr(ref) - d->lo) * 200u / (uint32_t)(d->hi - d->lo) : 0u;
}

static void ref_turn(uint32_t ref, int32_t e)
{
    const pdesc_t *d = ref_desc(ref);
    uint8_t *p = ref_ptr(ref);
    int32_t v = clamp((int32_t)*p + e * (int32_t)d->step, d->lo, d->hi);
    char t[16];
    if (v == *p)
        return;
    *p = (uint8_t)v;
    sc_dirty = 1;
    if (ref == G(GP_THEME))
        palette_set(sc.gp[GP_THEME]);
    ref_text(ref, t);
    app_say(ref_name(ref), t);
}

static void page_open(uint32_t pg, int sub)
{
    char t[24];
    if (sub < 0)                             /* the same button again: its next page */
        sub = ui.page == pg ? (ui.sub[pg] + 1) % PG_N[pg] : ui.sub[pg];
    if (sub >= PG_N[pg])
        return;
    ui.page = (uint8_t)pg;
    ui.sub[pg] = (uint8_t)sub;
    t[0] = 0;
    text_cat(t, PG_NAME[pg]);
    if (PG_N[pg] > 1u) {
        char n[4] = {' ', (char)('1' + sub), 0, 0};
        text_cat(t, n);
    }
    app_say(t, page_title(pg, (uint32_t)sub));
}

static void track_pick(uint32_t t)
{
    char s[24] = "TRACK ";
    char n[2] = {(char)('1' + t), 0};
    cur_trk = (uint8_t)t;
    text_cat(s, n);
    app_say(s, INST_NAME[sc.tp[t][TP_INST]]);
}

static int32_t scale_key_note(uint32_t wi)
{
    int32_t oct = 3 + 0;
    (void)oct;
    return degree_note((int32_t)wi, INST_OCT[sc.tp[cur_trk][TP_INST]] + (int32_t)sc.tp[cur_trk][TP_OCT] - 3);
}

/* ---------------------------------------------------- the black keys (scale mode) ---
 * 11 black keys, in the FM-1's groups 3 + 2 + 3 + 2 + 1. On every page but HOME and MIX: the first three are the
 * button's pages, the next two the track down and up, the next five this page's choices, the last MIX. HOME: the
 * tracks on the two groups of three, scenes down/up, PLAY, LATCH. MIX: the tracks on the groups of three, MUTE and
 * SOLO (what a track key does), clear the mutes, clear the solos. */
enum { S_NONE, S_SUB, S_TRK, S_TPREV, S_TNEXT, S_INST, S_ENV, S_LFO, S_SEQ, S_ARP, S_SEND, S_SCENE, S_SAVE,
       S_PLAY, S_LATCH, S_SPREV, S_SNEXT, S_MMODE, S_MTRK, S_MCLR, S_SCLR, S_MIX,
       S_SMODE, S_NOTES, S_RAND, S_REST, S_CURP, S_CURN };
static const int8_t BK_TRK[11] = {0, 1, 2, -1, -1, 3, 4, 5, -1, -1, -1};
static const char *const TRK_LBL[NTRK] = {"T1", "T2", "T3", "T4", "T5", "T6"};
static const char *const INST_SHORT[NINST] = {"DRONE", "PAD", "BELL", "PLUCK", "TEXT"};
static const char *const INST_3[NINST] = {"DRN", "PAD", "BEL", "PLK", "TEX"};
static const char *const ENV_LBL[5] = {"PERC", "PLUCK", "SWELL", "PAD", "HOLD"};
static const uint8_t ENV_SET[5][4] = {{0, 40, 0, 45}, {0, 62, 0, 62}, {82, 60, 90, 86}, {62, 60, 80, 76}, {12, 60, 100, 52}};
static const char *const LFO_LBL[5] = {"SINE", "TRI", "SAW", "RAND", "DRIFT"};
static const uint8_t LFO_SET[5] = {0, 1, 2, 4, 5};
static const char *const SEQ_LBL[2] = {"NEW", "LOCK"};
static const char *const ARP_LBL[5] = {"OFF", "UP", "DOWN", "UPDN", "RAND"};
static const char *const SEND_LBL[5] = {"DRY", "DLY", "VERB", "BOTH", "WASH"};
static const uint8_t SEND_SET[5][2] = {{0, 0}, {60, 10}, {10, 70}, {50, 60}, {80, 100}};
static const char *const SCN_LBL[5] = {"DEEP", "GLASS", "TIDE", "VOID", "SAVE"};

/* what black key bi does on the page shown: its action (S_*), its argument, its label, lit (1) or not */
static uint32_t slot(uint32_t bi, uint32_t *arg, const char **lbl, uint32_t *lit)
{
    uint32_t pg = ui.page, sub = ui.sub[pg], c = bi - 5u;
    const uint8_t *p = sc.tp[cur_trk];
    *arg = 0;
    *lbl = "";
    *lit = 0;
    if (bi == 10u) {
        *lbl = "MIX";
        *lit = pg == PG_MIX;
        return S_MIX;
    }
    if (pg == PG_HOME || pg == PG_MIX) {
        static const char *const HL[4] = {"SCN-", "SCN+", "PLAY", "LATCH"};
        static const char *const ML[4] = {"MUTE", "SOLO", "CLR M", "CLR S"};
        static const uint8_t HA[4] = {S_SPREV, S_SNEXT, S_PLAY, S_LATCH}, MA[4] = {S_MMODE, S_MMODE, S_MCLR, S_SCLR};
        uint32_t j = bi == 3u ? 0u : bi == 4u ? 1u : bi == 8u ? 2u : 3u;
        if (BK_TRK[bi] >= 0) {
            uint32_t t = (uint32_t)BK_TRK[bi];
            *arg = t;
            *lbl = TRK_LBL[t];
            if (pg == PG_HOME) {
                *lit = t == cur_trk;
                return S_TRK;
            }
            *lit = ui.mix_mode ? sc.tp[t][TP_SOLO] : sc.tp[t][TP_MUTE];
            return S_MTRK;
        }
        if (pg == PG_HOME) {
            *lbl = HL[j];
            *lit = j == 2u ? playing : j == 3u ? latch : 0u;
            return HA[j];
        }
        *lbl = ML[j];
        *arg = j;
        *lit = j < 2u && ui.mix_mode == j;
        return MA[j];
    }
    if (bi < 3u) {
        if (bi >= PG_N[pg])
            return S_NONE;
        *arg = bi;
        *lbl = page_short(pg, bi);
        *lit = bi == sub;
        return S_SUB;
    }
    if (bi < 5u && pg == PG_SEQ && ui.note_edit) {   /* entering notes: the cursor instead of the track */
        *lbl = bi == 3u ? "<CUR" : "CUR>";
        return bi == 3u ? S_CURP : S_CURN;
    }
    if (bi < 5u) {
        *lbl = bi == 3u ? "TRK-" : "TRK+";
        return bi == 3u ? S_TPREV : S_TNEXT;
    }
    *arg = c;
    switch (pg) {
    case PG_EDIT: *lbl = INST_SHORT[c]; *lit = p[TP_INST] == c; return S_INST;
    case PG_ENV:
        *lbl = ENV_LBL[c];
        *lit = p[TP_ATK] == ENV_SET[c][0] && p[TP_DEC] == ENV_SET[c][1] && p[TP_SUS] == ENV_SET[c][2] &&
               p[TP_REL] == ENV_SET[c][3];
        return S_ENV;
    case PG_LFO: *lbl = LFO_LBL[c]; *lit = p[TP_L1SHP + 4 * sub] == LFO_SET[c]; return S_LFO;
    case PG_SEQ:                             /* the mode; then NEW, LOCK (GEN) or NOTES, RAND / REST */
        if (c < 3u) {
            *lbl = SEQ_MODE_S[c];
            *lit = p[TP_SMODE] == c;
            return S_SMODE;
        }
        if (p[TP_SMODE] == 0u) {
            *arg = c - 3u;
            *lbl = SEQ_LBL[c - 3u];
            *lit = c == 4u && p[TP_DEJAVU] == 100;
            return S_SEQ;
        }
        if (c == 3u) {
            *lbl = "NOTES";
            *lit = ui.note_edit;
            return S_NOTES;
        }
        *lbl = ui.note_edit ? "REST" : "RAND";
        return ui.note_edit ? S_REST : S_RAND;
    case PG_ARP: *lbl = ARP_LBL[c]; *lit = c == 0u ? !p[TP_ARPON] : p[TP_ARPON] && p[TP_ARPPAT] == c - 1u; return S_ARP;
    case PG_FX:
        *lbl = SEND_LBL[c];
        *lit = p[TP_DSEND] == SEND_SET[c][0] && p[TP_RSEND] == SEND_SET[c][1];
        return S_SEND;
    case PG_GLO: *lbl = SCN_LBL[c]; *lit = c < 4u && am_store.cur == c; return c < 4u ? S_SCENE : S_SAVE;
    default: return S_NONE;
    }
}

static void track_pick(uint32_t t);
static void scene_save(void)
{
    am_store.slot[am_store.cur] = sc;
    am_store.saved |= (uint8_t)(1u << am_store.cur);
    if (plat_store_save(OBJ_SETTINGS, &am_store, sizeof am_store) == 0) {
        sc_dirty = 0;
        app_say("SAVED", SCENE_NAME[am_store.cur]);
    } else {
        app_say("SAVE", "FAILED");
    }
}
static void play_toggle(void)
{
    uint32_t i;
    playing = !playing;
    if (!playing)
        for (i = 0; i < NVOICE; i++)
            if (vc[i].gate >= 0 && vc[i].on)
                vc[i].stage = 4;
    app_say(playing ? "PLAYING" : "STOPPED", "");
}
static void latch_toggle(void)
{
    uint32_t i;
    latch = !latch;
    if (!latch)
        for (i = 0; i < NKEYS; i++)
            if (ui.latched[i]) {
                ui.latched[i] = 0;
                key_event(cur_trk, ui.knote[i], 0, 0);
            }
    app_say("LATCH", latch ? "ON" : "OFF");
}
static void scene_step(int32_t d)
{
    int32_t s = clamp((int32_t)am_store.cur + d, 0, 3);
    if (s != am_store.cur) {
        scene_use((uint32_t)s);
        app_say("SCENE", SCENE_NAME[s]);
    }
}
static void mix_open(void)
{
    if (ui.page != PG_MIX)
        ui.prev_page = ui.page;
    ui.page = PG_MIX;
    app_say("MIX", ui.mix_mode ? "SOLO" : "MUTE");
}
static void mix_close(void)
{
    ui.page = ui.prev_page;
    ui.redraw = 1;
}

/* SEQ 3: how many hits (EUCLID) or steps (STEP) the cursor moves over */
static uint32_t seq_count(uint32_t t)
{
    const uint8_t *p = sc.tp[t];
    uint32_t h = p[TP_EHITS] < p[TP_ELEN] ? p[TP_EHITS] : p[TP_ELEN];
    return p[TP_SMODE] == 1u ? (h ? h : 1u) : p[TP_SLEN];
}

/* SEQ 3 entering notes: white key wi writes its degree at the cursor, which moves on */
static void note_enter(uint32_t wi)
{
    if (sc.tp[cur_trk][TP_SMODE] == 1u) {
        sc.hn[cur_trk][ui.cursor] = (uint8_t)wi;
    } else {
        sc.sn[cur_trk][ui.cursor] = (uint8_t)wi;
        sc.son[cur_trk] |= (uint16_t)(1u << ui.cursor);
    }
    sc_dirty = 1;
    ui.cursor = (uint8_t)((ui.cursor + 1u) % seq_count(cur_trk));
}

/* a black key pressed (down) or let go (up) in scale mode */
static void black_key(uint32_t bi, int down)
{
    uint32_t arg, lit, t, i;
    const char *lbl;
    uint32_t a = slot(bi, &arg, &lbl, &lit);
    uint8_t *p = sc.tp[cur_trk];
    char n[4] = {(char)('1' + cur_trk), 0, 0, 0};
    if (a == S_MIX) {                        /* a tap opens or closes MIX; held past 400 ms, MIX only while held */
        if (down) {
            ui.mix_at = plat_ms();
            if (ui.page == PG_MIX)
                ui.mix_at = 0, mix_close();
            else
                mix_open();
        } else if (ui.mix_at && plat_ms() - ui.mix_at > 400u && ui.page == PG_MIX) {
            mix_close();
        }
        return;
    }
    if (!down)
        return;
    switch (a) {
    case S_SUB: page_open(ui.page, (int)arg); break;
    case S_TRK: track_pick(arg); break;
    case S_TPREV: track_pick(cur_trk ? cur_trk - 1u : NTRK - 1u); break;
    case S_TNEXT: track_pick(cur_trk + 1u < NTRK ? cur_trk + 1u : 0u); break;
    case S_INST:
        if (p[TP_INST] != arg) {
            track_inst(&sc, cur_trk, arg);
            sc_dirty = 1;
        }
        track_pick(cur_trk);
        break;
    case S_ENV:
        for (i = 0; i < 4u; i++)
            p[TP_ATK + i] = ENV_SET[arg][i];
        sc_dirty = 1;
        app_say("ENVELOPE", ENV_LBL[arg]);
        break;
    case S_LFO:
        p[TP_L1SHP + 4 * ui.sub[PG_LFO]] = LFO_SET[arg];
        sc_dirty = 1;
        app_say(ui.sub[PG_LFO] ? "LFO2 SHAPE" : "LFO1 SHAPE", LFO_SHAPE[LFO_SET[arg]]);
        break;
    case S_SEQ:
        if (arg == 0u) {
            mo[cur_trk].have = 0;            /* a new motif: written afresh on the next pass */
            app_say("MOTIF", "NEW");
        } else {
            p[TP_DEJAVU] = p[TP_DEJAVU] == 100 ? 60 : 100;
            sc_dirty = 1;
            app_say("MOTIF", p[TP_DEJAVU] == 100 ? "LOCKED" : "FREE");
        }
        break;
    case S_SMODE:
        if (p[TP_SMODE] != arg) {
            p[TP_SMODE] = (uint8_t)arg;
            seq_pos[cur_trk] = 0;
            sc_dirty = 1;
        }
        if (!arg)
            ui.note_edit = 0;
        app_say("SEQ MODE", SEQ_MODE[arg]);
        break;
    case S_NOTES:
        ui.note_edit = !ui.note_edit;
        ui.cursor = 0;
        if (ui.note_edit && !(ui.page == PG_SEQ && ui.sub[PG_SEQ] == 2u))
            page_open(PG_SEQ, 2);
        app_say("NOTE ENTRY", ui.note_edit ? "WHITE KEYS" : "OFF");
        break;
    case S_CURP:
    case S_CURN: {
        uint32_t cnt = seq_count(cur_trk);
        ui.cursor = (uint8_t)((ui.cursor + (a == S_CURN ? 1u : cnt - 1u)) % cnt);
        break;
    }
    case S_REST:
        if (p[TP_SMODE] == 1u)
            sc.hn[cur_trk][ui.cursor] = REST;
        else
            sc.son[cur_trk] &= (uint16_t)~(1u << ui.cursor);
        sc_dirty = 1;
        ui.cursor = (uint8_t)((ui.cursor + 1u) % seq_count(cur_trk));
        app_say("REST", "");
        break;
    case S_RAND:
        for (i = 0; i < 16u; i++) {          /* notes from the scale's lower octave and a bit, now and then a rest */
            uint32_t d = am_rnd() % 9u;
            if (p[TP_SMODE] == 1u)
                sc.hn[cur_trk][i] = (uint8_t)(i && am_rndf() < 0.15f ? REST : d);
            else
                sc.sn[cur_trk][i] = (uint8_t)d;
        }
        sc_dirty = 1;
        app_say("NOTES", "RANDOM");
        break;
    case S_ARP:
        p[TP_ARPON] = arg ? 1u : 0u;
        if (arg)
            p[TP_ARPPAT] = (uint8_t)(arg - 1u);
        sc_dirty = 1;
        app_say("ARP", arg ? ARP_PAT[arg - 1u] : "OFF");
        break;
    case S_SEND:
        p[TP_DSEND] = SEND_SET[arg][0];
        p[TP_RSEND] = SEND_SET[arg][1];
        sc_dirty = 1;
        app_say("SENDS", SEND_LBL[arg]);
        break;
    case S_SCENE:
        if (arg != am_store.cur) {
            scene_use(arg);
            app_say("SCENE", SCENE_NAME[arg]);
        }
        break;
    case S_SAVE: scene_save(); break;
    case S_PLAY: play_toggle(); break;
    case S_LATCH: latch_toggle(); break;
    case S_SPREV: scene_step(-1); break;
    case S_SNEXT: scene_step(1); break;
    case S_MMODE: ui.mix_mode = (uint8_t)arg; app_say("TRACK KEYS", arg ? "SOLO" : "MUTE"); break;
    case S_MTRK:
        t = ui.mix_mode ? TP_SOLO : TP_MUTE;
        sc.tp[arg][t] ^= 1u;
        sc_dirty = 1;
        n[0] = (char)('1' + arg);
        app_say(TRK_LBL[arg], ui.mix_mode ? (sc.tp[arg][t] ? "SOLO" : "SOLO OFF") : (sc.tp[arg][t] ? "MUTED" : "UNMUTED"));
        break;
    case S_MCLR:
    case S_SCLR:
        for (i = 0; i < NTRK; i++)
            sc.tp[i][a == S_MCLR ? TP_MUTE : TP_SOLO] = 0;
        sc_dirty = 1;
        app_say(a == S_MCLR ? "MUTES" : "SOLOS", "CLEARED");
        break;
    default: break;
    }
    (void)n;
    ui.redraw = 1;
}

static void input(void)
{
    static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
    static int8_t oct_shift;
    uint32_t keys = plat_keys(), btn = plat_buttons(), k, pressed = keys & ~ui.keys, released = ui.keys & ~keys;
    int32_t e;
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k, wi = 99, bi = 99, j;
        if (!((pressed | released) & bit))
            continue;
        for (j = 0; j < 16u; j++)
            if (WK[j] == k)
                wi = j;
        for (j = 0; j < 11u; j++)
            if (BK[j] == k)
                bi = j;
        if (scale_mode && bi < 11u) {        /* the black keys: the screen's submenus */
            black_key(bi, (pressed & bit) != 0u);
            continue;
        }
        if (scale_mode && wi < 16u && ui.page == PG_SEQ && ui.sub[PG_SEQ] == 2u && sc.tp[cur_trk][TP_SMODE]) {
            if (ui.note_edit) {              /* entering notes: write it, then play it as usual */
                if (pressed & bit)
                    note_enter(wi);
            } else {                         /* STEP: the white keys are the 16 steps; EUCL: its hits */
                if (sc.tp[cur_trk][TP_SMODE] == 1u && wi >= seq_count(cur_trk))
                    continue;                /* beyond the last hit: nothing there */
                if (pressed & bit) {
                    ui.hold_step = (int8_t)wi;
                    ui.hold_turned = 0;
                } else if (ui.hold_step == (int8_t)wi) {
                    if (!ui.hold_turned) {   /* a tap turns a step on or off, or a hit into a rest and back */
                        if (sc.tp[cur_trk][TP_SMODE] == 2u)
                            sc.son[cur_trk] ^= (uint16_t)(1u << wi);
                        else
                            sc.hn[cur_trk][wi] = sc.hn[cur_trk][wi] == REST ? 0u : REST;
                    }
                    sc_dirty = 1;
                    ui.hold_step = -1;
                }
                ui.redraw = 1;
                continue;
            }
        }
        if (pressed & bit) {
            int32_t note = scale_mode ? scale_key_note(wi) + 12 * oct_shift : 53 + (int32_t)k + 12 * oct_shift;
            if (latch && ui.latched[k]) {    /* latched: a second press lets it go */
                ui.latched[k] = 0;
                key_event(cur_trk, ui.knote[k], 0, 0);
                continue;
            }
            ui.knote[k] = note;
            key_event(cur_trk, note, 100, 1);
            plat_midi_send(0x09u | 0x90u << 8 | (uint32_t)note << 16 | 100u << 24, TO_USB);
            if (latch)
                ui.latched[k] = 1;
        } else if (!(latch && ui.latched[k])) {
            key_event(cur_trk, ui.knote[k], 0, 0);
            plat_midi_send(0x08u | 0x80u << 8 | (uint32_t)ui.knote[k] << 16, TO_USB);
        }
    }
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k, pg;
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        for (pg = 0; pg < NPG; pg++)
            if (PG_BTN[pg] == k)
                break;
        if (pg < NPG) {
            page_open(pg, -1);
            continue;
        }
        switch (k) {
        case B_PLAY:
            play_toggle();
            break;
        case B_SEL:
            scale_mode = !scale_mode;
            app_say("SCALE MODE", scale_mode ? "ON" : "OFF");
            break;
        case B_REC:
            latch_toggle();
            break;
        case B_OCTDN:
        case B_OCTUP: {
            char t[4];
            oct_shift = (int8_t)clamp(oct_shift + (k == B_OCTUP ? 1 : -1), -3, 3);
            int_text(oct_shift, t);
            app_say("KEYS OCTAVE", t);
            break;
        }
        case B_SAVE:
            scene_save();
            break;
        default:
            break;
        }
    }
    if ((e = plat_enc(EN_SELECT)) != 0)
        track_pick((uint32_t)clamp((int32_t)cur_trk + e, 0, NTRK - 1));
    if ((e = plat_enc(EN_ALGO)) != 0) {
        int32_t in = clamp((int32_t)sc.tp[cur_trk][TP_INST] + e, 0, NINST - 1);
        if (in != sc.tp[cur_trk][TP_INST]) {
            track_inst(&sc, cur_trk, (uint32_t)in);
            sc_dirty = 1;
            track_pick(cur_trk);
        }
    }
    if ((e = plat_enc(EN_PRESET)) != 0)
        scene_step(e);
    for (k = 0; k < 4u; k++)
        if ((e = plat_enc((int)(EN_K1 + k))) != 0) {
            if (k <= 1u && ui.hold_step >= 0) {   /* a step or hit held: K1 is its note, K2 its condition */
                uint32_t h = (uint32_t)ui.hold_step;
                uint32_t eu = sc.tp[cur_trk][TP_SMODE] == 1u;
                ui.hold_turned = 1;
                sc_dirty = 1;
                if (k == 1u) {
                    int32_t c = clamp((int32_t)sc.cd[cur_trk][h] + e, 0, NCOND - 1);
                    sc.cd[cur_trk][h] = (uint8_t)c;
                    app_say("CONDITION", COND_NAME[c]);
                    continue;
                }
                {
                    uint8_t *d = eu ? &sc.hn[cur_trk][h] : &sc.sn[cur_trk][h];
                    char nm[4] = {0, 0, 0, 0};
                    *d = (uint8_t)clamp((int32_t)(*d == REST ? 0 : *d) + e, 0, 15);
                    if (!eu)
                        sc.son[cur_trk] |= (uint16_t)(1u << h);
                    text_cat(nm, NOTE_NAME[degree_note(*d, 0) % 12]);
                    app_say(eu ? "HIT NOTE" : "STEP NOTE", nm);
                }
                continue;
            }
            ref_turn(page_ref(ui.page, ui.sub[ui.page], k), e);
        }
    if (!(ui.page == PG_SEQ && ui.sub[PG_SEQ] == 2u && sc.tp[cur_trk][TP_SMODE])) {
        ui.note_edit = 0;
        ui.hold_step = -1;
    }
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
    {   /* the lights: the page's button, PLAY, SEL, REC, SAVE; in scale mode the black keys show the track, page,
         * mute and solo, and the white keys glow */
        uint32_t lb = btn | (PG_BTN[ui.page] < NB ? 1u << PG_BTN[ui.page] : 0u), lk = keys, dk = 0, latched = 0;
        if (playing)
            lb |= 1u << B_PLAY;
        if (scale_mode)
            lb |= 1u << B_SEL;
        if (latch)
            lb |= 1u << B_REC;
        if (sc_dirty)
            lb |= 1u << B_SAVE;
        for (k = 0; k < NKEYS; k++)
            latched |= (uint32_t)ui.latched[k] << k;
        lk |= latched;
        if (scale_mode) {
            uint32_t len = SCALE_LEN[sc.gp[GP_SCALE]];
            for (k = 0; k < 11u; k++) {          /* the black keys: lit when on, glowing when they do something */
                uint32_t arg, lit;
                const char *lbl;
                if (slot(k, &arg, &lbl, &lit) == S_NONE)
                    continue;
                if (lit)
                    lk |= 1u << BK[k];
                else
                    dk |= 1u << BK[k];
            }
            if (ui.page == PG_SEQ && ui.sub[PG_SEQ] == 2u && sc.tp[cur_trk][TP_SMODE] == 2u && !ui.note_edit) {
                uint32_t on = sc.son[cur_trk];   /* the steps: lit when on, the step playing blinks the other way */
                for (k = 0; k < sc.tp[cur_trk][TP_SLEN]; k++)
                    if (((on >> k) & 1u) != (k == seq_cur[cur_trk] && playing))
                        lk |= 1u << WK[k];
                    else
                        dk |= 1u << WK[k];
            } else {
                for (k = 0; k < 16u; k++)
                    if (k % len == 0u)
                        dk |= 1u << WK[k];      /* the roots glow */
            }
        }
        plat_lights(lb, lk, 0x3FFFu & ~lb, dk, 1);
    }
}

void app_input_only(void) { input(); }

