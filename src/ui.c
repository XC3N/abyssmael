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
    uint8_t wk1;                         /* the pattern page's white key down, plus one */
    uint8_t cursor, adv;                 /* SEQ 3: the hit / step the white keys write into; adv: move on after a write */
    uint32_t wk_at;                      /* when the held white key went down */
    int8_t hold_step;                    /* SEQ 3, STEP: the white key held (K1 sets its note), -1 none */
    uint8_t hold_turned;
    int32_t knote[NKEYS];                /* the note each key started (so a key's note off finds it) */
    uint8_t redraw;
    uint8_t cpu_max, cpu_avg;            /* BUILD AND CPU page: the highest load seen, and a slow average */
    uint8_t pk[5];                       /* ... and the highest of each stage (S, B, V, F, R): held until K1 is turned */
    uint32_t cpu_at;
    uint8_t oct;                         /* the keys' octave shift, 0..6 (3 = none) */
    uint8_t proj, npos;                  /* SAV: the project the list is on, and the letter of its name being set */
    uint32_t oct_at;                     /* when the octave last changed (the OCT keys light, then blink the shift) */
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

static void proj_name(uint32_t n, char *t)   /* its name, 8 characters at most */
{
    uint32_t i;
    t[0] = 0;
    if (n < NFACT) {
        text_cat(t, SCENE_NAME[n]);
    } else if (proj_saved(n) || (n < NLIST && am_store.name[n - NFACT][0])) {
        for (i = 0; i < NAMELEN; i++)
            t[i] = am_store.name[n - NFACT][i];
        t[NAMELEN] = 0;
        for (i = NAMELEN; i && t[i - 1u] == ' '; i--)
            t[i - 1u] = 0;
    } else {
        text_cat(t, n < NLIST ? "-----" : "NEW");
    }
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
    if (pg == PG_SEQ && sub == 2u && sc.tp[cur_trk][TP_SMODE] == 1u) {   /* the pattern page: K1 the cursor, then what shapes it */
        static const uint8_t PAT_REF[4] = {0x62, TP_EROT, TP_EDIR, TP_ECHANCE};
        return PAT_REF[k];
    }
    if (pg == PG_SEQ && sub == 2u && sc.tp[cur_trk][TP_SMODE] == 2u) {
        static const uint8_t PAT_REF[4] = {0x62, TP_SDIR, TP_SCHANCE, TP_SMUT};
        return PAT_REF[k];
    }
    if (pg == PG_SEQ && sub > 0u)
        return MODE_REF[sc.tp[cur_trk][TP_SMODE]][k];
    return PG_REF[pg][sub][k];
}
static const char *page_title(uint32_t pg, uint32_t sub)
{
    static const char *const PT[3] = {"GENERATOR MOTIF", "EUCLID HITS", "STEP PATTERN"};
    if (pg == PG_HOME && sub == 1u && sleep_left_s()) {   /* the timer running: the time left */
        static char b[24];
        uint32_t s = sleep_left_s();
        char n[8];
        b[0] = 0;
        text_cat(b, "SLEEP IN ");
        int_text((int32_t)(s / 60u), n);
        text_cat(b, n);
        text_cat(b, s % 60u < 10u ? ":0" : ":");
        int_text((int32_t)(s % 60u), n);
        text_cat(b, n);
        return b;
    }
    if (pg == PG_SEQ && sub == 1u)
        return SEQ_MODE[sc.tp[cur_trk][TP_SMODE]];
    if (pg == PG_SEQ && sub == 2u)
        return PT[sc.tp[cur_trk][TP_SMODE]];
    return PG_TITLE[pg][sub];
}
static const char *page_short(uint32_t pg, uint32_t sub)
{
    if (pg == PG_SEQ && sub == 1u)
        return SEQ_MODE_S[sc.tp[cur_trk][TP_SMODE]];
    return PG_SHORT[pg][sub];
}

/* a value's name and text */
/* refs 0x60 and 0x61 are the UI's own values (SCALE page): scale mode on/off and the keys' octave; not in the scene */
static const pdesc_t PSD[4] = {{"SCALE MODE", 0, 1, K_ENUM, 1, OFF_ON}, {"KEYS OCTAVE", 0, 6, K_OCT, 1, 0},
                               {"CURSOR", 0, 15, K_POS1, 1, 0}, {"ADVANCE", 0, 1, K_ENUM, 1, OFF_ON}};
