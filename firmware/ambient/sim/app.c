/* SPDX-License-Identifier: MPL-2.0 */
/* AMBIENT 0.2: six tracks of generative ambient on fm1-base.
 *
 *   SELECT  the track (1-6)        ALGO  its instrument        PRESET  the scene (1-4)
 *   HOME EDIT ENV LFO SEQ ARP FX GLO   a page of four values for K1-K4; press again for the next page
 *   PLAY start / stop the generators   REC latch   SEL scale mode   SAVE the scene   OCT- OCT+ the keys' octave
 *   Scale mode (on at power-on): the 16 white keys play the scale; the black keys are the screen's submenus, shown
 *   along the bottom of the screen: the first three the button's pages, the next two the track, the next five
 *   this screen's choices; the last one MIX, the mute and solo page (tap: open it; hold: only while held).
 *
 * Part of the unity build (device/unity.c, host/plat_host.c), after the platform: gfx.c and announce.c are in scope. */
#include "fastmath.h"

#define SR 44100.0f
#define NTRK 6u
#define VPT 4u                           /* voices per track */
#define NVOICE (NTRK * VPT)
#define VOICE_CAP 16u                    /* sounding at once, all tracks: the CPU's budget */

/* ------------------------------------------------------------------ tables --- */
enum { I_DRONE, I_PAD, I_BELL, I_PLUCK, I_TEXTURE, NINST };
static const char *const INST_NAME[NINST] = {"DRONE", "PAD", "BELL", "PLUCK", "TEXTURE"};
static const char *const INST_P[NINST][8] = {
    {"DETUNE", "FIFTH", "OCTAVE", "SUB", "BREATH", "DRIVE", "DRIFT", "NOISE"},
    {"DETUNE", "VOICES", "SHAPE", "CHORUS", "HARMONIC", "VIBRATO", "AIR", "WIDTH"},
    {"RATIO", "INDEX", "INDEX DECAY", "BEAT", "STRIKE", "PARTIAL", "SPREAD", "TONE"},
    {"DAMP", "BRIGHT", "SUSTAIN", "POSITION", "BOW", "OCTAVE", "SPREAD", "DRIFT"},
    {"COLOR", "WIDTH", "GUST", "DEPTH", "CRACKLE", "PITCHED", "MOVE", "HISS"},
};
static const int8_t INST_OCT[NINST] = {2, 3, 5, 4, 4};   /* each instrument's home octave */

#define NSCALE 10
static const char *const SCALE_NAME[NSCALE] = {"MAJOR", "MINOR", "DORIAN", "PHRYGIAN", "LYDIAN", "MIXOLYD",
                                               "PENT MAJ", "PENT MIN", "HIRAJOSHI", "WHOLE"};
static const uint8_t SCALE_LEN[NSCALE] = {7, 7, 7, 7, 7, 7, 5, 5, 5, 6};
static const uint8_t SCALE_DEG[NSCALE][7] = {
    {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10},
    {0, 2, 4, 6, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 10}, {0, 2, 4, 7, 9}, {0, 3, 5, 7, 10}, {0, 2, 3, 7, 8},
    {0, 2, 4, 6, 8, 10}};
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const LFO_SHAPE[6] = {"SINE", "TRIANGLE", "SAW", "SQUARE", "RANDOM", "DRIFT"};
static const char *const LFO_TGT[6] = {"PITCH", "CUTOFF", "LEVEL", "PAN", "SOUND A", "SOUND B"};
static const char *const RATE_NAME[7] = {"1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS", "4 BARS"};
static const uint8_t RATE_TICKS[7] = {1, 2, 4, 8, 16, 32, 64};
static const char *const ARP_RATE[4] = {"1/16", "1/8", "1/4", "1/2"};
static const char *const ARP_PAT[4] = {"UP", "DOWN", "UP DOWN", "RANDOM"};
static const char *const OFF_ON[2] = {"OFF", "ON"};
static const char *const MOVE_NAME[4] = {"FIFTH", "FOURTH", "STEP", "ANY"};
static const char *const THEME_NAME[5] = {"GREEN", "AMBER", "CYAN", "RED", "MONO"};
static const char *const SCENE_NAME[4] = {"DEEP", "GLASS", "TIDE", "VOID"};

/* ------------------------------------------------------------------ scene --- */
enum {   /* a track's values */
    TP_INST, TP_P0, TP_P1, TP_P2, TP_P3, TP_P4, TP_P5, TP_P6, TP_P7,
    TP_ATK, TP_DEC, TP_SUS, TP_REL, TP_CUT, TP_RES, TP_FENV, TP_KEYT,
    TP_L1RATE, TP_L1DEP, TP_L1SHP, TP_L1TGT, TP_L2RATE, TP_L2DEP, TP_L2SHP, TP_L2TGT,
    TP_DENS, TP_RATE, TP_GATE, TP_RANGE, TP_STEPS, TP_DEJAVU, TP_OCT, TP_ACCENT,
    TP_ARPON, TP_ARPRATE, TP_ARPPAT, TP_ARPGATE,
    TP_LEVEL, TP_PAN, TP_DSEND, TP_RSEND, TP_MUTE, TP_SOLO, NTP
};
enum {   /* the scene's global values */
    GP_KEY, GP_SCALE, GP_TEMPO, GP_SWING, GP_DRIFT, GP_MOVE, GP_TUNE, GP_THEME,
    GP_HISS, GP_CRACKLE, GP_WOW, GP_AGE,
    GP_DTIME, GP_DFB, GP_DTONE, GP_DMIX, GP_RSIZE, GP_RDAMP, GP_RSHIM, GP_RMIX,
    GP_DARK, GP_ACT, GP_SPACE, NGP
};
typedef struct {
    uint8_t gp[NGP];
    uint8_t tp[NTRK][NTP];
} scene_t;

enum { K_PCT, K_ENUM, K_OCT, K_NOTE, K_BPM, K_CENT, K_INSTP };
typedef struct {
    const char *name;
    uint8_t lo, hi, kind, step;
    const char *const *names;
} pdesc_t;
static const pdesc_t TPD[NTP] = {
    {"INSTRUMENT", 0, NINST - 1, K_ENUM, 1, INST_NAME},
    {"", 0, 100, K_INSTP, 2, 0}, {"", 0, 100, K_INSTP, 2, 0}, {"", 0, 100, K_INSTP, 2, 0}, {"", 0, 100, K_INSTP, 2, 0},
    {"", 0, 100, K_INSTP, 2, 0}, {"", 0, 100, K_INSTP, 2, 0}, {"", 0, 100, K_INSTP, 2, 0}, {"", 0, 100, K_INSTP, 2, 0},
    {"ATTACK", 0, 100, K_PCT, 2, 0}, {"DECAY", 0, 100, K_PCT, 2, 0}, {"SUSTAIN", 0, 100, K_PCT, 2, 0},
    {"RELEASE", 0, 100, K_PCT, 2, 0}, {"CUTOFF", 0, 100, K_PCT, 2, 0}, {"RESONANCE", 0, 100, K_PCT, 2, 0},
    {"ENV AMOUNT", 0, 100, K_PCT, 2, 0}, {"KEY TRACK", 0, 100, K_PCT, 2, 0},
    {"LFO1 RATE", 0, 100, K_PCT, 2, 0}, {"LFO1 DEPTH", 0, 100, K_PCT, 2, 0}, {"LFO1 SHAPE", 0, 5, K_ENUM, 1, LFO_SHAPE},
    {"LFO1 TARGET", 0, 5, K_ENUM, 1, LFO_TGT}, {"LFO2 RATE", 0, 100, K_PCT, 2, 0}, {"LFO2 DEPTH", 0, 100, K_PCT, 2, 0},
    {"LFO2 SHAPE", 0, 5, K_ENUM, 1, LFO_SHAPE}, {"LFO2 TARGET", 0, 5, K_ENUM, 1, LFO_TGT},
    {"DENSITY", 0, 100, K_PCT, 2, 0}, {"RATE", 0, 6, K_ENUM, 1, RATE_NAME}, {"LENGTH", 0, 100, K_PCT, 2, 0},
    {"RANGE", 0, 100, K_PCT, 2, 0}, {"STEPS", 1, 16, K_PCT, 1, 0}, {"DEJA VU", 0, 100, K_PCT, 2, 0},
    {"OCTAVE", 0, 6, K_OCT, 1, 0}, {"ACCENT", 0, 100, K_PCT, 2, 0},
    {"ARP", 0, 1, K_ENUM, 1, OFF_ON}, {"ARP RATE", 0, 3, K_ENUM, 1, ARP_RATE},
    {"PATTERN", 0, 3, K_ENUM, 1, ARP_PAT}, {"ARP GATE", 0, 100, K_PCT, 2, 0},
    {"LEVEL", 0, 100, K_PCT, 2, 0}, {"PAN", 0, 100, K_PCT, 2, 0}, {"DELAY SEND", 0, 100, K_PCT, 2, 0},
    {"REVERB SEND", 0, 100, K_PCT, 2, 0}, {"MUTE", 0, 1, K_ENUM, 1, OFF_ON}, {"SOLO", 0, 1, K_ENUM, 1, OFF_ON},
};
static const pdesc_t GPD[NGP] = {
    {"KEY", 0, 11, K_NOTE, 1, 0}, {"SCALE", 0, NSCALE - 1, K_ENUM, 1, SCALE_NAME}, {"TEMPO", 30, 200, K_BPM, 1, 0},
    {"SWING", 0, 100, K_PCT, 2, 0}, {"DRIFT", 0, 100, K_PCT, 2, 0}, {"MOVE", 0, 3, K_ENUM, 1, MOVE_NAME},
    {"TUNE", 0, 100, K_CENT, 1, 0}, {"THEME", 0, 4, K_ENUM, 1, THEME_NAME},
    {"HISS", 0, 100, K_PCT, 2, 0}, {"CRACKLE", 0, 100, K_PCT, 2, 0}, {"WOW", 0, 100, K_PCT, 2, 0},
    {"AGE", 0, 100, K_PCT, 2, 0},
    {"TIME", 0, 100, K_PCT, 2, 0}, {"FEEDBACK", 0, 100, K_PCT, 2, 0}, {"TONE", 0, 100, K_PCT, 2, 0},
    {"DELAY MIX", 0, 100, K_PCT, 2, 0}, {"SIZE", 0, 100, K_PCT, 2, 0}, {"DAMPING", 0, 100, K_PCT, 2, 0},
    {"SHIMMER", 0, 100, K_PCT, 2, 0}, {"REVERB MIX", 0, 100, K_PCT, 2, 0},
    {"DARK", 0, 100, K_PCT, 2, 0}, {"ACTIVITY", 0, 100, K_PCT, 2, 0}, {"SPACE", 0, 100, K_PCT, 2, 0},
};

