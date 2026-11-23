/* SPDX-License-Identifier: MPL-2.0 */
/* AMBIENT TEST: the first test of a generative ambient firmware on fm1-base.
 *   - a drone of three slow-breathing partials (root, fifth, octave) that drifts to a new root now and then
 *   - bells: random notes from a minor pentatonic scale, FM tones with long decays
 *   - a stereo feedback delay for space
 *   - the keys play a soft pad over the top
 * K1 DARK, K2 ACTIVITY, K3 DECAY, K4 SPACE. PLAY starts and stops it, SEQ picks a new root, OCT-/OCT+ move the
 * pad, SAVE keeps the knobs and the root. Part of the unity build (device/unity.c, host/plat_host.c), after the
 * platform: gfx.c and announce.c are in scope. */
#include "fastmath.h"

#define SR 44100.0f
#define SET_MAGIC 0x414D4231u            /* "AMB1" */
enum { M_DARK, M_ACT, M_DECAY, M_SPACE, NMAC };
static const char *const MAC_NAME[NMAC] = {"DARK", "ACTIVITY", "DECAY", "SPACE"};

typedef struct {
    uint32_t magic;
    uint8_t mac[NMAC];                   /* 0..100 */
    uint8_t root;                        /* MIDI note of the drone, 36..48 */
    int8_t octave;                       /* the pad's, -2..+2 */
    uint8_t rsv[2];
} settings_t;
static settings_t set;
static uint8_t set_dirty;

/* ------------------------------------------------------------------ shared --- */
/* the main loop writes these, the audio interrupt reads them */
static volatile uint8_t playing = 1;
static volatile int32_t pad_note = -1;
static volatile int32_t root_now;        /* the drone's root: the render changes it when it drifts */
static volatile uint32_t bell_count, drift_count;
static volatile int32_t last_bell = -1;

/* ------------------------------------------------------------------ engine --- */
static uint32_t amb_seed = 0x9E3779B9u;
static uint32_t amb_rnd(void)
{
    amb_seed ^= amb_seed << 13;
    amb_seed ^= amb_seed >> 17;
    amb_seed ^= amb_seed << 5;
    return amb_seed;
}
static float amb_rndf(void) { return (float)(amb_rnd() >> 8) * (1.0f / 16777216.0f); }   /* 0..1 */

static float note_inc(float note) { return 440.0f * fm_exp2f((note - 69.0f) * (1.0f / 12.0f)) / SR; }

/* drone: three partials, two detuned oscillators each, breathing on slow LFOs */
static const float PART_OFS[3] = {0.0f, 7.0f, 12.0f};
static const float PART_LFO[3] = {0.031f, 0.047f, 0.067f};   /* Hz */
static const float PART_AMP[3] = {0.30f, 0.20f, 0.14f};
static float d_ph[3][2], d_inc[3], d_lfo[3], d_lp[2], d_fade;

/* bells: FM, ratio 3.5 (inharmonic, bell-like), the index follows the envelope */
#define NBELL 6
static struct { float ph, mph, inc, env, pan; } bell[NBELL];
static const int8_t SCALE[5] = {0, 3, 5, 7, 10};

/* pad: the keys, two detuned sines with a slow attack and a long release */
static float p_ph[2], p_inc, p_lvl;

/* delay: two lines, int16 to fit the pool */
#define DL_LEN 32768u
static int16_t dl_l[DL_LEN] PLAT_POOL, dl_r[DL_LEN] PLAT_POOL;
static uint32_t dl_w;
static float dl_lpl, dl_lpr;
static float gain_now;
static uint32_t drift_blocks;

static void bell_strike(int32_t root)
{
    uint32_t i, best = 0;
    int32_t note;
    for (i = 1; i < NBELL; i++)                  /* the quietest one */
        if (bell[i].env < bell[best].env)
            best = i;
    note = root + 24 + SCALE[amb_rnd() % 5u] + 12 * (int32_t)(amb_rnd() % 3u);
    bell[best].inc = note_inc((float)note);
    bell[best].env = 0.10f + 0.10f * amb_rndf();
    bell[best].pan = 0.15f + 0.7f * amb_rndf();
    bell[best].ph = bell[best].mph = 0.0f;
    last_bell = note;
    bell_count++;
}