static const pdesc_t *ref_desc(uint32_t ref)
{
    return ref >= 0x60u && ref < 0x64u ? &PSD[ref - 0x60u] : ref & 0x80u ? &GPD[ref & 0x7Fu] : &TPD[ref];
}
static uint8_t *ref_ptr(uint32_t ref)
{
    if (ref >= 0x60u && ref < 0x64u)
        return ref == 0x60u ? (uint8_t *)&scale_mode : ref == 0x61u ? &ui.oct : ref == 0x62u ? &ui.cursor : &ui.adv;
    return ref & 0x80u ? &sc.gp[ref & 0x7Fu] : &sc.tp[cur_trk][ref];
}
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
    case K_POS1: int_text((int32_t)v + 1, t); break;
    case K_BPM: int_text((int32_t)v, t); text_cat(t, " BPM"); break;
    case K_SLEEP:
        if (!v)
            text_cat(t, "OFF");
        else {
            int_text((int32_t)sleep_minutes(v), t);
            text_cat(t, " MIN");
        }
        break;
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
    if (d->kind == K_SLEEP) {                /* OFF, then 5 to 120 minutes in fives (GP_SLEEP 1, 5, 9 ... 97) */
        int32_t m = clamp((int32_t)sleep_minutes(*p) + e * 5, 0, 120);
        v = m ? (m / 5 - 1) * 4 + 1 : 0;
    }
    if (v == *p)
        return;
    *p = (uint8_t)v;
    if (ref < 0x60u || ref >= 0x64u)
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
    if (pg == PG_SAV && ui.page != PG_SAV)
        ui.proj = am_store.cur < NLIST ? am_store.cur : (uint8_t)NFACT;
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
 * 11 black keys, in the FM-1's groups 3 + 2 + 3 + 2 + 1. On every page the first five pick the track (the first three
 * the tonal voices, the last two the background voices) and the last is MIX; the five between are that page's choices.
 * HOME: PLAY, LATCH. MIX: the five track keys mute or solo (MUTE and SOLO say which), clear the mutes, clear the solos.
 * SAV: LOAD, NEW, SAVE, DEL, NAME (page 1); OK, <, >, SPACE, LIST (page 2). A button's pages are the button again. */
enum { S_NONE, S_TRK, S_INST, S_ENV, S_LFO, S_SEQ, S_ARP, S_SEND,
       S_PLAY, S_LATCH, S_MMODE, S_MTRK, S_MCLR, S_SCLR, S_MIX,
       S_SMODE, S_RAND, S_REST,
       S_PLOAD, S_PNEW, S_PSAVE, S_PDEL, S_PNAME, S_NOK, S_NPREV, S_NNEXT, S_NSPC, S_NLIST };