/* the pages: button, its pages, four values each (a track value, or G(...) for a global one) */
#define G(x) (0x80u | (x))
enum { PG_HOME, PG_EDIT, PG_ENV, PG_LFO, PG_SEQ, PG_ARP, PG_FX, PG_GLO, PG_MIX, NPG };
static const char *const PG_NAME[NPG] = {"HOME", "EDIT", "ENV", "LFO", "SEQ", "ARP", "FX", "GLO", "MIX"};
static const uint8_t PG_BTN[NPG] = {B_HOME, B_EDIT, B_ENV, B_LFO, B_SEQ, B_ARP, B_FX, B_GLO, 0xFFu};   /* MIX: a black key */
static const uint8_t PG_N[NPG] = {1, 2, 2, 2, 2, 1, 3, 3, 1};
static const char *const PG_TITLE[NPG][3] = {
    {"HOME"}, {"SOUND A", "SOUND B"}, {"AMP ENVELOPE", "FILTER"}, {"LFO 1", "LFO 2"}, {"GENERATOR", "MOTIF"},
    {"ARPEGGIO"}, {"TRACK MIX", "DELAY", "SHIMMER REVERB"}, {"KEY AND TIME", "DRIFT", "AGE"}, {"MUTE AND SOLO"}};
/* the pages' short names, for the black keys */
static const char *const PG_SHORT[NPG][3] = {
    {""}, {"SND A", "SND B"}, {"AMP", "FILT"}, {"LFO1", "LFO2"}, {"GEN", "MOTIF"}, {"ARP"}, {"MIX", "DELAY", "VERB"},
    {"KEY", "DRIFT", "AGE"}, {""}};
static const uint8_t PG_REF[NPG][3][4] = {
    {{G(GP_DARK), G(GP_ACT), G(GP_SPACE), TP_LEVEL}},
    {{TP_P0, TP_P1, TP_P2, TP_P3}, {TP_P4, TP_P5, TP_P6, TP_P7}},
    {{TP_ATK, TP_DEC, TP_SUS, TP_REL}, {TP_CUT, TP_RES, TP_FENV, TP_KEYT}},
    {{TP_L1RATE, TP_L1DEP, TP_L1SHP, TP_L1TGT}, {TP_L2RATE, TP_L2DEP, TP_L2SHP, TP_L2TGT}},
    {{TP_DENS, TP_RATE, TP_GATE, TP_RANGE}, {TP_STEPS, TP_DEJAVU, TP_OCT, TP_ACCENT}},
    {{TP_ARPON, TP_ARPRATE, TP_ARPPAT, TP_ARPGATE}},
    {{TP_LEVEL, TP_PAN, TP_DSEND, TP_RSEND}, {G(GP_DTIME), G(GP_DFB), G(GP_DTONE), G(GP_DMIX)},
     {G(GP_RSIZE), G(GP_RDAMP), G(GP_RSHIM), G(GP_RMIX)}},
    {{G(GP_KEY), G(GP_SCALE), G(GP_TEMPO), G(GP_SWING)}, {G(GP_DRIFT), G(GP_MOVE), G(GP_TUNE), G(GP_THEME)},
     {G(GP_HISS), G(GP_CRACKLE), G(GP_WOW), G(GP_AGE)}},
    {{TP_LEVEL, TP_PAN, TP_DSEND, TP_RSEND}},
};

#define STORE_MAGIC 0x414D4232u          /* "AMB2" */
typedef struct {
    uint32_t magic;
    uint8_t cur, saved, rsv[2];          /* the scene playing; which slots hold a saved scene (bits) */
    scene_t slot[4];
} store_t;
static store_t am_store;
static scene_t sc;                       /* the scene playing: the main loop edits it, the render reads it */
static uint8_t sc_dirty;

/* an instrument's own defaults: its 8 sound values, then A D S R, cutoff, resonance, env amount, key track */
static const uint8_t INST_DEF[NINST][16] = {
    {30, 40, 20, 30, 50, 20, 30, 10, 85, 60, 90, 85, 45, 15, 10, 30},
    {40, 60, 30, 40, 30, 20, 20, 60, 70, 60, 80, 75, 50, 20, 20, 40},
    {50, 40, 50, 20, 30, 40, 50, 50, 0, 70, 0, 70, 80, 10, 0, 50},
    {40, 60, 70, 30, 0, 20, 50, 10, 0, 70, 0, 60, 75, 10, 10, 50},
    {40, 40, 30, 60, 20, 0, 50, 20, 75, 60, 90, 80, 70, 10, 0, 0},
};

static void track_inst(scene_t *s, uint32_t t, uint32_t inst)
{
    uint32_t i;
    s->tp[t][TP_INST] = (uint8_t)inst;
    for (i = 0; i < 8u; i++)
        s->tp[t][TP_P0 + i] = INST_DEF[inst][i];
    for (i = 0; i < 8u; i++)
        s->tp[t][TP_ATK + i] = INST_DEF[inst][8 + i];
}

/* the four factory scenes */
static void scene_factory(scene_t *s, uint32_t n)
{
    static const uint8_t INST[4][NTRK] = {
        {I_DRONE, I_PAD, I_BELL, I_PLUCK, I_TEXTURE, I_BELL},
        {I_PAD, I_BELL, I_BELL, I_PLUCK, I_TEXTURE, I_DRONE},
        {I_DRONE, I_PLUCK, I_PAD, I_BELL, I_TEXTURE, I_PLUCK},
        {I_DRONE, I_TEXTURE, I_BELL, I_PAD, I_TEXTURE, I_DRONE}};
    static const uint8_t KEY[4] = {9, 4, 2, 1}, SCALE[4] = {1, 4, 2, 3}, TEMPO[4] = {70, 84, 76, 60};
    /* per track: density, rate, length, range, level, delay send, reverb send, octave */
    static const uint8_t GEN[NINST][8] = {
        {70, 6, 100, 10, 60, 10, 50, 3}, {40, 5, 90, 40, 45, 30, 60, 3}, {30, 1, 20, 60, 40, 50, 60, 3},
        {35, 1, 20, 50, 40, 40, 40, 3}, {50, 6, 100, 0, 30, 10, 40, 3}};
    uint32_t t;
    for (t = 0; t < NGP; t++)
        s->gp[t] = 0;
    s->gp[GP_KEY] = KEY[n & 3u];
    s->gp[GP_SCALE] = SCALE[n & 3u];
    s->gp[GP_TEMPO] = TEMPO[n & 3u];
    s->gp[GP_SWING] = 0;
    s->gp[GP_DRIFT] = 30;
    s->gp[GP_MOVE] = 0;
    s->gp[GP_TUNE] = 50;
    s->gp[GP_THEME] = 2;
    s->gp[GP_HISS] = 20;
    s->gp[GP_CRACKLE] = n == 3 ? 30 : 10;
    s->gp[GP_WOW] = 20;
    s->gp[GP_AGE] = 20;
    s->gp[GP_DTIME] = 60;
    s->gp[GP_DFB] = 50;
    s->gp[GP_DTONE] = 40;
    s->gp[GP_DMIX] = 50;
    s->gp[GP_RSIZE] = 80;
    s->gp[GP_RDAMP] = 40;
    s->gp[GP_RSHIM] = n == 1 ? 50 : 25;
    s->gp[GP_RMIX] = 60;
    s->gp[GP_DARK] = 40;
    s->gp[GP_ACT] = 50;
    s->gp[GP_SPACE] = 60;
    for (t = 0; t < NTRK; t++) {
        uint32_t inst = INST[n & 3u][t];
        uint8_t *p = s->tp[t];
        uint32_t i;
        for (i = 0; i < NTP; i++)
            p[i] = 0;
        track_inst(s, t, inst);
        p[TP_L1RATE] = (uint8_t)(15 + 7 * t);
        p[TP_L1DEP] = 25;
        p[TP_L1SHP] = 5;
        p[TP_L1TGT] = 1;
        p[TP_L2RATE] = 10;
        p[TP_L2DEP] = 20;
        p[TP_L2SHP] = 0;
        p[TP_L2TGT] = 3;
        p[TP_DENS] = GEN[inst][0];
        p[TP_RATE] = GEN[inst][1];
        p[TP_GATE] = GEN[inst][2];
        p[TP_RANGE] = GEN[inst][3];
        p[TP_STEPS] = (uint8_t)(5 + 2 * t);
        p[TP_DEJAVU] = 60;
        p[TP_OCT] = GEN[inst][7];
        p[TP_ACCENT] = 30;
        p[TP_ARPRATE] = 1;
        p[TP_ARPGATE] = 50;
        p[TP_LEVEL] = GEN[inst][4];
        p[TP_PAN] = (uint8_t)(t & 1u ? 35 + 5 * t : 65 - 5 * t);
        p[TP_DSEND] = GEN[inst][5];
        p[TP_RSEND] = GEN[inst][6];
    }
    if (n == 0)                              /* DEEP: the second bell an octave lower */
        s->tp[5][TP_OCT] = 2, s->tp[5][TP_DENS] = 15, s->tp[5][TP_RATE] = 2;
}

