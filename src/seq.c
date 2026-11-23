/* SPDX-License-Identifier: MPL-2.0 */
/* SEQ: what plays when. The sixteenth-note clock, the three sequencers (generator, Euclidean, step), the arpeggio,
 * the key drift, and the key and MIDI input queue. It starts notes through the engines (voice_start, voice_release_note)
 * and reads the scene (sc); it makes no sound itself. Built at -O2. */
#include "plat.h"
#include "app.h"
#include "abyssmael.h"

/* a key's note for the render: the main loop queues them, the render plays them */
static kev_t kq[32];
static volatile uint32_t kq_w, kq_r;
void key_event(uint32_t trk, int32_t note, uint32_t vel, uint32_t on)
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


static float clk_left;                   /* samples to the next sixteenth */
static uint32_t clk_tick;
motif_t mo[NTRK];
static uint8_t held[NTRK][8], hlife[NTRK][8], held_n[NTRK], arp_i[NTRK];   /* hlife: ticks left, 0 = until the key is let go */
static int8_t arp_dir[NTRK] = {1, 1, 1, 1, 1, 1};

int32_t degree_note(int32_t deg, int32_t oct)
{
    uint32_t sci = sc.gp[GP_SCALE], len = SCALE_LEN[sci];
    int32_t o = deg >= 0 ? deg / (int32_t)len : -((-deg + (int32_t)len - 1) / (int32_t)len);
    int32_t d = deg - o * (int32_t)len;
    return 12 * (oct + o + 1) + sc.gp[GP_KEY] + SCALE_DEG[sci][d];
}

uint32_t track_audible(uint32_t t)
{
    uint32_t i, solo = 0;
    for (i = 0; i < NTRK; i++)
        solo |= sc.tp[i][TP_SOLO];
    return solo ? sc.tp[t][TP_SOLO] : !sc.tp[t][TP_MUTE];
}

/* EUCLID and STEP: where each track is in its pattern (the step played last, for the screen) */
uint8_t seq_pos[NTRK], seq_cur[NTRK];
static int8_t seq_dir[NTRK] = {1, 1, 1, 1, 1, 1};
uint8_t eu_hits_now[NTRK], eu_rot_now[NTRK];
static uint32_t seq_n[NTRK];             /* steps run since play started: pass = seq_n / length */
static int8_t drift_h[NTRK], drift_r[NTRK];   /* EUCL DRIFT: how far the hits and rotation have wandered */
static uint8_t prev_ok[NTRK];            /* the last tested hit played (for PRE) */
static uint32_t last_play[NTRK];         /* the clock tick of a track's last note, plus one */
static uint32_t was_playing;

/* a Euclidean pattern: step j (0..len-1) is a hit when (j * hits) mod len < hits; its hit number (j * hits) / len */
int32_t eu_hit(uint32_t j, uint32_t hits, uint32_t len)
{
    if (hits > len)
        hits = len;
    return (j * hits) % len < hits ? (int32_t)(j * hits / len) : -1;
}

/* does a hit's condition let it play on this pass? */
static uint32_t cond_ok(uint32_t t, uint32_t c, uint32_t pass)
{
    uint32_t nei = (t + NTRK - 1u) % NTRK;
    if (c >= C_1_2 && c <= C_4_4) {
        static const uint8_t AB[10][2] = {{0, 1}, {1, 2}, {2, 2}, {1, 3}, {2, 3}, {3, 3}, {1, 4}, {2, 4}, {3, 4}, {4, 4}};
        return pass % AB[c][1] == (uint32_t)AB[c][0] - 1u;
    }
    switch (c) {
    case C_FIRST: return pass == 0;
    case C_NFIRST: return pass != 0;
    case C_PRE: return prev_ok[t];
    case C_NPRE: return !prev_ok[t];
    case C_NEI: return last_play[nei] == clk_tick + (nei > t ? 0u : 1u);
    case C_NNEI: return last_play[nei] != clk_tick + (nei > t ? 0u : 1u);
    default: return 1;
    }
}

/* a sequenced note: it sounds, or with the arpeggio fed from the sequencer it goes into the arpeggio's list for its gate */
static void held_add(uint32_t t, uint32_t note, uint32_t life);
static void seq_note(uint32_t t, int32_t note, float vel, float gate_s, float tick_s)
{
    const uint8_t *p = sc.tp[t];
    last_play[t] = clk_tick + 1u;
    if (p[TP_ARPON] && p[TP_ARPSRC] != 0u) {
        uint32_t life = (uint32_t)(gate_s / tick_s + 0.999f);
        held_add(t, (uint32_t)note, life < 1u ? 1u : life > 255u ? 255u : life);
        return;
    }
    voice_start(t, note, vel, (int32_t)(gate_s * SR));
}