static const char *const TRK_LBL[NTRK] = {"BEL", "PLK", "PAD", "DRN", "TEX"};
static const char *const INST_SHORT[NINST] = {"DRN", "PAD", "BEL", "PLK", "TEX"};
static const char *const INST_3[NINST] = {"DRN", "PAD", "BEL", "PLK", "TEX"};
static const char *const ENV_LBL[5] = {"PERC", "PLCK", "SWEL", "PAD", "HOLD"};
static const uint8_t ENV_SET[5][4] = {{0, 40, 0, 45}, {0, 62, 0, 62}, {82, 60, 90, 86}, {62, 60, 80, 76}, {12, 60, 100, 52}};
static const char *const LFO_LBL[5] = {"SINE", "TRI", "SAW", "RAND", "DRIFT"};
static const uint8_t LFO_SET[5] = {0, 1, 2, 4, 5};
static const char *const SEQ_LBL[2] = {"NEW", "LOCK"};
static const char *const ARP_LBL[5] = {"OFF", "UP", "DOWN", "UPDN", "RAND"};
static const char *const SEND_LBL[5] = {"DRY", "DLY", "VERB", "BOTH", "WASH"};
static const uint8_t SEND_SET[5][2] = {{0, 0}, {60, 10}, {10, 70}, {50, 60}, {80, 100}};

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
    if (bi < 5u) {                           /* the track keys, on every page */
        *arg = bi;
        *lbl = TRK_LBL[bi];
        if (pg == PG_MIX) {
            *lit = ui.mix_mode ? sc.tp[bi][TP_SOLO] : sc.tp[bi][TP_MUTE];
            return S_MTRK;
        }
        *lit = bi == cur_trk;
        return S_TRK;
    }
    if (pg == PG_HOME)
        return c < 2u ? (*lbl = c ? "LTC" : "PLY", *lit = c ? latch : playing, c ? S_LATCH : S_PLAY) : S_NONE;
    if (pg == PG_MIX) {
        static const char *const ML[4] = {"MUTE", "SOLO", "CLRM", "CLRS"};
        static const uint8_t MA[4] = {S_MMODE, S_MMODE, S_MCLR, S_SCLR};
        if (c > 3u)
            return S_NONE;
        *lbl = ML[c];
        *arg = c;
        *lit = c < 2u && ui.mix_mode == c;
        return MA[c];
    }
    if (pg == PG_SAV) {
        if (sub == 0u) {
            static const char *const L[5] = {"LOAD", "NEW", "SAVE", "DEL", "NAME"};
            static const uint8_t A[5] = {S_PLOAD, S_PNEW, S_PSAVE, S_PDEL, S_PNAME};
            *lbl = L[c];
            *lit = (c == 0u && ui.proj == am_store.cur) || (c == 1u && am_store.cur == NLIST) || (c == 2u && sc_dirty);
            return A[c];
        } else {
            static const char *const L[5] = {"OK", "<", ">", "SPC", "LIST"};
            static const uint8_t A[5] = {S_NOK, S_NPREV, S_NNEXT, S_NSPC, S_NLIST};
            *lbl = L[c];
            return A[c];
        }
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
        if (c == 3u) {                       /* the cursor's hit or step: rest, or play */
            *lbl = "REST";
            return S_REST;
        }
        *lbl = "RAND";
        return S_RAND;
    case PG_ARP: *lbl = ARP_LBL[c]; *lit = c == 0u ? !p[TP_ARPON] : p[TP_ARPON] && p[TP_ARPPAT] == c - 1u; return S_ARP;
    case PG_FX:
        *lbl = SEND_LBL[c];
        *lit = p[TP_DSEND] == SEND_SET[c][0] && p[TP_RSEND] == SEND_SET[c][1];
        return S_SEND;
    default: return S_NONE;
    }
}