static int scene_ok(const scene_t *s)
{
    uint32_t t, i;
    for (i = 0; i < NGP; i++)
        if (s->gp[i] < GPD[i].lo || s->gp[i] > GPD[i].hi)
            return 0;
    for (t = 0; t < NTRK; t++)
        for (i = 0; i < NTP; i++)
            if (s->tp[t][i] < TPD[i].lo || s->tp[t][i] > TPD[i].hi)
                return 0;
    return 1;
}

/* ------------------------------------------------------------------ shared --- */
static volatile uint8_t playing = 1, scale_mode = 1, latch;
static volatile uint8_t cur_trk;
static volatile uint32_t notes_played, drift_count;
static volatile uint8_t trk_active[NTRK];   /* voices sounding per track (the screen) */

/* a key's note for the render: the main loop queues them, the render plays them */
typedef struct { uint8_t trk, note, vel, on; } kev_t;
static kev_t kq[32];
static volatile uint32_t kq_w, kq_r;
static void key_event(uint32_t trk, int32_t note, uint32_t vel, uint32_t on)
{
    uint32_t w = kq_w;
    if (note < 12 || note > 115 || w - kq_r >= 32u)
        return;
    kq[w & 31u].trk = (uint8_t)trk;
    kq[w & 31u].note = (uint8_t)note;
    kq[w & 31u].vel = (uint8_t)vel;
    kq[w & 31u].on = (uint8_t)on;
    kq_w = w + 1u;
}

/* ------------------------------------------------------------------ random --- */
static uint32_t am_seed = 0x9E3779B9u;
static inline uint32_t am_rnd(void)
{
    am_seed ^= am_seed << 13;
    am_seed ^= am_seed >> 17;
    am_seed ^= am_seed << 5;
    return am_seed;
}
static inline float am_rndf(void) { return (float)(am_rnd() >> 8) * (1.0f / 16777216.0f); }   /* 0..1 */
static inline float am_rnds(void) { return am_rndf() * 2.0f - 1.0f; }                         /* -1..1 */

static inline float pct(uint8_t v) { return (float)v * 0.01f; }
static float note_inc(float note, float tune) { return 440.0f * fm_exp2f((note - 69.0f + tune) * (1.0f / 12.0f)) / SR; }
static float time_s(uint8_t v) { return 0.002f * fm_exp2f(pct(v) * 13.3f); }   /* 2 ms .. 20 s */

/* ------------------------------------------------------------------ voices --- */
#define KS_LEN 1024u
typedef struct {
    uint8_t on, trk, inst, stage;        /* stage: 1 attack, 2 decay, 3 sustain, 4 release */
    int16_t note;
    int32_t gate;                        /* samples until release; -1 held (a key) */
    float inc, vel, pan, env, ienv;      /* ienv: the bell's index envelope; the pluck's excitation */
    float ph[4], drift, x1, x2, x3;
    float s1, s2;                        /* the filter */
    uint32_t age;
    uint16_t kpos, klen;
} voice_t;
static voice_t vc[NVOICE];
static int16_t ks_buf[NVOICE][KS_LEN] PLAT_POOL;
static uint32_t voice_age;

/* per track, worked out once a block */
typedef struct {
    float att, dec_k, sus, rel_k;
    float g, a1, a2, a3, k;              /* the filter at the track's base cutoff (the envelope moves it per voice) */
    float cut_oct, fenv, keyt, res;
    float lvl, lvl0, dlvl, pan, dsend, rsend;   /* lvl0 + dlvl * i: the level ramped over the block (no clicks) */
    float pitch_mod, cut_mod, lvl_mod, pan_mod, pa_mod, pb_mod;
    float lfo_ph[2], lfo_val[2], lfo_rnd[2], lfo_tgt[2];
    float p[8];
} tblk_t;
static tblk_t tb[NTRK];

static void voice_start(uint32_t t, int32_t note, float vel, int32_t gate)
{
    uint32_t i, best = NVOICE, nsound = 0, oldest = NVOICE;
    uint32_t inst = sc.tp[t][TP_INST];
    voice_t *v;
    float tune = ((float)sc.gp[GP_TUNE] - 50.0f) * 0.01f;
    for (i = 0; i < NVOICE; i++)
        nsound += vc[i].on;
    for (i = t * VPT; i < (t + 1u) * VPT; i++)       /* a free voice of the track, else its oldest */
        if (!vc[i].on) {
            best = i;
            break;
        }
    if (best == NVOICE) {
        best = t * VPT;
        for (i = t * VPT; i < (t + 1u) * VPT; i++)
            if (vc[i].age < vc[best].age)
                best = i;
    } else if (nsound >= VOICE_CAP) {                /* over the budget: steal the quietest anywhere */
        float lo = 9.0f;
        for (i = 0; i < NVOICE; i++)
            if (vc[i].on && vc[i].env < lo) {
                lo = vc[i].env;
                oldest = i;
            }
        if (oldest < NVOICE)
            vc[oldest].on = 0;
    }
    v = &vc[best];
    v->on = 1;
    v->trk = (uint8_t)t;
    v->inst = (uint8_t)inst;
    v->stage = 1;
    v->note = (int16_t)note;
    v->gate = gate;
    v->vel = vel;
    v->inc = note_inc((float)note, tune);
    v->pan = 0.0f;
    v->env = 0.0f;
    v->ienv = 1.0f;
    v->drift = 0.0f;
    v->x1 = v->x2 = v->x3 = 0.0f;
    v->age = ++voice_age;
    if (inst == I_BELL || inst == I_PLUCK)
        v->pan = am_rnds() * pct(sc.tp[t][TP_P6]) * 0.8f;
    else if (inst == I_PAD)                           /* WIDTH */
        v->pan = am_rnds() * pct(sc.tp[t][TP_P7]) * 0.9f;
    if (inst == I_PLUCK) {                            /* fill the string with noise, coloured by BRIGHT */
        float hz = v->inc * SR, b = 0.05f + 0.95f * pct(sc.tp[t][TP_P1]), lp = 0.0f;
        uint32_t len, j, pos;
        while (hz < SR / (float)(KS_LEN - 2u))
            hz *= 2.0f;
        len = (uint32_t)(SR / hz);
        v->klen = (uint16_t)len;
        v->kpos = 0;
        pos = (uint32_t)(len * (0.05f + 0.45f * pct(sc.tp[t][TP_P3])));
        for (j = 0; j < len; j++) {
            lp += (am_rnds() - lp) * b;
            ks_buf[best][j] = (int16_t)(lp * 12000.0f * vel);
        }
        for (j = len; j-- > pos;)                     /* the pick's position: a comb on the excitation */
            ks_buf[best][j] = (int16_t)((ks_buf[best][j] - ks_buf[best][j - pos]) / 2);
    }
    for (i = 0; i < 4u; i++)
        v->ph[i] = am_rndf();
    v->s1 = v->s2 = 0.0f;
    notes_played++;
}

static void voice_release_note(uint32_t t, int32_t note)
{
    uint32_t i;
    for (i = t * VPT; i < (t + 1u) * VPT; i++)
        if (vc[i].on && vc[i].note == note && vc[i].gate < 0 && vc[i].stage < 4u)
            vc[i].stage = 4;
}