static void drone_tune(int32_t root)
{
    uint32_t k;
    for (k = 0; k < 3; k++)
        d_inc[k] = note_inc((float)root + PART_OFS[k]);
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i, k;
    float dark = set.mac[M_DARK] * 0.01f, act = set.mac[M_ACT] * 0.01f;
    float decay = set.mac[M_DECAY] * 0.01f, space = set.mac[M_SPACE] * 0.01f;
    float gain = (float)master_q12 / 4096.0f * 8300000.0f;
    float bright = 1.0f - dark;
    float lp_a = 1.0f - fm_expf(-6.2831853f * (150.0f + 3800.0f * bright * bright) / SR);
    float bell_t = 0.6f + 9.0f * decay * decay;                  /* seconds to fall by 60 dB */
    float bell_k = fm_expf(-6.9f / (bell_t * SR));
    float bell_ix = 0.4f + 2.2f * bright;
    float fb = 0.30f + 0.52f * space, wet = 0.75f * space;
    float fade_to = playing ? 1.0f : 0.0f;
    int32_t pn = pad_note;
    float pad_to = pn >= 0 ? 0.22f : 0.0f;
    uint32_t dl_lt = 18081u, dl_rt = 25137u;                     /* 0.41 s and 0.57 s */

    {   /* MIDI in: notes play the pad */
        uint32_t pkt;
        int port;
        while (plat_midi_in(&pkt, &port)) {
            uint32_t st = (pkt >> 8) & 0xF0u, d1 = (pkt >> 16) & 0x7Fu, d2 = pkt >> 24;
            if (st == 0x90u && d2)
                pad_note = pn = (int32_t)d1;
            else if ((st == 0x80u || st == 0x90u) && pad_note == (int32_t)d1)
                pad_note = pn = -1;
        }
        pad_to = pn >= 0 ? 0.22f : 0.0f;
    }
    if (pn >= 0)
        p_inc = note_inc((float)pn);

    if (playing) {
        /* bells: a Poisson-ish chance each block, 0.05 to 2.5 a second */
        float rate = 0.05f + 2.45f * act * act;
        if (amb_rndf() < rate * (float)n / SR)
            bell_strike(root_now);
        /* drift: every 25 to 60 s, a chance the root moves by a fourth or a fifth */
        if (++drift_blocks > (uint32_t)(25.0f * SR / (float)n) && amb_rndf() < 0.004f) {
            static const int8_t STEP[4] = {5, -5, 7, -7};
            int32_t r = root_now + STEP[amb_rnd() % 4u];
            if (r < 36)
                r += 12;
            if (r > 48)
                r -= 12;
            root_now = r;
            drone_tune(r);
            drift_blocks = 0;
            drift_count++;
        }
    }

    for (i = 0; i < n; i++) {
        float l = 0.0f, r = 0.0f, s, dl, dr, in_l, in_r;
        uint32_t ri;
        d_fade += (fade_to - d_fade) * 0.00003f;                /* ~1.5 s fade in and out */
        gain_now += (gain - gain_now) * 0.004f;
        /* drone */
        for (k = 0; k < 3; k++) {
            float a, o;
            d_ph[k][0] += d_inc[k] * 1.0015f;
            d_ph[k][1] += d_inc[k] * 0.9985f;
            if (d_ph[k][0] >= 1.0f) d_ph[k][0] -= 1.0f;
            if (d_ph[k][1] >= 1.0f) d_ph[k][1] -= 1.0f;
            d_lfo[k] += PART_LFO[k] / SR;
            if (d_lfo[k] >= 1.0f) d_lfo[k] -= 1.0f;
            a = PART_AMP[k] * (0.55f + 0.45f * fm_sin_turns(d_lfo[k]));
            o = fm_sin_turns(d_ph[k][0]) + 0.35f * bright * fm_sin_turns(2.0f * d_ph[k][0]);
            l += a * o;
            o = fm_sin_turns(d_ph[k][1]) + 0.35f * bright * fm_sin_turns(2.0f * d_ph[k][1]);
            r += a * o;
        }
        d_lp[0] += (l - d_lp[0]) * lp_a;
        d_lp[1] += (r - d_lp[1]) * lp_a;
        l = d_lp[0] * d_fade;
        r = d_lp[1] * d_fade;
        /* bells */
        for (k = 0; k < NBELL; k++) {
            float e = bell[k].env, m;
            if (e < 1e-5f)
                continue;
            bell[k].mph += bell[k].inc * 3.5f;
            if (bell[k].mph >= 1.0f) bell[k].mph -= fm_floorf(bell[k].mph);
            bell[k].ph += bell[k].inc;
            if (bell[k].ph >= 1.0f) bell[k].ph -= 1.0f;
            m = fm_sin_turns(bell[k].mph) * bell_ix * (e * 6.0f);
            s = e * fm_sin_turns(bell[k].ph + 0.159f * m);
            l += s * (1.0f - bell[k].pan);
            r += s * bell[k].pan;
            bell[k].env = e * bell_k;
        }
        /* pad */
        p_lvl += (pad_to - p_lvl) * (pad_to > p_lvl ? 0.00008f : 0.00003f);
        if (p_lvl > 1e-5f) {
            p_ph[0] += p_inc * 1.003f;
            p_ph[1] += p_inc * 0.997f;
            if (p_ph[0] >= 1.0f) p_ph[0] -= 1.0f;
            if (p_ph[1] >= 1.0f) p_ph[1] -= 1.0f;
            l += p_lvl * fm_sin_turns(p_ph[0]);
            r += p_lvl * fm_sin_turns(p_ph[1]);
        }
        /* delay: read, filter, write back with cross-feed for width */
        ri = (dl_w - dl_lt) & (DL_LEN - 1u);
        dl = (float)dl_l[ri] * (1.0f / 16384.0f);
        ri = (dl_w - dl_rt) & (DL_LEN - 1u);
        dr = (float)dl_r[ri] * (1.0f / 16384.0f);
        dl_lpl += (dl - dl_lpl) * 0.35f;
        dl_lpr += (dr - dl_lpr) * 0.35f;
        in_l = fm_clampf(l + fb * (0.7f * dl_lpl + 0.3f * dl_lpr), -1.99f, 1.99f);
        in_r = fm_clampf(r + fb * (0.7f * dl_lpr + 0.3f * dl_lpl), -1.99f, 1.99f);
        dl_l[dl_w] = (int16_t)(in_l * 16384.0f);
        dl_r[dl_w] = (int16_t)(in_r * 16384.0f);
        dl_w = (dl_w + 1u) & (DL_LEN - 1u);
        l = fm_tanhf(1.6f * (l + wet * dl_lpl));
        r = fm_tanhf(1.6f * (r + wet * dl_lpr));
        lr[2 * i] = (int32_t)(l * gain_now);
        lr[2 * i + 1] = (int32_t)(r * gain_now);
    }
}