/* EUCL DRIFT, once a pass: the hits or the rotation wander by one, and now and then step back toward the set values */
static void eu_drift(uint32_t t, const uint8_t *p)
{
    if (am_rndf() >= pct(p[TP_EDRIFT]))
        return;
    if (am_rnd() & 1u) {
        int8_t d = drift_h[t];
        d = (int8_t)(d && (am_rnd() & 1u) ? (d > 0 ? d - 1 : d + 1) : d + (am_rnd() & 1u ? 1 : -1));
        drift_h[t] = d > 2 ? 2 : d < -2 ? -2 : d;
    } else {
        int8_t d = drift_r[t];
        d = (int8_t)(d && (am_rnd() & 1u) ? (d > 0 ? d - 1 : d + 1) : d + (am_rnd() & 1u ? 1 : -1));
        drift_r[t] = d > 3 ? 3 : d < -3 ? -3 : d;
    }
}

static void pat_step(uint32_t t, float tick_s, uint32_t rt, int32_t oct)
{
    const uint8_t *p = sc.tp[t];
    int32_t deg = -1;
    uint32_t len, k;
    float chance;
    if (p[TP_SMODE] == 1u) {                 /* EUCLID */
        int32_t h, hits, rot, pos;
        len = p[TP_ELEN];
        k = seq_pos[t] % len;
        switch (p[TP_EDIR]) {
        case 0: pos = (int32_t)k; seq_pos[t] = (uint8_t)((k + 1u) % len); break;
        case 1: pos = (int32_t)(len - 1u - k); seq_pos[t] = (uint8_t)((k + 1u) % len); break;
        case 2:
            if (len > 1u && ((int32_t)k + seq_dir[t] < 0 || k + (uint32_t)seq_dir[t] >= len))
                seq_dir[t] = (int8_t)-seq_dir[t];
            pos = (int32_t)k;
            seq_pos[t] = (uint8_t)(len > 1u ? (int32_t)k + seq_dir[t] : 0);
            break;
        default: pos = (int32_t)(am_rnd() % len); seq_pos[t] = (uint8_t)((k + 1u) % len); break;
        }
        if (seq_n[t] % len == 0u && seq_n[t])
            eu_drift(t, p);
        hits = (int32_t)p[TP_EHITS] + drift_h[t];
        hits = hits < 0 ? 0 : hits > (int32_t)len ? (int32_t)len : hits;
        rot = (int32_t)(p[TP_EROT] % len) + drift_r[t];
        rot = ((rot % (int32_t)len) + (int32_t)len) % (int32_t)len;
        eu_hits_now[t] = (uint8_t)hits;
        eu_rot_now[t] = (uint8_t)rot;
        seq_cur[t] = (uint8_t)pos;
        h = eu_hit((uint32_t)(pos + (int32_t)len - rot) % len, (uint32_t)hits, len);
        if (h >= 0) {
            if (cond_ok(t, sc.cd[t][h], seq_n[t] / len)) {
                if (sc.hn[t][h] != REST)
                    deg = sc.hn[t][h];
                prev_ok[t] = 1;
            } else {
                prev_ok[t] = 0;
            }
        }
        seq_n[t]++;
        chance = pct(p[TP_ECHANCE]);
    } else {                                 /* STEP */
        len = p[TP_SLEN];
        k = seq_pos[t] % len;
        seq_cur[t] = (uint8_t)k;
        switch (p[TP_SDIR]) {
        case 0: seq_pos[t] = (uint8_t)((k + 1u) % len); break;
        case 1: seq_cur[t] = (uint8_t)(len - 1u - k); seq_pos[t] = (uint8_t)((k + 1u) % len); break;
        case 2:
            if (len > 1u && ((int32_t)k + seq_dir[t] < 0 || k + (uint32_t)seq_dir[t] >= len))
                seq_dir[t] = (int8_t)-seq_dir[t];
            seq_pos[t] = (uint8_t)(len > 1u ? (int32_t)k + seq_dir[t] : 0);
            break;
        default: seq_cur[t] = (uint8_t)(am_rnd() % len); seq_pos[t] = seq_cur[t]; break;
        }
        k = seq_cur[t];
        if (sc.son[t] >> k & 1u) {
            if (cond_ok(t, sc.cd[t][k], seq_n[t] / len)) {
                deg = sc.sn[t][k];
                prev_ok[t] = 1;
            } else {
                prev_ok[t] = 0;
            }
        }
        seq_n[t]++;
        if (deg >= 0) {
            if (am_rndf() < pct(p[TP_SMUT]) * 0.5f)        /* MUTATE: now and then another note of the scale */
                deg = (int32_t)(am_rnd() % 8u);
        }
        chance = pct(p[TP_SCHANCE]);
    }
    if (deg < 0 || !playing || am_rndf() >= chance * (0.6f + 0.8f * pct(sc.gp[GP_ACT])))
        return;
    {
        float gate = tick_s * (float)rt * (0.05f + 3.95f * pct(p[TP_GATE]) * pct(p[TP_GATE]));
        float vel = 1.0f - pct(p[TP_ACCENT]) * am_rndf() * 0.7f;
        seq_note(t, degree_note(deg, oct), vel, gate, tick_s);
    }
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
    if (p[TP_SMODE]) {
        pat_step(t, tick_s, rt, oct);
        return;
    }
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
        seq_note(t, degree_note(deg, oct), vel, gate, tick_s);
    }
}