/* one sample of a voice's instrument, before the filter */
static inline float voice_osc(voice_t *v, const tblk_t *b, float inc)
{
    const float *p = b->p;
    float o;
    switch (v->inst) {
    case I_DRONE: {
        float det = 0.0005f + p[0] * 0.012f, s0, s1;
        v->ph[0] += inc * (1.0f + det);
        v->ph[1] += inc * (1.0f - det);
        v->ph[2] += inc * 1.4983f;
        v->ph[3] += inc * 0.5f;
        v->ph[0] -= (float)(int32_t)v->ph[0];
        v->ph[1] -= (float)(int32_t)v->ph[1];
        v->ph[2] -= (float)(int32_t)v->ph[2];
        v->ph[3] -= (float)(int32_t)v->ph[3];
        s0 = fm_sin_turns(v->ph[0]);
        s1 = fm_sin_turns(v->ph[1]);
        o = s0 + s1 + p[1] * 1.2f * fm_sin_turns(v->ph[2]) + p[2] * 0.8f * fm_sin_turns(2.0f * v->ph[0])
            + p[3] * 1.4f * fm_sin_turns(v->ph[3]);
        v->x1 += (am_rnds() - v->x1) * 0.02f;          /* noise, softened */
        o += p[7] * 1.5f * v->x1;
        o = fm_tanhf(o * (0.35f + 1.2f * p[5])) * 1.3f;
        o *= 1.0f - p[4] * 0.6f * (0.5f + 0.5f * fm_sin_turns(v->x3));
        break;
    }
    case I_PAD: {
        float det = 0.001f + p[0] * 0.01f * (1.0f + p[3] * 0.5f * fm_sin_turns(v->x3)), sh = p[2] * 0.45f;
        float a, c, d;
        v->ph[0] += inc;
        v->ph[1] += inc * (1.0f + det);
        v->ph[2] += inc * (1.0f - det);
        v->ph[3] += inc * 2.0f;
        v->ph[0] -= (float)(int32_t)v->ph[0];
        v->ph[1] -= (float)(int32_t)v->ph[1];
        v->ph[2] -= (float)(int32_t)v->ph[2];
        v->ph[3] -= (float)(int32_t)v->ph[3];
        a = fm_sin_turns(v->ph[0]);
        c = fm_sin_turns(v->ph[1] + sh * fm_sin_turns(v->ph[1]));
        d = fm_sin_turns(v->ph[2] + sh * fm_sin_turns(v->ph[2]));
        o = a * (1.0f - p[1] * 0.5f) + (c + d) * (0.3f + 0.5f * p[1]) + p[4] * 0.6f * fm_sin_turns(v->ph[3]);
        v->x1 += (am_rnds() - v->x1) * 0.3f;
        o += p[6] * 0.4f * v->x1;
        o *= 0.6f;
        break;
    }
    case I_BELL: {
        static const float RATIO[8] = {0.5f, 1.0f, 1.414f, 2.0f, 2.76f, 3.5f, 4.2f, 5.4f};
        float r = RATIO[(int32_t)(p[0] * 7.0f + 0.5f)], ix, m;
        v->ph[0] += inc;
        v->ph[1] += inc * r;
        v->ph[2] += inc * (1.0f + 0.002f + p[3] * 0.01f);
        v->ph[3] += inc * 2.756f;
        v->ph[0] -= (float)(int32_t)v->ph[0];
        v->ph[1] -= (float)(int32_t)v->ph[1];
        v->ph[2] -= (float)(int32_t)v->ph[2];
        v->ph[3] -= (float)(int32_t)v->ph[3];
        ix = (0.2f + p[1] * 4.0f) * v->ienv * (0.4f + p[7]);
        m = ix * fm_sin_turns(v->ph[1]) * 0.159f;
        o = fm_sin_turns(v->ph[0] + m) + p[3] * 0.7f * fm_sin_turns(v->ph[2] + m)
            + p[5] * 0.5f * v->ienv * fm_sin_turns(v->ph[3]);
        if (v->x2 > 1e-4f) {                            /* the strike: a burst of noise */
            o += am_rnds() * v->x2 * p[4] * 2.0f;
            v->x2 *= 0.995f;
        }
        v->ienv *= v->x1;
        o *= 0.7f;
        break;
    }
    case I_PLUCK: {
        int16_t *buf = ks_buf[v - vc];
        uint32_t len = v->klen, pos = v->kpos, nx = pos + 1u == len ? 0u : pos + 1u;
        float a = buf[pos] * (1.0f / 12000.0f), c = buf[nx] * (1.0f / 12000.0f), y;
        float damp = 0.5f - p[0] * 0.45f, fb = 0.985f + p[2] * 0.0148f;
        y = (a * (1.0f - damp) + c * damp) * fb;
        if (p[4] > 0.0f)                                /* the bow: noise fed into the string */
            y += am_rnds() * p[4] * 0.02f;
        buf[pos] = (int16_t)fm_clampf(y * 12000.0f, -32000.0f, 32000.0f);
        v->kpos = (uint16_t)nx;
        o = a;
        if (p[5] > 0.0f) {                             /* the octave: a second tap half the string away */
            uint32_t h = pos + len / 2u;
            o += p[5] * buf[h >= len ? h - len : h] * (1.0f / 12000.0f);
        }
        o *= 1.4f;
        break;
    }
    default: {   /* TEXTURE: noise through a band-pass, gusts, crackle */
        float n = am_rnds(), hz, f, q, bp;
        v->ph[0] += 0.3f / SR + p[2] * 2.0f / SR;      /* the gust's slow random swell */
        if (v->ph[0] >= 1.0f) {
            v->ph[0] -= 1.0f;
            v->ph[1] = am_rndf();
        }
        v->x3 += (v->ph[1] - v->x3) * 0.0002f;
        v->ph[2] = fm_clampf(v->ph[2] + n * 0.0006f, -1.0f, 1.0f);   /* MOVE: the band's slow random walk */
        hz = 80.0f * fm_exp2f(p[0] * 7.0f + p[6] * 2.0f * v->ph[2]);
        hz += (inc * SR - hz) * p[5];
        f = fm_clampf(6.2831853f * hz / SR, 0.0005f, 1.2f);
        q = 0.05f + (1.0f - p[1]) * 0.9f;
        v->x1 += f * v->x2;                              /* a Chamberlin band-pass */
        bp = v->x2;
        v->x2 += f * (n - v->x1 - q * v->x2);
        o = bp * (0.8f + 1.6f * (1.0f - p[1])) * (1.0f - p[3] + p[3] * v->x3 * 1.6f) + n * p[7] * 0.15f;
        if (am_rndf() < p[4] * p[4] * 0.0015f)
            o += am_rnds() * 1.5f;
        break;
    }
    }
    return o;
}

/* ------------------------------------------------------------------ effects --- */
#define DL_LEN 32768u                    /* the delay: up to 0.74 s */
static int16_t dl_l[DL_LEN] PLAT_POOL, dl_r[DL_LEN] PLAT_POOL;
static uint32_t dl_w;
static float dl_t, dl_lpl, dl_lpr;

#define RV_LEN 4096u                     /* the reverb: four lines and a pitch shifter for the shimmer */
static const float RV_BASE[4] = {1557.0f, 1617.0f, 1491.0f, 1422.0f};
static int16_t rv_line[4][RV_LEN] PLAT_POOL;
static float rv_lp[4];
static uint32_t rv_w;
#define AP_LEN 1024u
static int16_t rv_ap[2][AP_LEN] PLAT_POOL;
static const uint32_t AP_D[2] = {347u, 883u};
#define PS_LEN 4096u
static int16_t ps_buf[PS_LEN] PLAT_POOL;
static uint32_t ps_w;
static float ps_ph, rv_mod, ps_hp, rv_dc[4];

#define WOW_LEN 2048u                    /* the master's wow and flutter */
static int16_t wow_l[WOW_LEN] PLAT_POOL, wow_r[WOW_LEN] PLAT_POOL;
static uint32_t wow_w;
static float wow_ph, flut_ph, wow_rnd, age_lpl, age_lpr, hiss_b[3], hiss_lvl, crk, crk_k, crk_pan;
static float gain_now;

static inline float dl_read(const int16_t *b, float d)
{
    float pos = (float)dl_w - d, fr;
    int32_t i;
    if (pos < 0.0f)
        pos += (float)DL_LEN;
    i = (int32_t)pos;
    fr = pos - (float)i;
    return (b[i & (DL_LEN - 1)] * (1.0f - fr) + b[(i + 1) & (DL_LEN - 1)] * fr) * (1.0f / 8192.0f);
}

/* ------------------------------------------------------------------ the clock and the generators --- */
static float clk_left;                   /* samples to the next sixteenth */
static uint32_t clk_tick;
typedef struct { int8_t deg[16]; uint8_t have; uint8_t pos; } motif_t;
static motif_t mo[NTRK];
static uint8_t held[NTRK][8], held_n[NTRK], arp_i[NTRK];
static int8_t arp_dir[NTRK] = {1, 1, 1, 1, 1, 1};

static int32_t degree_note(int32_t deg, int32_t oct)
{
    uint32_t sci = sc.gp[GP_SCALE], len = SCALE_LEN[sci];
    int32_t o = deg >= 0 ? deg / (int32_t)len : -((-deg + (int32_t)len - 1) / (int32_t)len);
    int32_t d = deg - o * (int32_t)len;
    return 12 * (oct + o + 1) + sc.gp[GP_KEY] + SCALE_DEG[sci][d];
}

static uint32_t track_audible(uint32_t t)
{
    uint32_t i, solo = 0;
    for (i = 0; i < NTRK; i++)
        solo |= sc.tp[i][TP_SOLO];
    return solo ? sc.tp[t][TP_SOLO] : !sc.tp[t][TP_MUTE];
}

static void gen_step(uint32_t t, float tick_s)
{
    uint8_t *p = sc.tp[t];
    motif_t *m = &mo[t];
    uint32_t rt = RATE_TICKS[p[TP_RATE]], steps = p[TP_STEPS], len = SCALE_LEN[sc.gp[GP_SCALE]];
    int32_t deg, span, oct = INST_OCT[p[TP_INST]] + (int32_t)p[TP_OCT] - 3;
    float dens = pct(p[TP_DENS]) * (0.3f + 1.4f * pct(sc.gp[GP_ACT]));
    if (clk_tick % rt)
        return;
    if (m->pos >= steps)
        m->pos = 0;
    span = 1 + (int32_t)(pct(p[TP_RANGE]) * (float)(len * 2u));
    if (!(m->have >> 0 & 1u) || am_rndf() >= pct(p[TP_DEJAVU])) {   /* deja vu: replay the motif, or write a new step */
        int32_t d = am_rndf() < dens ? (int32_t)(am_rnd() % (uint32_t)span) : -100;
        if (d != -100 && p[TP_INST] == I_DRONE && am_rndf() < 0.6f)
            d = (int32_t)(am_rnd() % 3u) * (int32_t)(len > 5u ? 2u : 1u) * 2 % span;   /* drones like roots and fifths */
        m->deg[m->pos] = (int8_t)d;
        if (m->pos + 1u >= steps)
            m->have = 1;
    }
    deg = m->deg[m->pos];
    m->pos++;
    if (deg == -100 || !playing)
        return;
    {
        float gate = tick_s * (float)rt * (0.05f + 3.95f * pct(p[TP_GATE]) * pct(p[TP_GATE]));
        float vel = 1.0f - pct(p[TP_ACCENT]) * am_rndf() * 0.7f;
        voice_start(t, degree_note(deg, oct), vel, (int32_t)(gate * SR));
    }
}

