/* SPDX-License-Identifier: MPL-2.0 */
/* FX: everything after the voices. A stereo delay, the shimmer reverb (all-passes, a four-line feedback network and an
 * octave-up pitch shifter in its loop), the AGE layer (wow and flutter, tape dropouts, hiss, dusty crackle, rumble and
 * the record's turn, a worn top end), the soft clip and the master gain. It takes the engines' mix buffers (mx) and
 * writes the audio block. Built at -O2, run from RAM.
 *
 * Cost: the per-sample work is mostly instruction count, so (1) every delay-line read is fixed point (a Q16 position,
 * an integer interpolation, one int-to-float), (2) wow is skipped while it is 0, the hiss and the dust while they are
 * 0, and (3) once nothing has come in and nothing has come out of the delay or the reverb for about 0.8 s, the delay,
 * reverb and wow are skipped altogether (idle) until sound arrives again. */
#define AMB_ENGINE 1
#include "plat.h"
#include "app.h"
#include "abyssmael.h"

#define DL_LEN 16384u                    /* the delay, at half rate: up to 0.74 s */
static int16_t dl_l[DL_LEN] PLAT_POOL, dl_r[DL_LEN] PLAT_POOL;
static uint32_t dl_w;
static float dl_t, dl_lpl, dl_lpr, dl_hl, dl_hr;   /* dl_t: the time in ticks (22.05 kHz) */

#define RV_LEN 1024u                     /* the reverb: four lines and a pitch shifter for the shimmer, at quarter rate */
static const float RV_BASE[4] = {1557.0f, 1617.0f, 1491.0f, 1422.0f};
static int16_t rv_line[4][RV_LEN] PLAT_POOL;
static float rv_lp[4];
static uint32_t rv_w;
#define AP_LEN 256u
static int16_t rv_ap[2][AP_LEN] PLAT_POOL;
static const uint32_t AP_D[2] = {87u, 221u};   /* in reverb ticks (11.025 kHz) */
#define PS_LEN 1024u
static int16_t ps_buf[PS_LEN] PLAT_POOL;
static uint32_t ps_w, ps_phq;            /* ps_phq: the shimmer's window phase, 2^32 = one window of 512 ticks */
/* the reverb's quarter-rate input (low-passed, summed) and output (each group of four samples runs from the last
 * tick to the new one, then two one-poles) */
static float rs_flt, rs_acc, gl0, gl1, gr0, gr1, olp[4];
static const float T4[4] = {0.25f, 0.5f, 0.75f, 1.0f};
static float rv_mod, ps_hp, rv_dc[4];

#define WOW_LEN 2048u                    /* the master's wow and flutter */
static int16_t wow_l[WOW_LEN] PLAT_POOL, wow_r[WOW_LEN] PLAT_POOL;
static uint32_t wow_w, wow_run;
static float wow_ph, flut_ph, wow_rnd, age_lpl, age_lpr, hiss_lvl, crk, crk_k, crk_pan;
static float gain_now;
/* the AGE layer's own state: dust density and its countdown, the right channel's hiss, rumble, the record's turn,
 * tape dropouts */
static float dust_dens = 1.0f, crk_cnt = 1000.0f, rumble_b, rev_ph, thump, drop_g = 1.0f, drop_t = 1.0f;
static uint32_t drop_blk;
static int32_t rd_q[4], rd_stepq[4], wow_dq = 8 << 16, wow_stepq;   /* delays in samples, Q16 */
/* the hiss is read from two tables of pink noise (made a slice at a time after power-on); each channel sums one read
 * from each table, the tables have different lengths and the four readers start apart, so nothing repeats as a pair */
#define NZ_A 20011u
#define NZ_B 16381u
static int16_t nz_a[NZ_A] PLAT_POOL, nz_b[NZ_B] PLAT_POOL;
static uint32_t nz_n, na = 0u, nb = 0u, nc = 7777u, nd = 5555u;   /* nz_n: samples made so far; the readers */
static float nz_f[3];
static uint32_t fx_quiet;                /* blocks in a row with nothing coming in or going round */
#define FX_IDLE_BLOCKS 280u              /* about 0.8 s: longer than the delay */
/* "quiet" is a summed absolute level of 1.5e-3 (about -56 dB): the int16 lines keep a rounding floor of 5e-4 to 8e-4 */
#define FX_QUIET_LVL 1.5e-3f

