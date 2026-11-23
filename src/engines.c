/* SPDX-License-Identifier: MPL-2.0 */
/* ENGINES: the sound makers. The five instruments (DRONE, PAD, BELL, PLUCK, TEXTURE), their voices, each track's
 * envelope, filter and LFOs, and the voice loop that runs on both cores. It reads the scene (sc) and writes each
 * core's mix buffers (mx); the effects (fx.c) take them from there. Built at -O2; the voice loop runs from RAM. */
#define AMB_ENGINE 1
#include "plat.h"
#include "app.h"
#include "abyssmael.h"

static float note_inc(float note, float tune) { return 440.0f * fm_exp2f((note - 69.0f + tune) * (1.0f / 12.0f)) / SR; }
static float time_s(uint8_t v) { return 0.002f * fm_exp2f(pct(v) * 13.3f); }   /* 2 ms .. 20 s */

voice_t vc[NVOICE];
static int16_t ks_buf[NVOICE][KS_LEN] PLAT_POOL;
static uint32_t voice_age;

static tblk_t tb[NTRK];

void voice_start(uint32_t t, int32_t note, float vel, int32_t gate)
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

void voice_release_note(uint32_t t, int32_t note)
{
    uint32_t i;
    for (i = t * VPT; i < (t + 1u) * VPT; i++)
        if (vc[i].on && vc[i].note == note && vc[i].gate < 0 && vc[i].stage < 4u)
            vc[i].stage = 4;
}

/* one sample of a voice's instrument, before the filter */
static inline __attribute__((always_inline)) float voice_osc(voice_t *v, const tblk_t *b, float inc, const uint32_t inst)
{
    const float *p = b->p;
    float o;
    switch (inst) {
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


mix_t mx[2];       /* each core's voices */
uint8_t amb_two_cores;

/* a voice for a block: its sample loop with the instrument a constant (the switch folds away), into the block's
 * mix buffers; the pan and the sends are the block's */
static inline __attribute__((always_inline)) void voice_run(voice_t *v, const tblk_t *b, float inc, float a1, float a2,
                                                            float a3, uint32_t n, const uint32_t inst, mix_t *mx)
{
    float pn = fm_clampf(b->pan + v->pan, -1.0f, 1.0f), gl = 0.5f - 0.5f * pn, gr = 0.5f + 0.5f * pn;
    float dsl = gl * b->dsend, dsr = gr * b->dsend, rsd = b->rsend;
    float e = v->env, s1 = v->s1, s2 = v->s2, lv = b->lvl0 * v->vel, dlv = b->dlvl * v->vel, vel_l = 0.0f;
    uint32_t i, stage = v->stage;
    (void)vel_l;
    for (i = 0; i < n; i++) {
        float o, v1, v2, v3;
        switch (stage) {
        case 1: e += b->att; if (e >= 1.0f) { e = 1.0f; stage = 2; } break;
        case 2: e = b->sus + (e - b->sus) * b->dec_k; if (e - b->sus < 0.001f) stage = 3; break;
        case 3: e = b->sus; if (e < 0.0005f) v->on = 0; break;
        default: e *= b->rel_k; if (e < 0.0002f) v->on = 0; break;
        }
        o = voice_osc(v, b, inc, inst);
        v3 = o - s2;                                  /* the filter (low-pass) */
        v1 = a1 * s1 + a2 * v3;
        v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;
        o = v2 * e * (lv + dlv * (float)i);
        mx->l[i] += o * gl;
        mx->r[i] += o * gr;
        mx->dl[i] += o * dsl;
        mx->dr[i] += o * dsr;
        mx->rs[i] += o * rsd;
        if (!v->on)
            break;
    }
    v->env = e;
    v->stage = (uint8_t)stage;
    v->s1 = s1;
    v->s2 = s2;
}

/* an effect's one-pole states decay towards zero into the denormals, which cost a lot on an FPU: once a block they
 * are set to zero when they are tiny */

static float vg[NVOICE], va1[NVOICE], va2[NVOICE], va3[NVOICE], vinc[NVOICE];   /* each voice's block settings */

/* the voices of one half (every second voice, from half) into that core's mix buffers */
static PLAT_RAM_CODE __attribute__((noinline)) void voices_range(uint32_t half, uint32_t n)
{
    uint32_t k, i;
    for (i = 0; i < n; i++)
        mx[half].l[i] = mx[half].r[i] = mx[half].dl[i] = mx[half].dr[i] = mx[half].rs[i] = 0.0f;
    for (k = half; k < NVOICE; k += 2u) {
        voice_t *v = &vc[k];
        if (!v->on)
            continue;
        switch (v->inst) {
        case I_DRONE: voice_run(v, &tb[v->trk], vinc[k], va1[k], va2[k], va3[k], n, I_DRONE, &mx[half]); break;
        case I_PAD: voice_run(v, &tb[v->trk], vinc[k], va1[k], va2[k], va3[k], n, I_PAD, &mx[half]); break;
        case I_BELL: voice_run(v, &tb[v->trk], vinc[k], va1[k], va2[k], va3[k], n, I_BELL, &mx[half]); break;
        case I_PLUCK: voice_run(v, &tb[v->trk], vinc[k], va1[k], va2[k], va3[k], n, I_PLUCK, &mx[half]); break;
        default: voice_run(v, &tb[v->trk], vinc[k], va1[k], va2[k], va3[k], n, I_TEXTURE, &mx[half]); break;
        }
    }
}
#ifdef PLAT_HOST
#define AMB_C1
#else
#define AMB_C1 __attribute__((section(".c1_text")))
#endif
static AMB_C1 void voices_job(uint32_t n) { voices_range(1, n); }   /* the second core's: the odd voices */
static void (*const voices_job_far)(uint32_t) = voices_job;

/* once a block, before the voices: each track's block values, then each sounding voice's filter, pitch and clocks */
void engines_block(uint32_t n)
{
    uint32_t t, k;
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
}

/* the voices of a block, half on each core (the even and the odd ones); one core: both halves here */
void engines_run(uint32_t n)
{
    int on2 = amb_two_cores && plat_cpu2_on() && plat_cpu2_run(voices_job_far, n);
    PLAT_FAR(voices_range)(0, n);
    if (!on2 || plat_cpu2_wait() != 0)
        PLAT_FAR(voices_range)(1, n);
}

/* the voices' one-pole states, set to zero when tiny (denormals are slow on the FPU) */
void engines_flush(void)
{
    uint32_t k;
    for (k = 0; k < NVOICE; k++) {
        vc[k].s1 = no_denorm(vc[k].s1);
        vc[k].s2 = no_denorm(vc[k].s2);
        vc[k].x1 = no_denorm(vc[k].x1);
        vc[k].x2 = no_denorm(vc[k].x2);
    }
}

/* all voices off */
void engines_panic(void)
{
    uint32_t k;
    for (k = 0; k < NVOICE; k++)
        vc[k].on = 0;
}
