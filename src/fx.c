/* SPDX-License-Identifier: MPL-2.0 */
/* FX: everything after the voices. A stereo delay, the shimmer reverb (all-passes, a four-line feedback network and an
 * octave-up pitch shifter in its loop), the AGE layer (wow and flutter, hiss, crackle, a worn top end), the soft clip
 * and the master gain. It takes the engines' mix buffers (mx) and writes the audio block. Built at -O2, run from RAM. */
#define AMB_ENGINE 1
#include "plat.h"
#include "app.h"
#include "abyssmael.h"

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

static inline __attribute__((always_inline)) float dl_read(const int16_t *b, float d)
{
    float pos = (float)dl_w - d, fr;
    int32_t i;
    if (pos < 0.0f)
        pos += (float)DL_LEN;
    i = (int32_t)pos;
    fr = pos - (float)i;
    return (b[i & (DL_LEN - 1)] * (1.0f - fr) + b[(i + 1) & (DL_LEN - 1)] * fr) * (1.0f / 8192.0f);
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

static float rd_now[4], rd_step[4], wow_d = 8.0f, wow_dstep;


static PLAT_RAM_CODE void fx_audio(int32_t *lr, uint32_t n, uint32_t master_q12)
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
    for (k = 0; k < 4u; k++)
        rlen[k] = RV_BASE[k] * (0.45f + 2.0f * rsize);
    /* the effects' slow modulation, once a block (and moved smoothly across it) */
    {
        float m0, m1;
        rv_mod += 0.6f / SR * (float)n;
        if (rv_mod >= 1.0f)
            rv_mod -= 1.0f;
        for (k = 0; k < 4u; k++) {
            float d = (k & 1u ? 6.0f : -6.0f) * fm_sin_turns(rv_mod + 0.25f * (float)k);
            rd_step[k] = (rlen[k] + d - rd_now[k]) / (float)n;
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
        m1 = (m0 - wow_d) / (float)n;
        wow_dstep = m1;
    }
    for (i = 0; i < n; i++) {
        float l = mx[0].l[i] + mx[1].l[i], r = mx[0].r[i] + mx[1].r[i], ds_l = mx[0].dl[i] + mx[1].dl[i];
        float ds_r = mx[0].dr[i] + mx[1].dr[i], rs = mx[0].rs[i] + mx[1].rs[i];
        float dl, dr, rin, rout_l, rout_r;
        gain_now += (gain - gain_now) * 0.004f;
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
            for (k = 0; k < 4u; k++) {
                float d, pos, fr;
                int32_t ix;
                rd_now[k] += rd_step[k];
                d = rd_now[k];
                pos = (float)rv_w - d;
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
            wow_l[wow_w] = (int16_t)(fm_clampf(l, -3.9f, 3.9f) * 8192.0f);
            wow_r[wow_w] = (int16_t)(fm_clampf(r, -3.9f, 3.9f) * 8192.0f);
            wow_d += wow_dstep;
            d = wow_d;
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
        l = soft_clip(l * (1.4f + age * 0.8f));
        r = soft_clip(r * (1.4f + age * 0.8f));
        lr[2 * i] = (int32_t)(l * gain_now);
        lr[2 * i + 1] = (int32_t)(r * gain_now);
    }
    for (k = 0; k < 4u; k++) {
        rv_lp[k] = no_denorm(rv_lp[k]);
        rv_dc[k] = no_denorm(rv_dc[k]);
    }
    dl_lpl = no_denorm(dl_lpl);
    dl_lpr = no_denorm(dl_lpr);
    ps_hp = no_denorm(ps_hp);
    age_lpl = no_denorm(age_lpl);
    age_lpr = no_denorm(age_lpr);
    hiss_b[0] = no_denorm(hiss_b[0]);
    hiss_b[1] = no_denorm(hiss_b[1]);
    hiss_b[2] = no_denorm(hiss_b[2]);
    crk = no_denorm(crk);
}

void fx_block(int32_t *lr, uint32_t n, uint32_t master_q12) { PLAT_FAR(fx_audio)(lr, n, master_q12); }