static void arp_step(uint32_t t, float tick_s)
{
    static const uint8_t AR[4] = {1, 2, 4, 8};
    uint8_t *p = sc.tp[t];
    uint32_t n = held_n[t], rt = AR[p[TP_ARPRATE]], k;
    if (!p[TP_ARPON] || !n || clk_tick % rt)
        return;
    switch (p[TP_ARPPAT]) {
    case 0: k = arp_i[t] % n; arp_i[t] = (uint8_t)((k + 1u) % n); break;
    case 1: k = (n - 1u) - arp_i[t] % n; arp_i[t] = (uint8_t)((arp_i[t] + 1u) % n); break;
    case 2:
        k = arp_i[t] % n;
        if (n > 1u) {
            if ((int32_t)k + arp_dir[t] < 0 || k + (uint32_t)arp_dir[t] >= n)
                arp_dir[t] = (int8_t)-arp_dir[t];
            arp_i[t] = (uint8_t)((int32_t)k + arp_dir[t]);
        }
        break;
    default: k = am_rnd() % n; break;
    }
    voice_start(t, held[t][k], 0.9f, (int32_t)(tick_s * (float)rt * (0.1f + 1.4f * pct(p[TP_ARPGATE])) * SR));
}

static void held_add(uint32_t t, uint32_t note)
{
    uint32_t i, j;
    for (i = 0; i < held_n[t]; i++)
        if (held[t][i] == note)
            return;
    if (held_n[t] >= 8u)
        return;
    for (i = 0; i < held_n[t] && held[t][i] < note; i++)
        ;
    for (j = held_n[t]; j > i; j--)
        held[t][j] = held[t][j - 1];
    held[t][i] = (uint8_t)note;
    held_n[t]++;
}
static void held_remove(uint32_t t, uint32_t note)
{
    uint32_t i, j;
    for (i = 0; i < held_n[t]; i++)
        if (held[t][i] == note) {
            for (j = i; j + 1u < held_n[t]; j++)
                held[t][j] = held[t][j + 1];
            held_n[t]--;
            return;
        }
}

static void key_play(const kev_t *e)
{
    uint32_t t = e->trk;
    if (sc.tp[t][TP_ARPON]) {                       /* with the arpeggio, keys only add notes to its list */
        if (e->on)
            held_add(t, e->note);
        else
            held_remove(t, e->note);
        return;
    }
    if (e->on)
        voice_start(t, e->note, (float)e->vel / 127.0f, -1);
    else
        voice_release_note(t, e->note);
}

static void drift_bar(void)
{
    static const int8_t STEP[4] = {7, 5, 2, 0};
    int32_t k = sc.gp[GP_KEY], s;
    if (!playing || am_rndf() >= pct(sc.gp[GP_DRIFT]) * 0.12f)
        return;
    s = STEP[sc.gp[GP_MOVE]];
    if (s == 0)
        s = 1 + (int32_t)(am_rnd() % 11u);
    else if (am_rnd() & 1u)
        s = -s;
    sc.gp[GP_KEY] = (uint8_t)((k + s + 12) % 12);
    drift_count++;
}

/* ------------------------------------------------------------------ the render --- */
static float lfo_value(tblk_t *b, uint32_t j, uint32_t shape, float dph)
{
    float ph = b->lfo_ph[j] + dph, v;
    if (ph >= 1.0f) {
        ph -= 1.0f;
        b->lfo_rnd[j] = am_rnds();
    }
    b->lfo_ph[j] = ph;
    switch (shape) {
    case 0: v = fm_sin_turns(ph); break;
    case 1: v = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph; break;
    case 2: v = 1.0f - 2.0f * ph; break;
    case 3: v = ph < 0.5f ? 1.0f : -1.0f; break;
    case 4: v = b->lfo_rnd[j]; break;
    default: b->lfo_tgt[j] += (b->lfo_rnd[j] - b->lfo_tgt[j]) * 0.02f; v = b->lfo_tgt[j]; break;
    }
    return v;
}