void app_panic(void)
{
    uint32_t k;
    pad_note = -1;
    p_lvl = 0.0f;
    d_fade = 0.0f;
    for (k = 0; k < NBELL; k++)
        bell[k].env = 0.0f;
}

/* --------------------------------------------------------------- settings --- */
static void settings_defaults(void)
{
    set.magic = SET_MAGIC;
    set.mac[M_DARK] = 45;
    set.mac[M_ACT] = 40;
    set.mac[M_DECAY] = 60;
    set.mac[M_SPACE] = 55;
    set.root = 45;                       /* A2 */
    set.octave = 0;
}

static void settings_load(void)
{
    uint32_t k, bad = 0;
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC)
        bad = 1;
    for (k = 0; k < NMAC; k++)
        bad |= set.mac[k] > 100u;
    if (bad || set.root < 36u || set.root > 48u || set.octave < -2 || set.octave > 2)
        settings_defaults();
}

void app_init(void)
{
    settings_load();
    root_now = set.root;
    drone_tune(root_now);
    amb_seed ^= plat_ms() * 2654435761u + 1u;
}

/* --------------------------------------------------------------------- UI --- */
static struct {
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;
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

static void note_name(int32_t note, char *b)
{
    static const char *const N[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    char o[12];
    b[0] = 0;
    if (note < 0)
        return;
    text_cat(b, N[note % 12]);
    int_text(note / 12 - 1, o);
    text_cat(b, o);
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

static void macro_turn(uint32_t m, int32_t e)
{
    char t[12];
    int32_t v = clamp((int32_t)set.mac[m] + 2 * e, 0, 100);
    set.mac[m] = (uint8_t)v;
    set_dirty = 1;
    int_text(v, t);
    app_say(MAC_NAME[m], t);
}

static void input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), k;
    int32_t e;
    for (k = 0; k < NKEYS; k++) {            /* key 0 is F3 (MIDI 53) */
        uint32_t bit = 1u << k;
        int32_t note = 53 + (int32_t)k + 12 * set.octave;
        if ((keys & bit) && !(ui.keys & bit)) {
            pad_note = note;
            plat_midi_send(0x09u | 0x90u << 8 | (uint32_t)note << 16 | 100u << 24, TO_USB);
        } else if (!(keys & bit) && (ui.keys & bit)) {
            if (pad_note == note)
                pad_note = -1;
            plat_midi_send(0x08u | 0x80u << 8 | (uint32_t)note << 16, TO_USB);
        }
    }
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        if (k == B_PLAY) {
            playing = !playing;
            app_say(playing ? "PLAYING" : "STOPPED", "");
        } else if (k == B_SEQ) {
            int32_t r = 36 + (int32_t)(amb_rnd() % 13u);
            char t[8];
            root_now = r;
            set.root = (uint8_t)r;
            set_dirty = 1;
            drone_tune(r);
            note_name(r, t);
            app_say("NEW ROOT", t);
        } else if (k == B_OCTDN || k == B_OCTUP) {
            int32_t o = clamp(set.octave + (k == B_OCTUP ? 1 : -1), -2, 2);
            char t[8];
            pad_note = -1;
            set.octave = (int8_t)o;
            set_dirty = 1;
            int_text(o, t);
            app_say("PAD OCTAVE", t);
        } else if (k == B_SAVE) {
            set.root = (uint8_t)root_now;
            if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) {
                set_dirty = 0;
                app_say("SAVED", "");
            } else {
                app_say("SAVE", "FAILED");
            }
        }
    }
    for (k = 0; k < NMAC; k++)
        if ((e = plat_enc((int)(EN_K1 + k))) != 0)
            macro_turn(k, e);
    for (k = EN_SELECT; k <= EN_PRESET; k++)
        (void)plat_enc((int)k);
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
    plat_glow(1);
    plat_leds(btn | (set_dirty ? 1u << B_SAVE : 0u) | (playing ? 1u << B_PLAY : 0u), keys);
}