/* a float to the int16 lines: clamped, and rounded (a plain cast truncates toward zero and kills the quiet tails) */
static inline __attribute__((always_inline)) int16_t qw(float x)
{
    float v = fm_clampf(x, -3.9f, 3.9f) * 8192.0f;
    return (int16_t)(v + (v < 0.0f ? -0.5f : 0.5f));
}

/* a soft ceiling for the reverb loop: linear up to 2, then bending toward 4, so a hot shimmer does not clip hard */
static inline __attribute__((always_inline)) float lim4(float x)
{
    float a = x < 0.0f ? -x : x, u, y;
    if (a <= 2.0f)
        return x;
    u = a - 2.0f;
    y = 2.0f + u / (1.0f + 0.5f * u);
    return x < 0.0f ? -y : y;
}

/* a line read at a Q16 position (the whole part picks the sample, 15 bits of the fraction weigh the next one); the
 * interpolation is integer, then one conversion to float. mask is the line's length minus 1 (a power of two up to
 * 65536, so the position can wrap as a plain unsigned subtraction) */
static inline __attribute__((always_inline)) float ip(const int16_t *b, uint32_t pos, uint32_t mask)
{
    uint32_t i = pos >> 16;
    int32_t f = (int32_t)((pos >> 1) & 0x7FFFu), a = b[i & mask], c = b[(i + 1u) & mask];
    return (float)(a * 32768 + (c - a) * f) * (1.0f / 268435456.0f);   /* 8192 (the line's scale) * 32768 */
}