static void track_pick(uint32_t t);
static const char NAME_CH[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static void store_write(const char *ok)
{
    char t[12];
    if (plat_store_save(OBJ_SETTINGS, &am_store, sizeof am_store) == 0) {
        sc_dirty = am_store.cur < NFACT ? sc_dirty : 0;
        proj_name(am_store.cur, t);
        app_say(ok, t);
    } else {
        app_say("SAVE", "FAILED");
    }
}
/* SAVE: into the slot the list is on, else the slot playing, else the first free one; a factory project is never overwritten */
static void proj_save(void)
{
    uint32_t n = ui.proj, i;
    char t[12];
    if (n < NFACT || n >= NLIST)
        n = am_store.cur >= NFACT && am_store.cur < NLIST ? am_store.cur : NLIST;
    if (n == NLIST)
        for (i = NFACT; i < NLIST; i++)
            if (!proj_saved(i) && !am_store.name[i - NFACT][0]) {
                n = i;
                break;
            }
    if (n == NLIST) {
        app_say("SAVE", "NO FREE SLOT");
        return;
    }
    if (!am_store.name[n - NFACT][0]) {      /* a name to start from: the project's own, else PROJECT */
        const char *src = am_store.cur < NFACT ? SCENE_NAME[am_store.cur] : "PROJECT";
        for (i = 0; i < NAMELEN; i++)
            am_store.name[n - NFACT][i] = *src ? *src++ : ' ';
    }
    am_store.slot[n - NFACT] = sc;
    am_store.saved |= (uint8_t)(1u << (n - NFACT));
    am_store.cur = (uint8_t)n;
    ui.proj = (uint8_t)n;
    proj_name(n, t);
    store_write("SAVED");
    (void)t;
}
static void proj_delete(void)
{
    uint32_t n = ui.proj, i;
    if (n < NFACT || n >= NLIST) {
        app_say(n < NFACT ? "FACTORY" : "NOTHING", n < NFACT ? "LOCKED" : "THERE");
        return;
    }
    am_store.saved &= (uint8_t)~(1u << (n - NFACT));
    for (i = 0; i < NAMELEN; i++)
        am_store.name[n - NFACT][i] = 0;
    if (am_store.cur == n)
        am_store.cur = NLIST;
    store_write("DELETED");
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
static void scene_step(int32_t d)        /* the PRESET encoder: the previous or next project that holds something */
{
    int32_t i = (int32_t)(am_store.cur < NLIST ? am_store.cur : d > 0 ? NLIST : NLIST) + d;
    char t[12];
    for (; i >= 0 && i < (int32_t)NLIST; i += d)
        if (i < (int32_t)NFACT || proj_saved((uint32_t)i)) {
            scene_use((uint32_t)i);
            ui.proj = (uint8_t)i;
            proj_name((uint32_t)i, t);
            app_say("PROJECT", t);
            return;
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
    if (ui.adv)                              /* the setting: move on after a write */
        ui.cursor = (uint8_t)((ui.cursor + 1u) % seq_count(cur_trk));
}

/* SAV page 2: the letter under the position (e < 0 back, > 0 forward, or 0 with `set`: a space) */
static void name_char(int32_t e, int set)
{
    char *nm;
    uint32_t i, n = ui.proj;
    int32_t ix = 0;
    if (n < NFACT || n >= NLIST)
        return;
    nm = am_store.name[n - NFACT];
    if (!nm[0])
        for (i = 0; i < NAMELEN; i++)
            nm[i] = ' ';
    for (i = 0; NAME_CH[i]; i++)
        if (NAME_CH[i] == nm[ui.npos])
            ix = (int32_t)i;
    ix = set ? 0 : clamp(ix + e, 0, (int32_t)sizeof NAME_CH - 2);
    nm[ui.npos] = NAME_CH[ix];
    if (!set)
        ui.redraw = 1;
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
    case S_TRK: track_pick(arg); break;
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
        app_say("SEQ MODE", SEQ_MODE[arg]);
        break;
    case S_REST:                             /* the cursor's hit or step: play or rest */
        if (p[TP_SMODE] == 1u) {
            sc.hn[cur_trk][ui.cursor] = sc.hn[cur_trk][ui.cursor] == REST ? 0u : REST;
            app_say("HIT", sc.hn[cur_trk][ui.cursor] == REST ? "REST" : "PLAYS");
        } else {
            sc.son[cur_trk] ^= (uint16_t)(1u << ui.cursor);
            app_say("STEP", sc.son[cur_trk] >> ui.cursor & 1u ? "ON" : "OFF");
        }
        sc_dirty = 1;
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
    case S_PLOAD:
        if (ui.proj < NFACT || proj_saved(ui.proj)) {
            char nm[12];
            scene_use(ui.proj);
            proj_name(ui.proj, nm);
            app_say("LOADED", nm);
        } else {
            app_say("SLOT", "EMPTY");
        }
        break;
    case S_PNEW:                             /* clear all: the sounds as they are, every sequencer silent */
        app_panic();
        scene_use(NLIST);
        app_say("NEW PROJECT", "EMPTY");
        break;
    case S_PSAVE: proj_save(); break;
    case S_PDEL: proj_delete(); break;
    case S_PNAME:
        if (ui.proj < NFACT) {
            app_say("FACTORY", "LOCKED");
        } else {
            page_open(PG_SAV, 1);
        }
        break;
    case S_NOK: store_write("NAME SAVED"); break;
    case S_NPREV: ui.npos = (uint8_t)(ui.npos ? ui.npos - 1u : NAMELEN - 1u); break;
    case S_NNEXT: ui.npos = (uint8_t)((ui.npos + 1u) % NAMELEN); break;
    case S_NSPC: name_char(0, 1); break;
    case S_NLIST: page_open(PG_SAV, 0); break;
    case S_PLAY: play_toggle(); break;
    case S_LATCH: latch_toggle(); break;
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
            /* the pattern page: a tap writes the key's note at the cursor (K1 moves the cursor); a key held with K1 / K2
             * turned sets that step's or hit's note and condition; held for 300 ms and let go, it turns that step on or
             * off (a hit into a rest and back). The key sounds as usual. */
            if (pressed & bit) {
                ui.wk1 = (uint8_t)(wi + 1u);
                ui.wk_at = plat_ms();
                ui.hold_turned = 0;
                ui.hold_step = wi < seq_count(cur_trk) ? (int8_t)wi : (int8_t)-1;
                ui.redraw = 1;
            } else if (ui.wk1 == wi + 1u) {
                if (!ui.hold_turned) {
                    if (plat_ms() - ui.wk_at < 300u) {
                        note_enter(wi);
                    } else if (ui.hold_step >= 0) {
                        if (sc.tp[cur_trk][TP_SMODE] == 2u)
                            sc.son[cur_trk] ^= (uint16_t)(1u << wi);
                        else
                            sc.hn[cur_trk][wi] = sc.hn[cur_trk][wi] == REST ? 0u : REST;
                        sc_dirty = 1;
                    }
                }
                ui.wk1 = 0;
                ui.hold_step = -1;
                ui.redraw = 1;
            }
        }
        if (pressed & bit) {
            int32_t note = scale_mode ? scale_key_note(wi) + 12 * ((int32_t)ui.oct - 3) : 53 + (int32_t)k + 12 * ((int32_t)ui.oct - 3);
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
        case B_REC:
            latch_toggle();
            break;
        case B_OCTDN:
        case B_OCTUP: {
            char t[4];
            ui.oct = (uint8_t)clamp((int32_t)ui.oct + (k == B_OCTUP ? 1 : -1), 0, 6);
            ui.oct_at = plat_ms();
            int_text((int32_t)ui.oct - 3, t);
            app_say("KEYS OCTAVE", t);
            break;
        }
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
            if (k == 0u && ui.page == PG_SEQ && ui.sub[PG_SEQ] == 2u && sc.tp[cur_trk][TP_SMODE]) {   /* K1: the cursor */
                int32_t cnt = (int32_t)seq_count(cur_trk), c = ((int32_t)ui.cursor + e) % cnt;
                ui.cursor = (uint8_t)(c < 0 ? c + cnt : c);
                ui.redraw = 1;
                continue;
            }
            if (ui.page == PG_GLO && ui.sub[PG_GLO] == 3u) {   /* BUILD AND CPU: K1 clears the peak */
                if (k == 0u)
                    ui.cpu_max = 0, ui.pk[0] = ui.pk[1] = ui.pk[2] = ui.pk[3] = ui.pk[4] = 0;
                else if (k == 1u) {              /* K2: the fixed stress test on (right) or off (left) */
                    stress_set(e > 0);
                    app_say("STRESS TEST", e > 0 ? "ON" : "OFF");
                }
                continue;
            }
            if (ui.page == PG_SAV) {            /* K1: the list or the position; K2 (NAME): the letter */
                if (ui.sub[PG_SAV] == 0u && k == 0u)
                    ui.proj = (uint8_t)clamp((int32_t)ui.proj + e, 0, (int32_t)NLIST - 1);
                else if (ui.sub[PG_SAV] == 1u && k == 0u)
                    ui.npos = (uint8_t)clamp((int32_t)ui.npos + e, 0, (int32_t)NAMELEN - 1);
                else if (ui.sub[PG_SAV] == 1u && k == 1u)
                    name_char(e, 0);
                ui.redraw = 1;
                continue;
            }
            ref_turn(page_ref(ui.page, ui.sub[ui.page], k), e);
        }
    if (!(ui.page == PG_SEQ && ui.sub[PG_SEQ] == 2u && sc.tp[cur_trk][TP_SMODE])) {
        ui.hold_step = -1;
        ui.wk1 = 0;
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
        {   /* OCT- and OCT+: both light when the octave changes, then the key of the side blinks the shift (once per step) */
            uint32_t ph = plat_ms() - ui.oct_at, o = ui.oct > 3u ? ui.oct - 3u : 3u - ui.oct, bit = 1u << (ui.oct > 3u ? B_OCTUP : B_OCTDN);
            if (ui.oct_at && ph < 700u)
                lb |= (1u << B_OCTUP) | (1u << B_OCTDN);
            else if (o) {
                uint32_t cyc = (ph - 700u) % (o * 400u + 1600u);
                if (cyc < o * 400u && cyc % 400u < 200u)
                    lb |= bit;
            }
        }
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
            if (ui.page == PG_SEQ && ui.sub[PG_SEQ] == 2u && sc.tp[cur_trk][TP_SMODE] == 2u) {
                uint32_t on = sc.son[cur_trk], blink = (plat_ms() / 110u) & 1u;
                for (k = 0; k < sc.tp[cur_trk][TP_SLEN]; k++)   /* the steps: lit when on, dim when off; the step playing blinks */
                    if (playing && k == seq_cur[cur_trk]) {
                        if (blink)
                            lk |= 1u << WK[k];
                    } else if ((on >> k) & 1u) {
                        lk |= 1u << WK[k];
                    } else {
                        dk |= 1u << WK[k];
                    }
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