static void arp_play(uint32_t t, float tick_s, uint32_t rt, uint32_t n);
static void held_remove(uint32_t t, uint32_t note);
static void arp_step(uint32_t t, float tick_s)
{
    static const uint8_t AR[4] = {1, 2, 4, 8};
    uint8_t *p = sc.tp[t];
    uint32_t n = held_n[t], rt = AR[p[TP_ARPRATE]], i;
    if (!p[TP_ARPON]) {                              /* arpeggio off: forget the sequenced notes */
        held_n[t] = 0;
        return;
    }
    if (n && !(clk_tick % rt))
        arp_play(t, tick_s, rt, n);
    for (i = 0; i < held_n[t];)                      /* sequenced notes run out after their gate */
        if (hlife[t][i] && !--hlife[t][i]) {
            held_remove(t, held[t][i]);
        } else {
            i++;
        }
}
static void arp_play(uint32_t t, float tick_s, uint32_t rt, uint32_t n)
{
    uint8_t *p = sc.tp[t];
    uint32_t k;
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

static void held_add(uint32_t t, uint32_t note, uint32_t life)
{
    uint32_t i, j;
    for (i = 0; i < held_n[t]; i++)
        if (held[t][i] == note) {
            if (hlife[t][i] && life > hlife[t][i])    /* a sequenced note again: keep it longer; a held key stays held */
                hlife[t][i] = (uint8_t)life;
            else if (!life)
                hlife[t][i] = 0;
            return;
        }
    if (held_n[t] >= 8u)
        return;
    for (i = 0; i < held_n[t] && held[t][i] < note; i++)
        ;
    for (j = held_n[t]; j > i; j--)
        held[t][j] = held[t][j - 1], hlife[t][j] = hlife[t][j - 1];
    held[t][i] = (uint8_t)note;
    hlife[t][i] = (uint8_t)life;
    held_n[t]++;
}
static void held_remove(uint32_t t, uint32_t note)
{
    uint32_t i, j;
    for (i = 0; i < held_n[t]; i++)
        if (held[t][i] == note) {
            for (j = i; j + 1u < held_n[t]; j++)
                held[t][j] = held[t][j + 1], hlife[t][j] = hlife[t][j + 1];
            held_n[t]--;
            return;
        }
}

static void key_play(const kev_t *e)
{
    uint32_t t = e->trk;
    if (sc.tp[t][TP_ARPON] && sc.tp[t][TP_ARPSRC] != 1u) {   /* with the arpeggio, keys only add notes to its list */
        if (e->on)
            held_add(t, e->note, 0u);
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


/* once a block, before the voices: the queued keys and the MIDI in, then the clock's steps that fall in this block */
void seq_block(uint32_t n)
{
    uint32_t t;
    float tick_s = 15.0f / (float)sc.gp[GP_TEMPO];
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
    if (playing && !was_playing) {                 /* play starts: every pattern from its first step, FIRST passes again */
        for (t = 0; t < NTRK; t++) {
            seq_pos[t] = 0;
            seq_n[t] = 0;
            drift_h[t] = drift_r[t] = 0;
            prev_ok[t] = 1;
            seq_dir[t] = 1;
        }
    }
    was_playing = playing;
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
}

/* all notes off: the arpeggio's held keys forgotten */
void seq_panic(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        held_n[k] = 0;
}