static void track_block(uint32_t t, uint32_t n)
{
    tblk_t *b = &tb[t];
    const uint8_t *p = sc.tp[t];
    float dark = pct(sc.gp[GP_DARK]), lf, cut;
    uint32_t j, i;
    b->att = 1.0f / (time_s(p[TP_ATK]) * SR);
    b->dec_k = fm_expf(-6.9f / (time_s(p[TP_DEC]) * SR));
    b->sus = pct(p[TP_SUS]);
    b->rel_k = fm_expf(-6.9f / (time_s(p[TP_REL]) * SR));
    b->pitch_mod = b->cut_mod = b->lvl_mod = b->pan_mod = b->pa_mod = b->pb_mod = 0.0f;
    for (j = 0; j < 2u; j++) {
        float rate = 0.01f * fm_exp2f(pct(p[TP_L1RATE + 4 * j]) * 10.0f);   /* 0.01 .. 10 Hz */
        float d = pct(p[TP_L1DEP + 4 * j]);
        lf = lfo_value(b, j, p[TP_L1SHP + 4 * j], rate * (float)n / SR) * d;
        b->lfo_val[j] = lf;
        switch (p[TP_L1TGT + 4 * j]) {
        case 0: b->pitch_mod += lf * 0.5f; break;      /* +-0.5 semitone at full depth */
        case 1: b->cut_mod += lf * 3.0f; break;        /* +-3 octaves */
        case 2: b->lvl_mod += lf * 0.5f; break;
        case 3: b->pan_mod += lf * 0.8f; break;
        case 4: b->pa_mod += lf * 0.5f; break;
        default: b->pb_mod += lf * 0.5f; break;
        }
    }
    for (i = 0; i < 8u; i++)
        b->p[i] = pct(p[TP_P0 + i]);
    b->p[0] = fm_clampf(b->p[0] + b->pa_mod, 0.0f, 1.0f);
    b->p[4] = fm_clampf(b->p[4] + b->pb_mod, 0.0f, 1.0f);
    cut = 4.9f + pct(p[TP_CUT]) * 9.3f - dark * 3.5f + b->cut_mod;   /* log2 Hz: 30 Hz .. 19 kHz */
    b->cut_oct = cut;
    b->fenv = pct(p[TP_FENV]) * 7.0f;
    b->keyt = pct(p[TP_KEYT]);
    b->res = pct(p[TP_RES]);
    b->k = 2.0f - 1.9f * b->res;
    b->lvl0 = b->lvl;
    b->lvl = pct(p[TP_LEVEL]) * fm_clampf(1.0f + b->lvl_mod, 0.0f, 1.5f) * (track_audible(t) ? 1.0f : 0.0f);
    b->dlvl = (b->lvl - b->lvl0) / (float)n;
    b->pan = fm_clampf(pct(p[TP_PAN]) * 2.0f - 1.0f + b->pan_mod, -1.0f, 1.0f);
    b->dsend = pct(p[TP_DSEND]) * pct(sc.gp[GP_SPACE]) * 1.6f;
    b->rsend = pct(p[TP_RSEND]) * pct(sc.gp[GP_SPACE]) * 1.6f;
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i, t, k;
    float gain = (float)master_q12 / 4096.0f * 8300000.0f;
    float tick_s = 15.0f / (float)sc.gp[GP_TEMPO];
    float tune = ((float)sc.gp[GP_TUNE] - 50.0f) * 0.01f;
    /* delay, reverb and age settings, once a block */
    float dl_target = (0.02f + 0.72f * pct(sc.gp[GP_DTIME])) * SR, dfb = pct(sc.gp[GP_DFB]) * 0.92f;
    float dtone = 0.08f + 0.85f * pct(sc.gp[GP_DTONE]), dmix = pct(sc.gp[GP_DMIX]);
    float rsize = pct(sc.gp[GP_RSIZE]), rg = 0.55f + 0.44f * rsize, rdamp = 0.9f - 0.8f * pct(sc.gp[GP_RDAMP]);
    float rshim = pct(sc.gp[GP_RSHIM]), rmix = pct(sc.gp[GP_RMIX]);
    float rlen[4];
    float hiss = pct(sc.gp[GP_HISS]), crack = pct(sc.gp[GP_CRACKLE]), wow = pct(sc.gp[GP_WOW]), age = pct(sc.gp[GP_AGE]);
    float age_a = 1.0f - fm_expf(-6.2831853f * (18000.0f * fm_exp2f(-age * 3.0f)) / SR);
    uint32_t vbase_cut[NVOICE];
    float vg[NVOICE], va1[NVOICE], va2[NVOICE], va3[NVOICE], vinc[NVOICE];
    (void)vbase_cut;
    for (k = 0; k < 4u; k++)
        rlen[k] = RV_BASE[k] * (0.45f + 2.0f * rsize);

    /* the keys and MIDI in */
    while (kq_r != kq_w) {
        key_play(&kq[kq_r & 31u]);
        kq_r = kq_r + 1u;
    }
    {
        uint32_t pkt;
        int port;
        while (plat_midi_in(&pkt, &port)) {
            uint32_t st = (pkt >> 8) & 0xF0u, ch = (pkt >> 8) & 0x0Fu, d1 = (pkt >> 16) & 0x7Fu, d2 = pkt >> 24;
            kev_t e;
            if (ch >= NTRK)
                ch = cur_trk;
            e.trk = (uint8_t)ch;
            e.note = (uint8_t)d1;
            e.vel = (uint8_t)d2;
            if (st == 0x90u && d2) {
                e.on = 1;
                key_play(&e);
            } else if (st == 0x80u || st == 0x90u) {
                e.on = 0;
                key_play(&e);
            }
        }
    }
    /* the clock: sixteenths, swung */
    clk_left -= (float)n;
    while (clk_left <= 0.0f) {
        float sw = pct(sc.gp[GP_SWING]) * 0.33f;
        for (t = 0; t < NTRK; t++) {
            gen_step(t, tick_s);
            arp_step(t, tick_s);
        }
        if (clk_tick % 16u == 0u)
            drift_bar();
        clk_left += tick_s * SR * (clk_tick & 1u ? 1.0f - sw : 1.0f + sw);
        clk_tick++;
    }
    for (t = 0; t < NTRK; t++) {
        track_block(t, n);
        trk_active[t] = 0;
    }
    /* each voice's filter and pitch for this block */
    for (k = 0; k < NVOICE; k++) {
        voice_t *v = &vc[k];
        tblk_t *b;
        float hz, g, a1;
        if (!v->on)
            continue;
        if (v->inst != sc.tp[v->trk][TP_INST]) {       /* the track changed instrument */
            v->on = 0;
            continue;
        }
        b = &tb[v->trk];
        trk_active[v->trk]++;
        hz = fm_exp2f(fm_clampf(b->cut_oct + b->fenv * v->env + b->keyt * ((float)v->note - 60.0f) / 12.0f,
                                4.9f, 14.2f));
        if (hz > 0.45f * SR)
            hz = 0.45f * SR;
        g = fm_tanf(3.14159265f * hz / SR);
        a1 = 1.0f / (1.0f + g * (g + b->k));
        vg[k] = g;
        va1[k] = a1;
        va2[k] = g * a1;
        va3[k] = g * g * a1;
        vinc[k] = v->inc * fm_exp2f((b->pitch_mod + (v->inst == I_DRONE ? v->drift : 0.0f)
                                     + (v->inst == I_PAD ? b->p[5] * 0.15f * fm_sin_turns(v->x2) : 0.0f)
                                     + (v->inst == I_PLUCK ? v->drift : 0.0f)) * (1.0f / 12.0f));
        if (v->inst == I_DRONE || v->inst == I_PLUCK)  /* drift: a slow random walk in pitch */
        {
            float dr = v->inst == I_PLUCK ? b->p[7] : b->p[6];   /* DRIFT: the drone's 7th value, the pluck's 8th */
            v->drift = fm_clampf(v->drift + am_rnds() * dr * 0.02f, -0.3f * dr, 0.3f * dr);
        }
        if (v->inst == I_BELL && v->stage == 1u) {     /* the bell's index decay, set at its start */
            v->x1 = fm_expf(-6.9f / ((0.05f + 3.0f * (1.0f - b->p[2])) * SR));
            v->x2 = 1.0f;
        }
        v->x3 += (0.05f + 0.15f * b->p[3]) * (float)n / SR;   /* the breath and chorus LFO */
        if (v->x3 >= 1.0f)
            v->x3 -= 1.0f;
        v->x2 += (v->inst == I_PAD) ? 5.0f * (float)n / SR : 0.0f;
        if (v->inst == I_PAD && v->x2 >= 1.0f)
            v->x2 -= 1.0f;
        if (v->gate > 0) {
            v->gate -= (int32_t)n;
            if (v->gate <= 0) {
                v->gate = 0;
                v->stage = 4;
            }
        }
    }
    (void)tune;

    for (i = 0; i < n; i++) {
        float l = 0.0f, r = 0.0f, ds_l = 0.0f, ds_r = 0.0f, rs = 0.0f;
        float dl, dr, rin, rout_l, rout_r;
        gain_now += (gain - gain_now) * 0.004f;
        for (k = 0; k < NVOICE; k++) {
            voice_t *v = &vc[k];
            tblk_t *b;
            float o, v1, v2, v3, e, gl, gr, pn;
            if (!v->on)
                continue;
            b = &tb[v->trk];
            /* the envelope */
            e = v->env;
            switch (v->stage) {
            case 1: e += b->att; if (e >= 1.0f) { e = 1.0f; v->stage = 2; } break;
            case 2: e = b->sus + (e - b->sus) * b->dec_k; if (e - b->sus < 0.001f) v->stage = 3; break;
            case 3: e = b->sus; if (e < 0.0005f) { v->on = 0; } break;
            default: e *= b->rel_k; if (e < 0.0002f) v->on = 0; break;
            }
            v->env = e;
            o = voice_osc(v, b, vinc[k]);
            /* the filter (low-pass) */
            v3 = o - v->s2;
            v1 = va1[k] * v->s1 + va2[k] * v3;
            v2 = v->s2 + va2[k] * v->s1 + va3[k] * v3;
            v->s1 = 2.0f * v1 - v->s1;
            v->s2 = 2.0f * v2 - v->s2;
            o = v2 * e * v->vel * (b->lvl0 + b->dlvl * (float)i);
            pn = fm_clampf(b->pan + v->pan, -1.0f, 1.0f);
            gl = 0.5f - 0.5f * pn;
            gr = 0.5f + 0.5f * pn;
            l += o * gl;
            r += o * gr;
            ds_l += o * gl * b->dsend;
            ds_r += o * gr * b->dsend;
            rs += o * b->rsend;
        }
        (void)vg;
        /* the delay: smoothed time, filtered feedback, a little cross-feed */
        dl_t += (dl_target - dl_t) * 0.00005f;
        dl = dl_read(dl_l, dl_t);
        dr = dl_read(dl_r, dl_t * 1.13f > (float)(DL_LEN - 4u) ? (float)(DL_LEN - 4u) : dl_t * 1.13f);
        dl_lpl += (dl - dl_lpl) * dtone;
        dl_lpr += (dr - dl_lpr) * dtone;
        dl_l[dl_w] = (int16_t)(fm_clampf(ds_l + dfb * (0.8f * dl_lpl + 0.2f * dl_lpr), -3.9f, 3.9f) * 8192.0f);
        dl_r[dl_w] = (int16_t)(fm_clampf(ds_r + dfb * (0.8f * dl_lpr + 0.2f * dl_lpl), -3.9f, 3.9f) * 8192.0f);
        dl_w = (dl_w + 1u) & (DL_LEN - 1u);
        l += dl_lpl * dmix;
        r += dl_lpr * dmix;
        rs += (dl_lpl + dl_lpr) * 0.25f * dmix;
        /* the reverb: two all-passes into four lines, mixed (Hadamard), damped; the shimmer is an octave up
         * pitch shifter (two crossfaded taps) fed back into the lines */
        rin = rs * 0.5f;
        for (k = 0; k < 2u; k++) {
            uint32_t ri = (rv_w - AP_D[k]) & (AP_LEN - 1u);
            float dly = rv_ap[k][ri] * (1.0f / 8192.0f), y = dly - 0.6f * rin;
            rv_ap[k][rv_w & (AP_LEN - 1u)] = (int16_t)(fm_clampf(rin + 0.6f * y, -3.9f, 3.9f) * 8192.0f);
            rin = y;
        }
        {
            float a[4], h[4], shim;
            rv_mod += 0.6f / SR;
            if (rv_mod >= 1.0f)
                rv_mod -= 1.0f;
            for (k = 0; k < 4u; k++) {
                float d = rlen[k] + (k & 1u ? 6.0f : -6.0f) * fm_sin_turns(rv_mod + 0.25f * (float)k);
                float pos = (float)rv_w - d, fr;
                int32_t ix;
                if (pos < 0.0f)
                    pos += (float)RV_LEN;
                ix = (int32_t)pos;
                fr = pos - (float)ix;
                a[k] = (rv_line[k][ix & (RV_LEN - 1)] * (1.0f - fr) + rv_line[k][(ix + 1) & (RV_LEN - 1)] * fr)
                       * (1.0f / 8192.0f);
                rv_lp[k] += (a[k] - rv_lp[k]) * rdamp;
                rv_dc[k] += (rv_lp[k] - rv_dc[k]) * 0.0007f;   /* a DC blocker in each line (about 5 Hz) */
                a[k] = rv_lp[k] - rv_dc[k];
            }
            h[0] = (a[0] + a[1] + a[2] + a[3]) * 0.5f;
            h[1] = (a[0] - a[1] + a[2] - a[3]) * 0.5f;
            h[2] = (a[0] + a[1] - a[2] - a[3]) * 0.5f;
            h[3] = (a[0] - a[1] - a[2] + a[3]) * 0.5f;
            rout_l = a[0] + a[2];
            rout_r = a[1] + a[3];
            /* shimmer: write the tail, read it back at double speed through two taps 180 degrees apart */
            ps_buf[ps_w] = (int16_t)(fm_clampf((rout_l + rout_r) * 0.5f, -3.9f, 3.9f) * 8192.0f);
            {
                float wnd = 2048.0f, d1, d2, w1, s1, s2;
                uint32_t i1, i2;
                ps_ph += 1.0f / wnd;
                if (ps_ph >= 1.0f)
                    ps_ph -= 1.0f;
                d1 = wnd * (1.0f - ps_ph);
                d2 = wnd * (1.0f - (ps_ph >= 0.5f ? ps_ph - 0.5f : ps_ph + 0.5f));
                i1 = (ps_w - (uint32_t)d1) & (PS_LEN - 1u);
                i2 = (ps_w - (uint32_t)d2) & (PS_LEN - 1u);
                w1 = fm_sin_turns(ps_ph * 0.5f);
                w1 *= w1;
                s1 = ps_buf[i1] * (1.0f / 8192.0f);
                s2 = ps_buf[i2] * (1.0f / 8192.0f);
                shim = s1 * w1 + s2 * (1.0f - w1);
                /* a high-pass (about 180 Hz) on the shimmer: an octave up leaves DC and the lows where they are,
                 * so without it the loop builds up there */
                ps_hp += (shim - ps_hp) * 0.0253f;
                shim -= ps_hp;
            }
            ps_w = (ps_w + 1u) & (PS_LEN - 1u);
            for (k = 0; k < 4u; k++) {
                float in = rin + (k < 2u ? shim : -shim) * rshim * 0.55f;
                rv_line[k][rv_w & (RV_LEN - 1u)] = (int16_t)(fm_clampf(h[k] * rg + in, -3.9f, 3.9f) * 8192.0f);
            }
            rv_w = (rv_w + 1u) & (RV_LEN - 1u);
        }
        l += rout_l * rmix * 0.7f;
        r += rout_r * rmix * 0.7f;
        /* AGE: wow and flutter (a modulated short delay), hiss, crackle, a softer top and a little drive */
        {
            float pink, hs, d, pos, fr, wl, wr;
            int32_t ix;
            wow_ph += 0.45f / SR;
            if (wow_ph >= 1.0f) {
                wow_ph -= 1.0f;
                wow_rnd = am_rndf();
            }
            flut_ph += 6.3f / SR;
            if (flut_ph >= 1.0f)
                flut_ph -= 1.0f;
            wow_l[wow_w] = (int16_t)(fm_clampf(l, -3.9f, 3.9f) * 8192.0f);
            wow_r[wow_w] = (int16_t)(fm_clampf(r, -3.9f, 3.9f) * 8192.0f);
            d = 8.0f + wow * (60.0f * (0.5f + 0.5f * fm_sin_turns(wow_ph)) * (0.6f + 0.4f * wow_rnd)
                              + 6.0f * (0.5f + 0.5f * fm_sin_turns(flut_ph)));
            pos = (float)wow_w - d;
            if (pos < 0.0f)
                pos += (float)WOW_LEN;
            ix = (int32_t)pos;
            fr = pos - (float)ix;
            wl = (wow_l[ix & (WOW_LEN - 1)] * (1.0f - fr) + wow_l[(ix + 1) & (WOW_LEN - 1)] * fr) * (1.0f / 8192.0f);
            wr = (wow_r[ix & (WOW_LEN - 1)] * (1.0f - fr) + wow_r[(ix + 1) & (WOW_LEN - 1)] * fr) * (1.0f / 8192.0f);
            wow_w = (wow_w + 1u) & (WOW_LEN - 1u);
            if (wow > 0.0f) {
                l = wl;
                r = wr;
            }
            /* hiss: pink-ish noise (three one-poles), its level wandering slowly */
            pink = am_rnds();
            hiss_b[0] = 0.997f * hiss_b[0] + 0.029591f * pink;
            hiss_b[1] = 0.985f * hiss_b[1] + 0.032534f * pink;
            hiss_b[2] = 0.950f * hiss_b[2] + 0.048056f * pink;
            hs = hiss_b[0] + hiss_b[1] + hiss_b[2] + pink * 0.05f;
            hiss_lvl += ((0.6f + 0.4f * wow_rnd) - hiss_lvl) * 0.00001f;
            hs *= hiss * hiss * 0.25f * hiss_lvl;
            /* crackle: random clicks of random size, each a short decaying burst */
            if (am_rndf() < crack * crack * 0.0012f) {
                float sz = am_rndf();
                crk = am_rnds() * (0.05f + 0.6f * sz * sz * sz);
                crk_k = 0.3f + 0.6f * am_rndf();
                crk_pan = am_rndf();
            }
            l += hs + crk * crk_pan;
            r += hs * 0.9f + am_rnds() * hiss * hiss * 0.01f + crk * (1.0f - crk_pan);
            crk *= -crk_k;
            /* the worn top and a little drive */
            age_lpl += (l - age_lpl) * age_a;
            age_lpr += (r - age_lpr) * age_a;
            l = age_lpl;
            r = age_lpr;
        }
        l = fm_tanhf(l * (1.4f + age * 0.8f));
        r = fm_tanhf(r * (1.4f + age * 0.8f));
        lr[2 * i] = (int32_t)(l * gain_now);
        lr[2 * i + 1] = (int32_t)(r * gain_now);
    }
}

