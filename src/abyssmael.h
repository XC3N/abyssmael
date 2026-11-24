/* SPDX-License-Identifier: MPL-2.0 */
/* ABYSSMAEL: the contract between the modules (see MODULES.md): the tables, the scene, the shared state and each
 * module's calls. */
#pragma once
#include <stdint.h>
#include "plat.h"
#ifdef AMB_ENGINE   /* the engine runs from RAM: its helpers are always inlined (a call out of line would reach the flash) */
#define inline inline __attribute__((always_inline))
#endif
#include "fastmath.h"
#ifdef AMB_ENGINE
#undef inline
#endif
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#pragma GCC diagnostic ignored "-Wunused-variable"

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
static const char *const SEQ_MODE[3] = {"GENERATOR", "EUCLID", "STEP SEQ"};
static const char *const SEQ_MODE_S[3] = {"GEN", "EUCL", "STEP"};
static const char *const DIR_NAME[4] = {"FWD", "BACK", "PING", "RAND"};
/* a hit's (EUCL) or step's (STEP) condition, tested on each pass of the pattern; PRE: this track's last tested hit played;
 * NEI: the track above it (the last one for track 1) played on this tick; FIRST: the first pass after play starts */
enum { C_ALWAYS, C_1_2, C_2_2, C_1_3, C_2_3, C_3_3, C_1_4, C_2_4, C_3_4, C_4_4, C_FIRST, C_NFIRST, C_PRE, C_NPRE, C_NEI, C_NNEI, NCOND };
static const char *const COND_NAME[NCOND] = {"ALWAYS", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4",
                                             "FIRST", "!FIRST", "PRE", "!PRE", "NEI", "!NEI"};
static const char *const ARP_SRC[3] = {"KEYS", "SEQ", "BOTH"};
#define REST 31u                         /* a Euclidean hit's note: none */

/* ------------------------------------------------------------------ scene --- */
enum {   /* a track's values */
    TP_INST, TP_P0, TP_P1, TP_P2, TP_P3, TP_P4, TP_P5, TP_P6, TP_P7,
    TP_ATK, TP_DEC, TP_SUS, TP_REL, TP_CUT, TP_RES, TP_FENV, TP_KEYT,
    TP_L1RATE, TP_L1DEP, TP_L1SHP, TP_L1TGT, TP_L2RATE, TP_L2DEP, TP_L2SHP, TP_L2TGT,
    TP_DENS, TP_RATE, TP_GATE, TP_RANGE, TP_STEPS, TP_DEJAVU, TP_OCT, TP_ACCENT,
    TP_SMODE, TP_EHITS, TP_ELEN, TP_EROT, TP_ECHANCE, TP_SLEN, TP_SDIR, TP_SCHANCE, TP_SMUT,
    TP_EDIR, TP_EDRIFT,
    TP_ARPON, TP_ARPRATE, TP_ARPPAT, TP_ARPGATE, TP_ARPSRC,
    TP_LEVEL, TP_PAN, TP_DSEND, TP_RSEND, TP_MUTE, TP_SOLO, NTP
};
enum {   /* the scene's global values */
    GP_KEY, GP_SCALE, GP_TEMPO, GP_SWING, GP_DRIFT, GP_MOVE, GP_TUNE, GP_THEME,
    GP_HISS, GP_CRACKLE, GP_WOW, GP_AGE,
    GP_DTIME, GP_DFB, GP_DTONE, GP_DMIX, GP_RSIZE, GP_RDAMP, GP_RSHIM, GP_RMIX,
    GP_DARK, GP_ACT, GP_SPACE, GP_SLEEP, NGP
};
typedef struct {
    uint8_t gp[NGP];
    uint8_t tp[NTRK][NTP];
    uint8_t hn[NTRK][16];                /* EUCLID: each hit's note, a scale degree (0-15) or REST */
    uint8_t sn[NTRK][16];                /* STEP: each step's note, a scale degree (0-15) */
    uint16_t son[NTRK];                  /* STEP: the steps that play (bits) */
    uint8_t cd[NTRK][16];                /* the condition of each EUCLID hit or STEP step (C_...) */
} scene_t;

enum { K_PCT, K_ENUM, K_OCT, K_NOTE, K_BPM, K_CENT, K_INSTP, K_SLEEP };
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
    {"SEQ MODE", 0, 2, K_ENUM, 1, SEQ_MODE}, {"HITS", 0, 16, K_PCT, 1, 0}, {"STEPS", 1, 16, K_PCT, 1, 0},
    {"ROTATE", 0, 15, K_PCT, 1, 0}, {"CHANCE", 0, 100, K_PCT, 2, 0},
    {"STEPS", 1, 16, K_PCT, 1, 0}, {"DIRECTION", 0, 3, K_ENUM, 1, DIR_NAME}, {"CHANCE", 0, 100, K_PCT, 2, 0},
    {"MUTATE", 0, 100, K_PCT, 2, 0},
    {"DIRECTION", 0, 3, K_ENUM, 1, DIR_NAME}, {"DRIFT", 0, 100, K_PCT, 2, 0},
    {"ARP", 0, 1, K_ENUM, 1, OFF_ON}, {"ARP RATE", 0, 3, K_ENUM, 1, ARP_RATE},
    {"PATTERN", 0, 3, K_ENUM, 1, ARP_PAT}, {"ARP GATE", 0, 100, K_PCT, 2, 0},
    {"ARP FROM", 0, 2, K_ENUM, 1, ARP_SRC},
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
    {"SLEEP TIMER", 0, 100, K_SLEEP, 1, 0},
};