/* tanh, as a rational (Pade) fit that is exact to the third order and bounded: |error| < 0.02 over |x| < 3 */
static inline __attribute__((always_inline)) float soft_clip(float x)
{
    float a = x < 0.0f ? -x : x, x2;
    if (a > 3.0f)
        return x < 0.0f ? -1.0f : 1.0f;
    x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

static inline __attribute__((always_inline)) float fabs_(float x) { return x < 0.0f ? -x : x; }

/* the delay time the scene asks for, in seconds: TIME, or with SYNC a note length at the scene's tempo, halved until it
 * fits the line (and doubled if it is too short) */
static inline __attribute__((always_inline)) float dl_seconds(void)
{
    uint32_t sy = sc.gp[GP_DSYNC], k;
    float t;
    if (sy == 0u || sy >= 9u)
        return 0.02f + 0.72f * pct(sc.gp[GP_DTIME]);
    t = DSYNC_BEATS[sy] * 60.0f / (float)sc.gp[GP_TEMPO];
    for (k = 0; k < 6u && t > 0.72f; k++)
        t *= 0.5f;
    for (k = 0; k < 6u && t < 0.02f; k++)
        t *= 2.0f;
    return t;
}


static PLAT_RAM_CODE void fx_audio(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i, k;
    float gain = (float)master_q12 / 4096.0f * 8300000.0f;
    /* delay, reverb and age settings, once a block */
    float dl_target = dl_seconds() * SR, dr_ratio = sc.gp[GP_DSYNC] ? 1.0f : 1.13f, dfb = pct(sc.gp[GP_DFB]) * 0.92f;
    float dtone = 0.08f + 0.85f * pct(sc.gp[GP_DTONE]), dtone2, dmix = pct(sc.gp[GP_DMIX]);
    float rsize = pct(sc.gp[GP_RSIZE]), rg = 0.55f + 0.44f * rsize, rdamp = 0.9f - 0.8f * pct(sc.gp[GP_RDAMP]), rdamp4;
    float rshim = pct(sc.gp[GP_RSHIM]), rmix = pct(sc.gp[GP_RMIX]);
    float rlen[4];
    float hiss = pct(sc.gp[GP_HISS]), crack = pct(sc.gp[GP_CRACKLE]), wow = pct(sc.gp[GP_WOW]), age = pct(sc.gp[GP_AGE]);
    float age_a = 1.0f - fm_expf(-6.2831853f * (18000.0f * fm_exp2f(-age * 3.0f)) / SR);
    float hiss_g = hiss * hiss * 0.25f, hiss_k, rumble_g = crack * crack * 1.2f, crk_gap, pk = 0.0f;
    float inv_n = 1.0f / (float)n;
    uint32_t crk_on = crack >= 0.02f, hiss_on = hiss > 0.0f && nz_n >= NZ_A + NZ_B, idle = fx_quiet >= FX_IDLE_BLOCKS;
    uint32_t wow_on = wow > 0.0f && !idle;
    dtone2 = dtone * (2.0f - dtone);          /* the delay's tone at half rate */
    dl_target *= 0.5f;                        /* in ticks */
    {                                         /* the same damping at quarter rate: four one-poles in one */
        float u = (1.0f - rdamp) * (1.0f - rdamp);
        rdamp4 = 1.0f - u * u;
    }
    for (k = 0; k < 4u; k++)
        rlen[k] = RV_BASE[k] * 0.25f * (0.45f + 2.0f * rsize);   /* in ticks */
    /* the effects' slow modulation, once a block (and moved smoothly across it) */
    {
        float m0;
        int32_t diff;
        rv_mod += 0.6f / SR * (float)n;
        if (rv_mod >= 1.0f)
            rv_mod -= 1.0f;
        for (k = 0; k < 4u; k++) {
            float d = (k & 1u ? 6.0f : -6.0f) * fm_sin_turns(rv_mod + 0.25f * (float)k);
            rd_stepq[k] = (int32_t)((float)((int32_t)((rlen[k] + 0.25f * d) * 65536.0f) - rd_q[k]) * (4.0f * inv_n));   /* per tick: n / 4 ticks in a block */
        }
        wow_ph += 0.45f / SR * (float)n;
        if (wow_ph >= 1.0f) {
            wow_ph -= 1.0f;
            wow_rnd = am_rndf();
        }
        flut_ph += 6.3f / SR * (float)n;
        if (flut_ph >= 1.0f)
            flut_ph -= 1.0f;
        m0 = 8.0f + wow * (60.0f * (0.5f + 0.5f * fm_sin_turns(wow_ph)) * (0.6f + 0.4f * wow_rnd)
                           + 6.0f * (0.5f + 0.5f * fm_sin_turns(flut_ph)));
        if (wow_on && !wow_run) {                 /* wow comes on: start from an empty line and the rest position */
            for (k = 0; k < WOW_LEN; k++) {
                wow_l[k] = 0;
                wow_r[k] = 0;
            }
            wow_dq = 8 << 16;
        }
        wow_run = wow_on;
        diff = (int32_t)(m0 * 65536.0f) - wow_dq;
        if (diff < (int32_t)n && diff > -(int32_t)n) {   /* the step is a truncated division: land the last bit */
            wow_dq += diff;
            wow_stepq = 0;
        } else {
            wow_stepq = (int32_t)((float)diff * inv_n);
        }
    }
    /* the hiss tables: 512 samples a block until both are made (the hiss is silent till then) */
    if (nz_n < NZ_A + NZ_B) {
        for (k = 0; k < 512u && nz_n < NZ_A + NZ_B; k++, nz_n++) {
            float pink = am_rnds(), v;
            nz_f[0] = 0.997f * nz_f[0] + 0.029591f * pink;
            nz_f[1] = 0.985f * nz_f[1] + 0.032534f * pink;
            nz_f[2] = 0.950f * nz_f[2] + 0.048056f * pink;
            v = fm_clampf(nz_f[0] + nz_f[1] + nz_f[2] + pink * 0.05f, -1.9f, 1.9f) * 16384.0f;
            if (nz_n < NZ_A)
                nz_a[nz_n] = (int16_t)v;
            else
                nz_b[nz_n - NZ_A] = (int16_t)v;
        }
    }
    hiss_lvl += ((0.6f + 0.4f * wow_rnd) - hiss_lvl) * (0.00001f * (float)n);   /* the level wanders, once a block */
    hiss_k = hiss_g * hiss_lvl * (0.7071f / 16384.0f);
    /* AGE, once a block: the dust's density wanders, the record turns (33 rpm), the tape drops out now and then */
    {
        dust_dens += (1.0f - dust_dens) * 0.002f + (am_rndf() - 0.5f) * 0.03f;
        dust_dens = fm_clampf(dust_dens, 0.3f, 1.8f);
        crk_gap = SR / (53.0f * crack * crack * dust_dens + 0.01f);   /* the mean gap between clicks, in samples */
        if (crk_cnt > 3.0f * crk_gap)
            crk_cnt = am_rndf() * crk_gap;
        rev_ph += 0.5556f / SR * (float)n;
        if (rev_ph >= 1.0f) {
            rev_ph -= 1.0f;
            if (crk_on)
                thump = (0.5f + 0.5f * am_rndf()) * 0.15f * crack * crack;
        }
        if (drop_blk) {
            drop_blk--;
            if (!drop_blk)
                drop_t = 1.0f;
        } else if (wow > 0.5f && am_rndf() < (wow - 0.5f) * (wow - 0.5f) * 0.0008f) {
            drop_blk = 4u + (uint32_t)(am_rndf() * 16.0f);
            drop_t = 0.35f + 0.3f * am_rndf();
        }
    }
    for (i = 0; i < n; i++) {
        float l = mx[0].l[i] + mx[1].l[i], r = mx[0].r[i] + mx[1].r[i], ds_l = mx[0].dl[i] + mx[1].dl[i];
        float ds_r = mx[0].dr[i] + mx[1].dr[i], rs = mx[0].rs[i] + mx[1].rs[i];
        float s;
        gain_now += (gain - gain_now) * 0.004f;
        if ((i & 7u) == 0u) {                 /* a look at the input every 8th sample is enough to wake from idle */
            s = fabs_(l) + fabs_(r) + fabs_(ds_l) + fabs_(ds_r) + fabs_(rs);
            if (s > pk)
                pk = s;
        }
        if (!idle) {
            float rin, rout_l, rout_r, dyl = 0.0f, dyr = 0.0f;
#ifndef FX_OFF_DELAY
            /* the delay runs at half rate: a tick every second sample, fed the mean of the pair, its output linearly
             * interpolated back up. Smoothed time, filtered feedback, a little cross-feed */
            if (i & 1u) {
                dl_hl = ds_l;
                dl_hr = ds_r;
                dyl = dl_lpl;
                dyr = dl_lpr;
            } else {
                float dl, dr, tr, pl = dl_lpl, pr = dl_lpr, ml = (dl_hl + ds_l) * 0.5f, mr = (dl_hr + ds_r) * 0.5f;
                uint32_t pw = dl_w << 16;
                dl_t += (dl_target - dl_t) * 0.0001f;
                tr = dl_t * dr_ratio;
                if (tr > (float)(DL_LEN - 4u))
                    tr = (float)(DL_LEN - 4u);
                dl = ip(dl_l, pw - (uint32_t)(int32_t)(dl_t * 65536.0f), DL_LEN - 1u);
                dr = ip(dl_r, pw - (uint32_t)(int32_t)(tr * 65536.0f), DL_LEN - 1u);
                dl_lpl += (dl - dl_lpl) * dtone2;
                dl_lpr += (dr - dl_lpr) * dtone2;
                dl_l[dl_w] = qw(ml + dfb * (0.8f * dl_lpl + 0.2f * dl_lpr));
                dl_r[dl_w] = qw(mr + dfb * (0.8f * dl_lpr + 0.2f * dl_lpl));
                dl_w = (dl_w + 1u) & (DL_LEN - 1u);
                dyl = 0.5f * (pl + dl_lpl);
                dyr = 0.5f * (pr + dl_lpr);
                s = fabs_(dl_lpl) + fabs_(dl_lpr);
                if (s > pk)
                    pk = s;
            }
            l += dyl * dmix;
            r += dyr * dmix;
            rs += (dyl + dyr) * 0.25f * dmix;
#endif
#ifndef FX_OFF_REVERB
            /* the reverb runs at quarter rate (11.025 kHz): the input is low-passed and summed over four samples, a
             * tick is done on the fourth, and the output is linearly interpolated over the next four and low-passed
             * again (the reverb is dark anyway). Two all-passes into four lines, mixed (Hadamard), damped; the
             * shimmer is an octave up pitch shifter (two crossfaded taps) fed back into the lines */
            {
                float t = T4[i & 3u];
                rs_flt += (rs - rs_flt) * 0.5f;
                rs_acc += rs_flt;
                olp[0] += ((gl0 + (gl1 - gl0) * t) - olp[0]) * 0.55f;
                olp[1] += (olp[0] - olp[1]) * 0.55f;
                olp[2] += ((gr0 + (gr1 - gr0) * t) - olp[2]) * 0.55f;
                olp[3] += (olp[2] - olp[3]) * 0.55f;
                rout_l = olp[1];
                rout_r = olp[3];
            }
            if ((i & 3u) == 3u) {
                float a[4], h[4], shim;
                uint32_t rw = rv_w << 16;
                rin = rs_acc * 0.125f;
                rs_acc = 0.0f;
                for (k = 0; k < 2u; k++) {
                    uint32_t ri = (rv_w - AP_D[k]) & (AP_LEN - 1u);
                    float dly = rv_ap[k][ri] * (1.0f / 8192.0f), y = dly - 0.6f * rin;
                    rv_ap[k][rv_w & (AP_LEN - 1u)] = qw(rin + 0.6f * y);
                    rin = y;
                }
                for (k = 0; k < 4u; k++) {
                    rd_q[k] += rd_stepq[k];
                    a[k] = ip(rv_line[k], rw - (uint32_t)rd_q[k], RV_LEN - 1u);
                    rv_lp[k] += (a[k] - rv_lp[k]) * rdamp4;
                    rv_dc[k] += (rv_lp[k] - rv_dc[k]) * 0.0028f;   /* a DC blocker in each line (about 5 Hz) */
                    a[k] = rv_lp[k] - rv_dc[k];
                }
                h[0] = (a[0] + a[1] + a[2] + a[3]) * 0.5f;
                h[1] = (a[0] - a[1] + a[2] - a[3]) * 0.5f;
                h[2] = (a[0] + a[1] - a[2] - a[3]) * 0.5f;
                h[3] = (a[0] - a[1] - a[2] + a[3]) * 0.5f;
                gl0 = gl1;
                gr0 = gr1;
                gl1 = a[0] + a[2];
                gr1 = a[1] + a[3];
                s = fabs_(gl1) + fabs_(gr1);
                if (s > pk)
                    pk = s;
#ifdef FX_OFF_SHIM
                shim = 0.0f;
#else
                /* shimmer: write the tail, read it back at double speed through two taps 180 degrees apart. The
                 * window phase is an integer (2^32 = 512 ticks), so the tap delays are integer Q16 as well */
                ps_buf[ps_w] = qw((gl1 + gr1) * 0.5f);
                {
                    uint32_t p = ps_phq, p2 = ps_phq + 0x80000000u, pp = ps_w << 16, tri;
                    float q, w1;
                    /* the crossfade: a smoothstep that is 0 where tap 1 wraps and 1 where tap 2 wraps; the two
                     * weights sum to exactly 1 (no per-sample sin) */
                    tri = p < 0x80000000u ? (p >> 15) : ((0xFFFFFFFFu - p) >> 15);
                    q = (float)tri * (1.0f / 65536.0f);
                    w1 = q * q * (3.0f - 2.0f * q);
                    shim = ip(ps_buf, pp - ((1u << 25) - (p >> 7)), PS_LEN - 1u) * w1
                           + ip(ps_buf, pp - ((1u << 25) - (p2 >> 7)), PS_LEN - 1u) * (1.0f - w1);
                    ps_phq += 1u << 23;
                    /* a high-pass (about 180 Hz) on the shimmer: an octave up leaves DC and the lows where they
                     * are, so without it the loop builds up there */
                    ps_hp += (shim - ps_hp) * 0.0975f;
                    shim -= ps_hp;
                }
                ps_w = (ps_w + 1u) & (PS_LEN - 1u);
#endif
                for (k = 0; k < 4u; k++) {
                    float in = rin + (k < 2u ? shim : -shim) * rshim * 0.55f;
                    rv_line[k][rv_w & (RV_LEN - 1u)] = qw(lim4(h[k] * rg + in));
                }
                rv_w = (rv_w + 1u) & (RV_LEN - 1u);
            }
            l += rout_l * rmix * 0.7f;
            r += rout_r * rmix * 0.7f;
#endif
            /* AGE: wow and flutter (a modulated short delay), skipped while WOW is 0 */
#ifndef FX_OFF_WOW
            if (wow_run) {
                float wl, wr;
                uint32_t ww = wow_w << 16;
                wow_l[wow_w] = qw(l);
                wow_r[wow_w] = qw(r);
                wow_dq += wow_stepq;
                wl = ip(wow_l, ww - (uint32_t)wow_dq, WOW_LEN - 1u);
                wr = ip(wow_r, ww - (uint32_t)wow_dq, WOW_LEN - 1u);
                wow_w = (wow_w + 1u) & (WOW_LEN - 1u);
                l = wl;
                r = wr;
            }
#endif
        }
        /* AGE: tape dropouts, hiss, dusty crackle, rumble and the record's turn, a softer top and a little drive */
        {
            float pink = 0.0f, hs = 0.0f, hsr = 0.0f, low = 0.0f;
            drop_g += (drop_t - drop_g) * 0.004f;
            l *= drop_g;
            r *= drop_g;
#ifndef FX_OFF_NOISE
            if (crk_on)
                pink = am_rnds();
            /* hiss: a read from each table, summed, per channel */
            if (hiss_on) {
                hs = (float)((int32_t)nz_a[na] + (int32_t)nz_b[nb]) * hiss_k;
                hsr = (float)((int32_t)nz_a[nc] + (int32_t)nz_b[nd]) * hiss_k;
                if (++na >= NZ_A)
                    na = 0u;
                if (++nb >= NZ_B)
                    nb = 0u;
                if (++nc >= NZ_A)
                    nc = 0u;
                if (++nd >= NZ_B)
                    nd = 0u;
            }
            /* crackle: dust. The gaps between clicks bunch up (a squared random gap), the density wanders, the
             * clicks are mostly small with the odd pop, and each is a short one-sided decay */
            if (crk_on) {
                crk_cnt -= 1.0f;
                if (crk_cnt <= 0.0f) {
                    float u = am_rndf(), sz = am_rndf();
                    crk_cnt = crk_gap * 3.0f * u * u + 1.0f;
                    crk = am_rnds() * (0.03f + 0.55f * sz * sz * sz * sz);
                    crk_k = 0.25f + 0.55f * am_rndf();
                    crk_pan = am_rndf();
                }
                /* rumble (a very low noise) and the thump of the record's turn, both with CRACKLE */
                rumble_b += (pink - rumble_b) * 0.003f;
                low = rumble_b * rumble_g;
            }
#endif
            low += thump;
            thump *= 0.999f;
            l += hs + crk * crk_pan + low;
            r += hsr + crk * (1.0f - crk_pan) + low;
            crk *= crk_k;
            /* the worn top and a little drive */
            age_lpl += (l - age_lpl) * age_a;
            age_lpr += (r - age_lpr) * age_a;
            l = age_lpl;
            r = age_lpr;
        }
#ifdef FX_OFF_CLIP
        l = fm_clampf(l * (1.4f + age * 0.8f), -1.0f, 1.0f);
        r = fm_clampf(r * (1.4f + age * 0.8f), -1.0f, 1.0f);
#else
        l = soft_clip(l * (1.4f + age * 0.8f));
        r = soft_clip(r * (1.4f + age * 0.8f));
#endif
        lr[2 * i] = (int32_t)(l * gain_now);
        lr[2 * i + 1] = (int32_t)(r * gain_now);
    }
    if (pk < FX_QUIET_LVL) {
        if (fx_quiet < 100000u)
            fx_quiet++;
    } else {
        fx_quiet = 0;
    }
    for (k = 0; k < 4u; k++) {
        rv_lp[k] = no_denorm(rv_lp[k]);
        rv_dc[k] = no_denorm(rv_dc[k]);
    }
    dl_lpl = no_denorm(dl_lpl);
    dl_lpr = no_denorm(dl_lpr);
    ps_hp = no_denorm(ps_hp);
    rs_flt = no_denorm(rs_flt);
    for (k = 0; k < 4u; k++)
        olp[k] = no_denorm(olp[k]);
    age_lpl = no_denorm(age_lpl);
    age_lpr = no_denorm(age_lpr);
    rumble_b = no_denorm(rumble_b);
    thump = no_denorm(thump);
    crk = no_denorm(crk);
}

void fx_block(int32_t *lr, uint32_t n, uint32_t master_q12) { PLAT_FAR(fx_audio)(lr, n, master_q12); }

float fx_delay_seconds(void) { return dl_seconds(); }