void app_panic(void)
{
    uint32_t k;
    for (k = 0; k < NVOICE; k++)
        vc[k].on = 0;
    for (k = 0; k < NTRK; k++)
        held_n[k] = 0;
}

/* --------------------------------------------------------------- the am_store --- */
static void scene_use(uint32_t n)
{
    scene_t s;
    n &= 3u;
    if (am_store.saved >> n & 1u)
        s = am_store.slot[n];
    else
        scene_factory(&s, n);
    if (!scene_ok(&s))
        scene_factory(&s, n);
    sc = s;
    am_store.cur = (uint8_t)n;
    palette_set(sc.gp[GP_THEME]);
    sc_dirty = 0;
}

void app_init(void)
{
    if (plat_store_load(OBJ_SETTINGS, &am_store, sizeof am_store) != (int)sizeof am_store || am_store.magic != STORE_MAGIC) {
        am_store.magic = STORE_MAGIC;
        am_store.cur = 0;
        am_store.saved = 0;
    }
    am_seed ^= plat_ms() * 2654435761u + 1u;
    scene_use(am_store.cur);
}

/* --------------------------------------------------------------------- UI --- */
static struct {
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;
    uint8_t page, sub[NPG];              /* the page shown, and each button's page */
    uint8_t latched[NKEYS];
    uint8_t prev_page, mix_mode;         /* the page MIX came from; MIX's black keys: 0 mute, 1 solo */
    uint32_t mix_at;                     /* when the MIX key went down (held long: MIX only while held) */
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
    app_say(t, PG_TITLE[pg][sub]);
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
       S_PLAY, S_LATCH, S_SPREV, S_SNEXT, S_MMODE, S_MTRK, S_MCLR, S_SCLR, S_MIX };
static const int8_t BK_TRK[11] = {0, 1, 2, -1, -1, 3, 4, 5, -1, -1, -1};
static const char *const TRK_LBL[NTRK] = {"T1", "T2", "T3", "T4", "T5", "T6"};
static const char *const INST_SHORT[NINST] = {"DRONE", "PAD", "BELL", "PLUCK", "TEXT"};
static const char *const INST_3[NINST] = {"DRN", "PAD", "BEL", "PLK", "TEX"};
static const char *const ENV_LBL[5] = {"PERC", "PLUCK", "SWELL", "PAD", "HOLD"};
static const uint8_t ENV_SET[5][4] = {{0, 40, 0, 45}, {0, 62, 0, 62}, {82, 60, 90, 86}, {62, 60, 80, 76}, {12, 60, 100, 52}};
static const char *const LFO_LBL[5] = {"SINE", "TRI", "SAW", "RAND", "DRIFT"};
static const uint8_t LFO_SET[5] = {0, 1, 2, 4, 5};
static const char *const SEQ_LBL[5] = {"NEW", "LOCK", "FREE", "SPARS", "BUSY"};
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
        *lbl = PG_SHORT[pg][bi];
        *lit = bi == sub;
        return S_SUB;
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
    case PG_SEQ:
        *lbl = SEQ_LBL[c];
        *lit = (c == 1u && p[TP_DEJAVU] == 100) || (c == 2u && p[TP_DEJAVU] == 0);
        return S_SEQ;
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
        if (arg == 0u)
            mo[cur_trk].have = 0;            /* a new motif: written afresh on the next pass */
        else if (arg == 1u)
            p[TP_DEJAVU] = 100;
        else if (arg == 2u)
            p[TP_DEJAVU] = 0;
        else
            p[TP_DENS] = arg == 3u ? 20 : 80;
        if (arg)
            sc_dirty = 1;
        app_say("MOTIF", SEQ_LBL[arg]);
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
        if ((e = plat_enc((int)(EN_K1 + k))) != 0)
            ref_turn(PG_REF[ui.page][ui.sub[ui.page]][k], e);
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
            for (k = 0; k < 16u; k++)
                if (k % len == 0u)
                    dk |= 1u << WK[k];          /* the roots glow */
        }
        plat_lights(lb, lk, 0x3FFFu & ~lb, dk, 1);
    }
}

void app_input_only(void) { input(); }