/* the pages: button, its pages, four values each (a track value, or G(...) for a global one) */
#define G(x) (0x80u | (x))
enum { PG_HOME, PG_EDIT, PG_ENV, PG_LFO, PG_SEQ, PG_ARP, PG_FX, PG_GLO, PG_MIX, PG_SCALE, NPG };
static const char *const PG_NAME[NPG] = {"HOME", "EDIT", "ENV", "LFO", "SEQ", "ARP", "FX", "GLO", "MIX", "SCALE"};
static const uint8_t PG_BTN[NPG] = {B_HOME, B_EDIT, B_ENV, B_LFO, B_SEQ, B_ARP, B_FX, B_GLO, 0xFFu, B_SEL};   /* MIX: a black key */
static const uint8_t PG_N[NPG] = {2, 2, 2, 2, 3, 2, 3, 4, 1, 1};   /* GLO's fourth page is on black key 4 (BUILD AND CPU) */
static const char *const PG_TITLE[NPG][4] = {
    {"HOME", "SESSION"}, {"SOUND A", "SOUND B"}, {"AMP ENVELOPE", "FILTER"}, {"LFO 1", "LFO 2"}, {"SEQUENCER", "", "PATTERN"},
    {"ARPEGGIO", "ARP SOURCE"}, {"TRACK MIX", "DELAY", "SHIMMER REVERB"}, {"KEY AND TIME", "DRIFT", "AGE", "BUILD AND CPU"}, {"MUTE AND SOLO"}, {"KEY, SCALE, KEYS"}};
/* the pages' short names, for the black keys */
static const char *const PG_SHORT[NPG][4] = {
    {""}, {"SND A", "SND B"}, {"AMP", "FILT"}, {"LFO1", "LFO2"}, {"SEQ", "", "PATT"}, {"ARP", "SRC"}, {"MIX", "DELAY", "VERB"},
    {"KEY", "DRIFT", "AGE", "INFO"}, {""}, {"SCALE"}};
static const uint8_t PG_REF[NPG][4][4] = {
    {{G(GP_DARK), G(GP_ACT), G(GP_SPACE), TP_LEVEL}, {G(GP_SLEEP), G(GP_TEMPO), G(GP_SWING), G(GP_DRIFT)}},
    {{TP_P0, TP_P1, TP_P2, TP_P3}, {TP_P4, TP_P5, TP_P6, TP_P7}},
    {{TP_ATK, TP_DEC, TP_SUS, TP_REL}, {TP_CUT, TP_RES, TP_FENV, TP_KEYT}},
    {{TP_L1RATE, TP_L1DEP, TP_L1SHP, TP_L1TGT}, {TP_L2RATE, TP_L2DEP, TP_L2SHP, TP_L2TGT}},
    {{TP_RATE, TP_GATE, TP_OCT, TP_ACCENT}, {0}, {0}},   /* SEQ 2 and 3: the mode's (seq_ref) */
    {{TP_ARPON, TP_ARPRATE, TP_ARPPAT, TP_ARPGATE}, {TP_ARPSRC, TP_ARPON, TP_LEVEL, TP_PAN}},
    {{TP_LEVEL, TP_PAN, TP_DSEND, TP_RSEND}, {G(GP_DTIME), G(GP_DFB), G(GP_DTONE), G(GP_DMIX)},
     {G(GP_RSIZE), G(GP_RDAMP), G(GP_RSHIM), G(GP_RMIX)}},
    {{G(GP_KEY), G(GP_SCALE), G(GP_TEMPO), G(GP_SWING)}, {G(GP_DRIFT), G(GP_MOVE), G(GP_TUNE), G(GP_THEME)},
     {G(GP_HISS), G(GP_CRACKLE), G(GP_WOW), G(GP_AGE)}},
    {{TP_LEVEL, TP_PAN, TP_DSEND, TP_RSEND}},
    {{G(GP_KEY), G(GP_SCALE), 0x60, 0x61}},   /* SCALE: 0x60 scale mode on/off, 0x61 the keys' octave (the UI's own) */
};

#define STORE_MAGIC 0x414D4235u          /* "AMB5" (conditions, drift, arp source, sleep) */

/* SEQ pages 2 and 3: the track's mode's four values */
static const uint8_t MODE_REF[3][4] = {{TP_DENS, TP_RANGE, TP_STEPS, TP_DEJAVU},
                                       {TP_EHITS, TP_ELEN, TP_EROT, TP_EDRIFT},
                                       {TP_SLEN, TP_SDIR, TP_SCHANCE, TP_SMUT}};
typedef struct {
    uint32_t magic;
    uint8_t cur, saved, rsv[2];          /* the scene playing; which slots hold a saved scene (bits) */
    scene_t slot[4];
} store_t;