void app_input_only(void) { input(); }

static void draw(void)
{
    char t[24];
    uint32_t k, now = plat_ms();
    cv_begin(240, 40, C_BLACK);
    if (now < ui.msg_until) {
        int32_t x = cv_text(10, 10, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 10, &FONT_B, ui.msg[1], C_HI);
    } else {
        cv_text(10, 10, &FONT_B, APP_NAME, C_WHITE);
        cv_text(170, 10, &FONT_B, playing ? "PLAY" : "STOP", playing ? C_HI : C_DIM);
    }
    cv_blit(0, 0);
    /* the four macros: name, value and a bar */
    cv_begin(240, 136, C_BLACK);
    for (k = 0; k < NMAC; k++) {
        int32_t y = (int32_t)k * 34, w = (int32_t)set.mac[k] * 2;
        cv_text(10, y, &FONT_S, MAC_NAME[k], C_GRAY);
        int_text(set.mac[k], t);
        cv_text(200, y, &FONT_S, t, C_HI);
        cv_rect(10, y + 20, 200, 6, C_LINE);
        cv_rect(10, y + 20, w, 6, C_HI);
    }
    cv_blit(0, 44);
    /* the root, the last bell and the load */
    cv_begin(240, 56, C_BLACK);
    cv_text(10, 4, &FONT_XS, "ROOT", C_GRAY);
    note_name(root_now, t);
    cv_text(50, 4, &FONT_XS, t, C_HI);
    cv_text(100, 4, &FONT_XS, "BELL", C_GRAY);
    note_name(last_bell, t);
    cv_text(140, 4, &FONT_XS, t, C_AMB);
    cv_text(10, 22, &FONT_XS, "PAD", C_GRAY);
    note_name(pad_note, t);
    cv_text(50, 22, &FONT_XS, t[0] ? t : "-", C_HI);
    cv_text(100, 22, &FONT_XS, "CPU", C_GRAY);
    int_text((int32_t)plat_cpu_pct(), t);
    text_cat(t, "%");
    cv_text(140, 22, &FONT_XS, t, C_HI);
    cv_text(10, 40, &FONT_XS, "PLAY  SEQ NEW ROOT  K1-K4", C_DIM);
    cv_blit(0, 184);
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = 1;
}

void app_frame(void)
{
    static uint32_t last_bells, last_root, last_cpu_t;
    uint32_t now = plat_ms();
    input();
    ann_tick(now);
    if (bell_count != last_bells || (uint32_t)root_now != last_root || now - last_cpu_t > 1000u ||
        (ui.msg_until && now >= ui.msg_until)) {
        last_bells = bell_count;
        last_root = (uint32_t)root_now;
        last_cpu_t = now;
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
    if (!strcmp(what, "playing"))
        *got = playing;
    else if (!strcmp(what, "bells"))
        *got = (int)bell_count;
    else if (!strcmp(what, "root"))
        *got = root_now;
    else if (!strcmp(what, "pad"))
        *got = pad_note;
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else
        return 0;
    return 1;
}
#endif