static void draw(void)
{
    static const uint16_t TCOL[NTRK] = {RGB(90, 150, 220), RGB(140, 110, 220), RGB(230, 190, 90), RGB(110, 200, 150),
                                        RGB(200, 120, 110), RGB(170, 170, 180)};
    char t[32];
    uint32_t k, now = plat_ms(), pg = ui.page, sub = ui.sub[pg];
    /* the header: the track and the page, or a message */
    cv_begin(240, 36, C_BLACK);
    if (now < ui.msg_until) {
        int32_t x = cv_text(8, 8, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 8, &FONT_B, ui.msg[1], C_HI);
    } else {
        char n[2] = {(char)('1' + cur_trk), 0};
        t[0] = 0;
        text_cat(t, n);
        text_cat(t, " ");
        text_cat(t, INST_NAME[sc.tp[cur_trk][TP_INST]]);
        cv_rect(0, 0, 4, 36, TCOL[cur_trk]);
        cv_text(10, 2, &FONT_B, t, C_WHITE);
        t[0] = 0;
        text_cat(t, PG_NAME[pg]);
        if (PG_N[pg] > 1u) {
            char s[6] = {' ', (char)('1' + sub), '/', (char)('0' + PG_N[pg]), 0, 0};
            text_cat(t, s);
        }
        cv_text(232 - text_w(&FONT_B, t), 2, &FONT_B, t, C_HI);
        cv_text(10, 20, &FONT_XS, PG_TITLE[pg][sub], C_GRAY);
        t[0] = 0;
        text_cat(t, SCENE_NAME[am_store.cur]);
        if (sc_dirty)
            text_cat(t, " *");
        if (scale_mode) {                    /* the footer is the key map: the load and the tempo go up here */
            char c[8];
            int_text((int32_t)sc.gp[GP_TEMPO], c);
            text_cat(t, "  ");
            text_cat(t, c);
            text_cat(t, playing ? " BPM  CPU " : " STOP  CPU ");
            int_text((int32_t)plat_cpu_pct(), c);
            text_cat(t, c);
            text_cat(t, "%");
        }
        cv_text(232 - text_w(&FONT_XS, t), 20, &FONT_XS, t, C_DIM);
    }
    cv_blit(0, 0);
    /* the middle: HOME shows the six tracks, every other page its four values */
    cv_begin(240, 144, C_BLACK);
    if (pg == PG_MIX) {                      /* six strips: level, activity, M and S; the track's mix below */
        for (k = 0; k < NTRK; k++) {
            int32_t x = 4 + (int32_t)k * 39, h = (int32_t)sc.tp[k][TP_LEVEL] * 56 / 100;
            uint32_t aud = track_audible(k), mu = sc.tp[k][TP_MUTE], so = sc.tp[k][TP_SOLO];
            char n[2] = {(char)('1' + k), 0};
            if (k == cur_trk)
                cv_rect(x - 2, 0, 38, 98, C_LINE);
            cv_rect(x, 0, 34, 3, TCOL[k]);
            cv_text(x + 2, 4, &FONT_B, n, aud ? C_WHITE : C_DIM);
            cv_text(x + 14, 7, &FONT_T, INST_3[sc.tp[k][TP_INST]], aud ? C_GRAY : C_DIM);
            cv_rect(x + 4, 24, 8, 56, C_DIM);
            cv_rect(x + 4, 80 - h, 8, h, aud ? TCOL[k] : C_LINE);
            cv_rect(x + 18, 74, 12, 6, trk_active[k] && aud ? TCOL[k] : C_LINE);   /* sounding */
            cv_rect(x + 16, 26, 16, 14, mu ? C_AMB : C_BLACK);
            cv_text(x + 20, 26, &FONT_XS, "M", mu ? C_BLACK : C_DIM);
            cv_rect(x + 16, 44, 16, 14, so ? C_WHITE : C_BLACK);
            cv_text(x + 21, 44, &FONT_XS, "S", so ? C_BLACK : C_DIM);
            cv_text(x + 2, 83, &FONT_T, aud ? "ON" : (mu ? "MUTED" : "OFF"), aud ? C_HI : C_DIM);
        }
        for (k = 0; k < 4u; k++) {
            uint32_t ref = PG_REF[PG_MIX][0][k];
            int32_t x = 4 + (int32_t)k * 59;
            cv_text(x, 102, &FONT_T, ref_name(ref), C_GRAY);
            ref_text(ref, t);
            cv_text(x, 116, &FONT_S, t, C_HI);
            cv_rect(x, 136, 54, 3, C_DIM);
            cv_rect(x, 136, (int32_t)(ref_frac(ref) * 54u / 200u), 3, TCOL[cur_trk]);
        }
    } else if (pg == PG_HOME) {
        for (k = 0; k < NTRK; k++) {
            int32_t y = (int32_t)k * 16;
            uint32_t w = sc.tp[k][TP_LEVEL];
            uint16_t c = track_audible(k) ? C_HI : C_DIM;
            char n[2] = {(char)('1' + k), 0};
            if (k == cur_trk)
                cv_rect(0, y, 240, 15, C_LINE);
            cv_rect(4, y + 2, 3, 11, TCOL[k]);
            cv_text(12, y, &FONT_XS, n, C_WHITE);
            cv_text(24, y, &FONT_XS, INST_NAME[sc.tp[k][TP_INST]], k == cur_trk ? C_WHITE : C_GRAY);
            cv_rect(100, y + 6, 100, 3, C_DIM);
            cv_rect(100, y + 6, (int32_t)w, 3, c);
            if (sc.tp[k][TP_MUTE])
                cv_text(206, y, &FONT_XS, "M", C_AMB);
            if (sc.tp[k][TP_SOLO])
                cv_text(218, y, &FONT_XS, "S", C_WHITE);
            if (trk_active[k])
                cv_rect(230, y + 5, 5, 5, TCOL[k]);
        }
        for (k = 0; k < 4u; k++) {
            uint32_t ref = PG_REF[PG_HOME][0][k];
            int32_t x = 4 + (int32_t)k * 59;
            cv_text(x, 102, &FONT_XS, ref_name(ref), C_GRAY);
            ref_text(ref, t);
            cv_text(x, 116, &FONT_S, t, C_HI);
            cv_rect(x, 136, 54, 3, C_DIM);
            cv_rect(x, 136, (int32_t)(ref_frac(ref) * 54u / 200u), 3, C_HI);
        }
    } else {
        for (k = 0; k < 4u; k++) {
            uint32_t ref = PG_REF[pg][sub][k];
            int32_t y = (int32_t)k * 36;
            cv_text(10, y + 2, &FONT_XS, "K", C_DIM);
            int_text((int32_t)k + 1, t);
            cv_text(18, y + 2, &FONT_XS, t, C_DIM);
            cv_text(32, y, &FONT_S, ref_name(ref), C_GRAY);
            ref_text(ref, t);
            cv_text(232 - text_w(&FONT_B, t), y, &FONT_B, t, C_HI);
            cv_rect(32, y + 22, 200, 5, C_LINE);
            cv_rect(32, y + 22, (int32_t)ref_frac(ref), 5, (ref & 0x80u) ? C_AMB : TCOL[cur_trk]);
        }
    }
    cv_blit(0, 40);
    /* the footer: in scale mode the black keys' map (each key's label over its place on the keyboard), else the
     * six tracks, the key and scale, the modes, the load */
    cv_begin(240, 52, C_BLACK);
    if (scale_mode) {
        static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
        for (k = 0; k < 16u; k++)                    /* the white keys: the roots marked */
            cv_rect((int32_t)k * 15 + 1, 30, 13, 21, k % SCALE_LEN[sc.gp[GP_SCALE]] == 0u ? C_DIM : C_LINE);
        for (k = 0; k < 11u; k++) {
            uint32_t arg, lit, w, j, a;
            const char *lbl;
            int32_t cx, x, lw;
            uint16_t col;
            a = slot(k, &arg, &lbl, &lit);
            for (w = 0, j = 0; j < 16u; j++)         /* the white key just below it */
                if (WK[j] < BK[k])
                    w = j;
            cx = (int32_t)(w + 1u) * 15;
            col = a == S_NONE ? C_BLACK : lit ? ((a == S_TRK || a == S_MTRK) ? TCOL[arg] : C_HI) : C_GRAY;
            cv_rect(cx - 5, 30, 10, 14, C_BLACK);
            cv_rect(cx - 4, 30, 8, 13, a == S_NONE ? C_LINE : lit ? col : C_DIM);
            if (a == S_NONE)
                continue;
            lw = text_w(&FONT_T, lbl);
            x = clamp(cx - lw / 2, 1, 239 - lw);
            cv_text(x, (k & 1u) ? 14 : 1, &FONT_T, lbl, lit ? (a == S_MTRK && ui.mix_mode == 0u ? C_AMB : C_WHITE)
                                                         : ((a == S_TRK || a == S_MTRK) ? TCOL[arg] : C_GRAY));
        }
        cv_blit(0, 188);
        return;
    }
    for (k = 0; k < NTRK; k++) {
        int32_t x = 6 + (int32_t)k * 22;
        char n[2] = {(char)('1' + k), 0};
        uint16_t c = k == cur_trk ? TCOL[k] : C_LINE;
        cv_rect(x, 4, 18, 16, c);
        cv_text(x + 6, 4, &FONT_XS, n, track_audible(k) ? C_WHITE : C_DIM);
        if (trk_active[k])
            cv_rect(x, 21, 18, 2, TCOL[k]);
    }
    t[0] = 0;
    text_cat(t, NOTE_NAME[sc.gp[GP_KEY] % 12u]);
    text_cat(t, " ");
    text_cat(t, SCALE_NAME[sc.gp[GP_SCALE]]);
    cv_text(140, 4, &FONT_XS, t, C_HI);
    cv_text(6, 30, &FONT_XS, playing ? "PLAY" : "STOP", playing ? C_HI : C_DIM);
    cv_text(42, 30, &FONT_XS, "SCALE", scale_mode ? C_HI : C_DIM);
    cv_text(82, 30, &FONT_XS, "LATCH", latch ? C_HI : C_DIM);
    int_text((int32_t)sc.gp[GP_TEMPO], t);
    text_cat(t, " BPM");
    cv_text(124, 30, &FONT_XS, t, C_GRAY);
    t[0] = 0;
    text_cat(t, "CPU ");
    {
        char c[8];
        int_text((int32_t)plat_cpu_pct(), c);
        text_cat(t, c);
        text_cat(t, "%");
    }
    cv_text(234 - text_w(&FONT_XS, t), 30, &FONT_XS, t, plat_cpu_pct() > 80u ? C_WHITE : C_GRAY);
    cv_blit(0, 188);
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.page = PG_HOME;
    ui.redraw = 1;
}

void app_frame(void)
{
    static uint32_t last_n, last_t, last_key;
    uint32_t now = plat_ms();
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
    else if (!strcmp(what, "dejavu"))
        *got = sc.tp[cur_trk][TP_DEJAVU];
    else if (!strcmp(what, "arp"))
        *got = sc.tp[cur_trk][TP_ARPON] ? sc.tp[cur_trk][TP_ARPPAT] + 1 : 0;
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