/* ------------------------------------------------------------------ shared --- */
typedef struct { uint8_t trk, note, vel, on; } kev_t;
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
typedef struct { int8_t deg[16]; uint8_t have; uint8_t pos; } motif_t;

extern scene_t sc;
extern volatile uint8_t playing, cur_trk;
extern volatile uint32_t notes_played, drift_count;
extern volatile uint8_t trk_active[NTRK];
/* SLEEP TIMER (core.c). GP_SLEEP is 0 (OFF) or 1..100: the minutes are sleep_minutes(), 5 to 120 in steps of 5. Setting it
 * starts the countdown; the last SLEEP_FADE_S seconds fade the sound out; at zero the engine stops and amb_asleep is 1:
 * the render outputs silence and the main loop (app.c) blanks the screen and the lights until a key or button is pressed.
 * Waking, the main loop sets GP_SLEEP to 0. */
#define SLEEP_FADE_S 60u
extern volatile uint8_t amb_asleep;
uint32_t sleep_minutes(uint32_t gp_sleep);   /* 0 .. 120 */
uint32_t sleep_left_s(void);                 /* seconds to go (0 when off) */
/* the CPU share of each render stage, in % of the block time: seq, engines_block, engines_run (both cores), fx. prof_pct is
 * smoothed, prof_max a peak that decays in a few seconds. The total is plat_cpu_pct(). (core.c) */
extern volatile uint8_t prof_pct[5], prof_max[5];   /* [4]: the whole app_render call (R), so C - R is the rest of the audio interrupt */
extern voice_t vc[NVOICE];
extern motif_t mo[NTRK];
extern uint8_t seq_pos[NTRK], seq_cur[NTRK];
extern uint8_t eu_hits_now[NTRK], eu_rot_now[NTRK];   /* EUCLID: the hits and rotation in play (HITS and ROTATE plus DRIFT) */

/* random numbers: each unit its own */
static uint32_t am_seed = 0x9E3779B9u;
static inline __attribute__((always_inline)) uint32_t am_rnd(void)
{
    am_seed ^= am_seed << 13;
    am_seed ^= am_seed >> 17;
    am_seed ^= am_seed << 5;
    return am_seed;
}
static inline __attribute__((always_inline)) float am_rndf(void) { return (float)(am_rnd() >> 8) * (1.0f / 16777216.0f); }   /* 0..1 */
static inline __attribute__((always_inline)) float am_rnds(void) { return am_rndf() * 2.0f - 1.0f; }                         /* -1..1 */
static inline __attribute__((always_inline)) float pct(uint8_t v) { return (float)v * 0.01f; }
static inline int32_t amb_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* a track's values worked out once a block (the engines' own) */
typedef struct {
    float att, dec_k, sus, rel_k;
    float g, a1, a2, a3, k;              /* the filter at the track's base cutoff (the envelope moves it per voice) */
    float cut_oct, fenv, keyt, res;
    float lvl, lvl0, dlvl, pan, dsend, rsend;   /* lvl0 + dlvl * i: the level ramped over the block (no clicks) */
    float pitch_mod, cut_mod, lvl_mod, pan_mod, pa_mod, pb_mod;
    float lfo_ph[2], lfo_val[2], lfo_rnd[2], lfo_tgt[2];
    float p[8];
} tblk_t;
/* each core's summed voices for a block: the stereo mix, the two delay sends, the reverb send */
typedef struct { float l[APP_BLOCK], r[APP_BLOCK], dl[APP_BLOCK], dr[APP_BLOCK], rs[APP_BLOCK]; } mix_t;
extern mix_t mx[2];
extern uint8_t amb_two_cores;

/* a denormal costs a lot on the FPU: tiny states are set to zero once a block */
static inline __attribute__((always_inline)) float no_denorm(float x) { return x > -1e-18f && x < 1e-18f ? 0.0f : x; }

/* --- core.c: the audio block (seq, engines, fx in order) */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
/* --- seq.c */
void seq_panic(void);
void seq_block(uint32_t n);                       /* the keys, MIDI and clock for a block of n frames */
void key_event(uint32_t trk, int32_t note, uint32_t vel, uint32_t on);   /* a key, from the UI, queued for the render */
int32_t degree_note(int32_t deg, int32_t oct);    /* the scale's degree (0 up) as a MIDI note */
uint32_t track_audible(uint32_t t);               /* muted or soloed out? */
int32_t eu_hit(uint32_t j, uint32_t hits, uint32_t len);   /* a Euclidean step's hit number, or -1 */
/* --- engines.c */
void voice_start(uint32_t t, int32_t note, float vel, int32_t gate);
void voice_release_note(uint32_t t, int32_t note);
void engines_block(uint32_t n);                   /* each track's and voice's values for a block */
void engines_run(uint32_t n);                     /* the voices, both cores, into mx */
void engines_flush(void);
void engines_panic(void);
/* --- fx.c */
void fx_block(int32_t *lr, uint32_t n, uint32_t master_q12);   /* mx through delay, reverb and AGE to the output */
