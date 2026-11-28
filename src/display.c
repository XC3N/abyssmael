/* SPDX-License-Identifier: MPL-2.0 */
/* DISPLAY: what the screen shows: the header, the pages' four values, HOME, MIX, the pattern view, the black-key map.
 * It reads the scene and the UI's state and draws with fm1-base's gfx.c (cv_*). Part of app.c's unity build. */
/* ---- the bell's pages (EDIT 1 to 3, ENV 1 and 2): the palette of the new look and one graphic per page ----
 * #006466 #065a60 #0b525b #144552 #1b3a4b #212f45 #272640 #312244 #3e1f47 #4d194d, and #041b15 #136f63 #22aaa1 #4ce0d2 */
#define PAL0 RGB(0, 100, 102)
#define PAL2 RGB(11, 82, 91)
#define PAL3 RGB(20, 69, 82)
#define PAL4 RGB(27, 58, 75)
#define PAL5 RGB(33, 47, 69)
#define PAL6 RGB(39, 38, 64)
#define PAL8 RGB(62, 31, 71)
#define PAL9 RGB(77, 25, 77)
#define ACC1 RGB(19, 111, 99)
#define ACC2 RGB(34, 170, 161)
#define ACC3 RGB(76, 224, 210)
static const int8_t SIN96[96] = {100,100,99,98,97,95,92,90,87,83,79,75,71,66,61,56,50,44,38,32,26,20,13,7,0,-7,-13,-20,
    -26,-32,-38,-44,-50,-56,-61,-66,-71,-75,-79,-83,-87,-90,-92,-95,-97,-98,-99,-100,-100,-100,-99,-98,-97,-95,
    -92,-90,-87,-83,-79,-75,-71,-66,-61,-56,-50,-44,-38,-32,-26,-20,-13,-7,0,7,13,20,26,32,38,44,50,56,61,66,
    71,75,79,83,87,90,92,95,97,98,99,100};   /* (cos of step i/96 of a turn: sin is the same table a quarter turn on) */
/* EUCL ring: hit h of n runs plum, teal, bright teal, so the hits can be told apart while the pattern rotates */
static uint16_t hit_colour(uint32_t h, uint32_t n)
{
    static const uint8_t G[3][3] = {{77, 25, 77}, {19, 111, 99}, {76, 224, 210}};
    uint32_t pos = n > 1u ? h * 200u / (n - 1u) : 0u;   /* 0..200 */
    uint32_t i = pos >= 100u ? 1u : 0u, f = pos - i * 100u, c[3], j;
    for (j = 0; j < 3u; j++)
        c[j] = ((uint32_t)G[i][j] * (100u - f) + (uint32_t)G[i + 1u][j] * f) / 100u;
    return RGB(c[0], c[1], c[2]);
}

static uint32_t bell_page(uint32_t pg)
{
    return pg == PG_EDIT || pg == PG_ENV || pg == PG_LFO;
}
static void bell_dot(int32_t cx, int32_t cy, int32_t r, uint32_t a, uint16_t c)   /* a point on a circle, a in 0..95 */
{
    cv_rect(cx + r * SIN96[a % 96u] / 100 - 1, cy - r * SIN96[(a + 72u) % 96u] / 100 - 1, 2, 2, c);
}
static uint16_t mixc(uint16_t a, uint16_t b, uint32_t f)   /* a to b, f 0..100 (a rough blend of the two 565 colours) */
{
    uint32_t ar = a >> 11, ag = (a >> 5) & 63u, ab = a & 31u, br = b >> 11, bg = (b >> 5) & 63u, bb = b & 31u;
    return (uint16_t)(((ar * (100u - f) + br * f) / 100u) << 11 | ((ag * (100u - f) + bg * f) / 100u) << 5 | ((ab * (100u - f) + bb * f) / 100u));
}
/* a curve as columns: heights from f(x) in 0..h; the fill fades from the edge to the base */
static void bell_graph(const uint8_t *pt, int32_t y0, int32_t h, uint16_t top, uint16_t bot)   /* pt: 232 heights, 0..h */
{
    int32_t x;
    for (x = 0; x < 232; x++) {
        int32_t v = pt[x], y;
        if (v <= 0)
            continue;
        for (y = 0; y < v; y += 2)
            cv_rect(4 + x, y0 - y, 1, 2, mixc(bot, top, (uint32_t)(y * 100 / (h ? h : 1))));
        cv_rect(4 + x, y0 - v, 1, 2, ACC3);
    }
}
/* the envelope's curve of attack, decay, sustain and release (0..100 each) over 232 columns, height h */
static void env_curve(uint8_t *pt, uint32_t a, uint32_t dc, uint32_t s, uint32_t r, int32_t h)
{
    int32_t wa = 6 + (int32_t)a * 60 / 100, wd = 6 + (int32_t)dc * 60 / 100, ws = 40, wr = 6 + (int32_t)r * 60 / 100, x;
    int32_t tot = wa + wd + ws + wr, sh = (int32_t)s * h / 100;
    for (x = 0; x < 232; x++) {
        int32_t u = x * tot / 232, v;
        if (u < wa)
            v = h * u / wa;
        else if (u < wa + wd)
            v = h - (h - sh) * (u - wa) / wd;
        else if (u < wa + wd + ws)
            v = sh;
        else
            v = sh * (tot - u) / wr;
        pt[x] = (uint8_t)(v < 0 ? 0 : v);
    }
}
/* METAL, drawn per pixel: the distance of each pixel to the centre goes through a table, then two per-frame tables (the rings
 * and the glow, by distance) give an intensity that is added to a ground that fades from indigo down. The beat splits it
 * into two centres that circle slowly; the rings drift outward and brighten when the bell is struck. */
static uint8_t d3_tab[4097];                 /* 3 x the distance (in pixels), for a squared distance 0..4096 */
static uint8_t d3_ready, bell_ring, bell_last;
/* a value in the big type, in the smaller one when it would run into the next column (FREE, 1/8., names) */
static void big_value(int32_t x, const char *t)
{
    if (text_w(&FONT_L, t) > 56 && text_w(&FONT_M, t) > 56 && text_w(&FONT_S, t) > 56)
        cv_text(x, 124, &FONT_XS, t, C_WHITE);
    else if (text_w(&FONT_L, t) > 56 && text_w(&FONT_M, t) > 56)
        cv_text(x, 122, &FONT_S, t, C_WHITE);
    else if (text_w(&FONT_L, t) > 56)
        cv_text(x, 118, &FONT_M, t, C_WHITE);
    else
        cv_text(x, 114, &FONT_L, t, C_WHITE);
}
static void d3_init(void)
{
    uint32_t r = 0, i;
    if (d3_ready)
        return;
    for (i = 0; i <= 4096u; i++) {           /* once: 3 x sqrt(d2) by counting */
        while ((r + 1u) * (r + 1u) <= i * 9u)
            r++;
        d3_tab[i] = (uint8_t)r;
    }
    d3_ready = 1;
}
/* add light to a pixel: i 0..255 of the colour (r 0..31, g 0..63, b 0..31 at full) */
static void light_px(int32_t x, int32_t y, uint32_t i, uint32_t cr, uint32_t cg, uint32_t cb)
{
    uint16_t v;
    uint32_t r, g, b;
    if ((uint32_t)x >= cv_w || (uint32_t)y >= 162u)
        return;
    v = swap16(cv_px[(uint32_t)y * cv_w + (uint32_t)x]);
    r = (uint32_t)(v >> 11) + (cr * i >> 8);
    g = (uint32_t)((v >> 5) & 63u) + (cg * i >> 8);
    b = (uint32_t)(v & 31u) + (cb * i >> 8);
    cv_px[(uint32_t)y * cv_w + (uint32_t)x] = swap16((uint16_t)((r > 31u ? 31u : r) << 11 | (g > 63u ? 63u : g) << 5 | (b > 31u ? 31u : b)));
}
/* a soft glow of radius rad (<= 12) around a point, in a 565 colour, amount 0..255 */
static void glow_at(int32_t cx, int32_t cy, int32_t rad, uint16_t col, uint32_t amt)
{
    int32_t x, y;
    uint32_t cr = col >> 11, cg = (col >> 5) & 63u, cb = col & 31u, r2 = (uint32_t)(rad * rad);
    for (y = -rad; y <= rad; y++)
        for (x = -rad; x <= rad; x++) {
            uint32_t d2 = (uint32_t)(x * x + y * y);
            if (d2 < r2) {
                uint32_t q = (r2 - d2) * 255u / r2;
                light_px(cx + x, cy + y, q * q / 255u * amt / 255u, cr, cg, cb);
            }
        }
}
static void disc_at(int32_t cx, int32_t cy, int32_t rad, uint16_t col)
{
    int32_t x, y;
    for (y = -rad; y <= rad; y++)
        for (x = -rad; x <= rad; x++)
            if (x * x + y * y <= rad * rad + rad / 2)
                cv_pset(cx + x, cy + y, col);
}
static void bell_metal(const uint8_t *p)
{
    uint32_t ratio = p[TP_P0], idx = p[TP_P1], dec = p[TP_P2], beat = p[TP_P3];
    uint32_t t = plat_ms(), act = trk_active[cur_trk], n = 3u + idx * 8u / 100u, gap3 = 3u * (5u + ratio * 8u / 100u), i, y, x;
    uint32_t drift3 = (t * (6u + ratio * 12u / 100u) / 1000u) % gap3, en;
    uint8_t lut[256];
    int32_t cxa, cya, cxb, cyb, sep = (int32_t)beat * 14 / 100, two = beat >= 4u;
    uint32_t ph = ((t & 0x3FFFFu) * (40u + beat * 240u / 100u)) / 6545u;
    d3_init();
    if (act > bell_last)                      /* a note began on this track: the bell is struck */
        bell_ring = 255;
    bell_last = (uint8_t)act;
    bell_ring = (uint8_t)(bell_ring - (uint32_t)bell_ring * (4u + dec * 16u / 100u) / 256u);
    en = 64u + (uint32_t)bell_ring * 191u / 255u;       /* the rings' energy 64..255 */
    for (i = 0; i < 256u; i++) {              /* intensity by 3 x distance: the rings, then the glow */
        uint32_t v = 0, g, ri, w, hw3, f, th;
        int32_t xx = (int32_t)i - 18 - (int32_t)drift3;
        if (xx >= 0) {
            ri = (uint32_t)xx / gap3;
            w = (uint32_t)xx % gap3;
            if (ri < n) {
                th = 6u + (n - ri) * 16u / n;                           /* thickness in tenths of a pixel */
                hw3 = th * 3u / 20u + 1u;
                w = w > gap3 / 2u ? w - gap3 / 2u : gap3 / 2u - w;      /* distance from the ring's middle line, in 3x pixels */
                v = w >= hw3 ? 0u : 255u - w * 255u / hw3;
                f = (n + 1u - ri) * 255u / (n + 1u);
                if (dec > 33u)
                    f = f * f / 255u;
                if (dec > 66u)
                    f = f * f / 255u;
                v = v * f / 255u * en / 255u;
            }
        }
        g = i >= 162u ? 0u : (162u - i) * 255u / 162u;                 /* the glow: a soft disc, 0.3 at the middle */
        g = g * g / 255u * (60u + (uint32_t)bell_ring * 80u / 255u) / 255u;
        lut[i] = (uint8_t)(v + g > 255u ? 255u : v + g);
    }
    cxa = 120 - sep * (int32_t)(two) * SIN96[(ph + 72u) % 96u] / 100;
    cya = 50 - sep * (int32_t)two * SIN96[(ph * 13u / 10u) % 96u] * 35 / 10000;
    cxb = 120 + sep * (int32_t)two * SIN96[(ph + 72u) % 96u] / 100;
    cyb = 50 + sep * (int32_t)two * SIN96[(ph * 13u / 10u) % 96u] * 35 / 10000;
    for (y = 0; y < 100u; y++) {
        uint16_t gc = mixc(PAL6, PAL4, y);      /* the ground fades from indigo to blue-teal going down */
        uint32_t gr = gc >> 11, gg = (gc >> 5) & 63u, gb = gc & 31u;
        cv_rect(0, (int32_t)y, 240, 1, gc);
        for (x = 42; x < 198u; x++) {
            int32_t ax = (int32_t)x - cxa, ay = (int32_t)y - cya;
            uint32_t da = (uint32_t)(ax * ax + ay * ay), db = 0xFFFFu, ia = 0, ib = 0, r, g, b;
            if (da <= 4096u)
                ia = lut[d3_tab[da]];
            if (two) {
                int32_t bx = (int32_t)x - cxb, by = (int32_t)y - cyb;
                db = (uint32_t)(bx * bx + by * by);
                if (db <= 4096u)
                    ib = lut[d3_tab[db]];
            }
            if (!(ia | ib))
                continue;
            r = gr + ((9u * ia + 4u * ib) >> 8);
            g = gg + ((56u * ia + 42u * ib) >> 8);
            b = gb + ((26u * ia + 20u * ib) >> 8);
            cv_px[y * cv_w + x] = swap16((uint16_t)((r > 31u ? 31u : r) << 11 | (g > 63u ? 63u : g) << 5 | (b > 31u ? 31u : b)));
        }
    }
}

/* ---- the other instruments' EDIT 1 and 2: one picture each, from the four values (the bell's are in bell_draw) ---- */
static int32_t sin96(uint32_t i) { return SIN96[(i + 72u) % 96u]; }
static int32_t cos96(uint32_t i) { return SIN96[i % 96u]; }
static void dot_ring(int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint16_t c, uint32_t wob)   /* an ellipse of dots, wob: a ripple 0..100 */
{
    uint32_t a;
    for (a = 0; a < 96u; a++) {
        int32_t w = wob ? sin96(a * 6u) * (int32_t)wob / 2000 : 0;
        cv_rect(cx + (rx + w) * sin96(a) / 100, cy - (ry + w) * cos96(a) / 100, 2, 2, c);
    }
}
static void string_line(const uint32_t *h, int32_t cy, int32_t amp, uint32_t nh, uint32_t pos, uint32_t damp, uint16_t col)
{
    int32_t x, py = cy, px = 4;
    for (x = 0; x < 232; x++) {
        int32_t sum = 0, y;
        uint32_t m;
        for (m = 1; m <= nh; m++) {
            int32_t pk = sin96((m * (uint32_t)(pos * 48u / 100u + 1u) * 2u) % 96u) * 100 / 100;   /* the pluck point's weight on this harmonic */
            sum += sin96((m * 48u * (uint32_t)x / 232u) % 96u) * (pk < 0 ? -pk : pk) / (int32_t)m / 100;
        }
        y = cy - sum * amp / 100 * (100 - (int32_t)damp * 85 / 100 * x / 232) / 100;
        if (x)
            cv_line(px, py, 4 + x, y, col);
        px = 4 + x;
        py = y;
    }
    (void)h;
}
static void inst_pic(const uint8_t *p, uint32_t sub)
{
    uint32_t inst = p[TP_INST], a = p[TP_P0 + sub * 4u], b = p[TP_P1 + sub * 4u], c = p[TP_P2 + sub * 4u], e = p[TP_P3 + sub * 4u], i, rng = 12345u + inst * 977u + sub * 31u;
    int32_t gy, x, y;
    for (gy = 0; gy < 100; gy++)
        cv_rect(0, gy, 240, 1, mixc(PAL6, PAL4, (uint32_t)gy));
    if (inst == I_PLUCK && sub == 0u) {      /* STRING: damp, bright, sustain, position: a standing wave, the pluck point marked */
        uint32_t nh = 1u + b * 6u / 100u;
        string_line(0, 50, 8 + (int32_t)c * 36 / 100, nh, e, a, PAL0);
        cv_rect(0, 0, 0, 0, 0);
        string_line(0, 49, 8 + (int32_t)c * 36 / 100, nh, e, a, ACC3);
        x = 4 + (int32_t)e * 224 / 100;
        glow_at(x, 50, 12, ACC3, 200u);
        disc_at(x, 50, 3, C_WHITE);
    } else if (inst == I_PLUCK) {            /* BOW: bow, octave, spread, drift: three strings, further apart with spread, thicker with the bow */
        int32_t sep = 8 + (int32_t)c * 18 / 100, s, th = 1 + (int32_t)a * 3 / 100, px, py, k;
        for (s = -1; s <= 1; s++) {
            px = 4;
            py = 50 + s * sep;
            for (x = 1; x < 232; x++) {
                y = 50 + s * sep - sin96(((uint32_t)x * (2u + b / 14u) * 48u / 232u + e + (uint32_t)(s + 1) * 12u) % 96u) * (8 + (int32_t)a / 5) / 100;
                for (k = 0; k < th; k++)
                    cv_line(px, py + k, 4 + x, y + k, k ? ACC1 : ACC3);
                px = 4 + x;
                py = y;
            }
        }
    } else if (inst == I_PAD && sub == 0u) { /* CHOIR: detune, voices, shape, chorus: voices as dots, scattered by the detune, bigger with the shape, a halo of chorus */
        uint32_t n = 2u + b * 9u / 100u;
        for (i = 0; i < n; i++) {
            int32_t vx = 20 + (int32_t)i * 200 / (int32_t)(n - 1u), vy = 50 + ((int32_t)((i * 37u) % 11u) - 5) * (int32_t)a * 7 / 500;
            glow_at(vx, vy, 4 + (int32_t)c / 14 + (int32_t)e / 12, ACC2, 70u + e);
            disc_at(vx, vy, 2 + (int32_t)c / 25, i % 2u ? ACC3 : ACC2);
        }
    } else if (inst == I_PAD) {              /* BLOOM: harmonic, vibrato, air, width: ellipses, rippled by the vibrato, wider with the width */
        uint32_t n = 2u + a * 6u / 100u;
        for (i = 0; i < n; i++)
            dot_ring(120, 50, (6 + (int32_t)i * 7) * (100 + (int32_t)e) / 100, 6 + (int32_t)i * 6, mixc(ACC3, PAL3, i * 100u / n), b);
        glow_at(120, 50, 12, ACC2, 80u + c);
    } else if (inst == I_DRONE && sub == 0u) { /* STACK: detune, fifth, octave, sub: four bars, each as long as its level; the detune doubles them */
        static const uint8_t lv[4] = {100, 0, 0, 0};
        uint32_t vals[4];
        vals[0] = lv[0], vals[1] = b, vals[2] = c, vals[3] = e;
        for (i = 0; i < 4u; i++) {
            int32_t by = 12 + (int32_t)(3u - i) * 22, len = 20 + (int32_t)vals[i] * 190 / 100;
            glow_at(120, by + 6, 12, ACC2, 40u + vals[i]);
            cv_rect(120 - len / 2, by, len, 12, mixc(PAL0, ACC3, vals[i]));
            if (a > 4u)
                cv_rect(120 - len / 2 + (int32_t)a / 8, by + 14, len, 2, ACC1);
        }
    } else if (inst == I_DRONE) {            /* GRAIN: breath, drive, drift, noise: a wave driven into clipping, noise sprinkled on it */
        int32_t amp = 14 + (int32_t)b * 44 / 100, px = 4, py = 50;
        for (x = 0; x < 232; x++) {
            y = sin96(((uint32_t)x * 4u * 96u / 232u + c / 4u * (uint32_t)x / 40u) % 96u) * amp / 100 * 2;
            y = y > 36 ? 36 : y < -36 ? -36 : y;
            if (x)
                cv_line(px, py, 4 + x, 50 - y, ACC3);
            px = 4 + x;
            py = 50 - y;
            rng = rng * 1103515245u + 12345u;
            if ((rng >> 16) % 100u < e / 2u)
                cv_rect(4 + x, 50 - y + (int32_t)((rng >> 8) % 9u) - 4, 1, 1, ACC2);
        }
        glow_at(120, 50, 12 + (int32_t)a / 10, PAL0, 150u);
    } else if (inst == I_TEXTURE && sub == 0u) { /* WEATHER: color, width, gust, depth: clouds, in layers, spread by the gust */
        uint32_t n = 1u + e * 3u / 100u;
        for (i = 0; i < n; i++)
            for (x = 0; x < 4; x++) {
                rng = rng * 1103515245u + 12345u;
                glow_at(30 + (int32_t)((rng >> 16) % 180u), 28 + (int32_t)i * 18 + ((int32_t)((rng >> 8) % 9u) - 4) * (int32_t)c / 40, 10 + (int32_t)b / 8,
                        mixc(PAL0, ACC3, a), 120u);
            }
    } else {                                 /* DUST: crackle, pitched, move, hiss: specks, a few bright and pitched, streaks of the movement */
        for (i = 0; i < 12u + a; i++) {
            rng = rng * 1103515245u + 12345u;
            x = 4 + (int32_t)((rng >> 16) % 230u);
            y = 2 + (int32_t)((rng >> 8) % 96u);
            cv_rect(x, y, 1 + (int32_t)(b > 50u && i % 5u == 0u), 1, i % 7u ? ACC2 : ACC3);
            if (b > 20u && i % 9u == 0u)
                glow_at(x, y, 6, ACC2, b);
            if (c > 20u && i % 4u == 0u)
                cv_rect(x, y, 1 + (int32_t)c / 6, 1, PAL0);
        }
        for (i = 0; i < e / 2u; i++) {
            rng = rng * 1103515245u + 12345u;
            cv_rect((int32_t)((rng >> 16) % 236u), (int32_t)((rng >> 8) % 100u), 1, 1, PAL3);
        }
    }
}
/* ---- ENV and LFO pages: a smooth curve with a glow, drawn to a sixteenth of a pixel ---- */
static void soft_plot(int32_t x, int32_t y16, uint32_t k, uint32_t cr, uint32_t cg, uint32_t cb)   /* a point at y16/16, split over two pixels */
{
    int32_t y = y16 >> 4, f = y16 & 15;
    light_px(x, y, k * (uint32_t)(16 - f) / 16u, cr, cg, cb);
    light_px(x, y + 1, k * (uint32_t)f / 16u, cr, cg, cb);
}
/* one column of a curve whose edge is at y16: the body fades down from the edge, the edge is bright and bloomed */
static void curve_col(int32_t x, int32_t y16, int32_t base, uint32_t body)
{
    int32_t y, top = (y16 + 15) >> 4;
    for (y = top; y < base; y++) {
        uint32_t depth = (uint32_t)(y - top), k = body * (uint32_t)(80 - (int32_t)(depth > 79u ? 79u : depth)) / 80u;
        light_px(x, y, k, 0, 28, 14);
    }
    soft_plot(x, y16, 255u, 9, 56, 26);
    soft_plot(x, y16 - 24, 90u, 5, 36, 20);
    soft_plot(x, y16 + 24, 50u, 5, 36, 20);
}
static void bell_ground(void)
{
    int32_t gy;
    for (gy = 0; gy < 100; gy++)
        cv_rect(0, gy, 240, 1, mixc(PAL6, PAL4, (uint32_t)gy));
}
/* the four stages of an envelope over 232 columns: the curve, the stage dividers, A D S R over each stage; scale 0..100 the height */
static void env_pic(uint32_t a, uint32_t dc, uint32_t s, uint32_t r, uint32_t scale, uint32_t body)
{
    int32_t wa = 10 + (int32_t)a * 52 / 100, wd = 10 + (int32_t)dc * 52 / 100, ws = 44, wr = 10 + (int32_t)r * 52 / 100, x;
    int32_t tot = wa + wd + ws + wr, hmax = 84 * (int32_t)scale / 100, sh = (int32_t)s * hmax / 100, edge[4];
    static const char *const L[4] = {"A", "D", "S", "R"};
    int32_t xs[5];
    bell_ground();
    xs[0] = 4;
    xs[1] = 4 + wa * 232 / tot;
    xs[2] = 4 + (wa + wd) * 232 / tot;
    xs[3] = 4 + (wa + wd + ws) * 232 / tot;
    xs[4] = 236;
    cv_rect(4, 96, 232, 1, PAL0);                                    /* the floor */
    for (x = 0; x < 232; x++) {
        int32_t u = x * tot / 232, uf = x * tot * 16 / 232, y16;
        int32_t h16;
        if (u < wa)
            h16 = hmax * 16 * uf / (wa * 16);
        else if (u < wa + wd)
            h16 = hmax * 16 - (hmax - sh) * 16 * (uf - wa * 16) / (wd * 16);
        else if (u < wa + wd + ws)
            h16 = sh * 16;
        else
            h16 = sh * 16 * (tot * 16 - uf) / (wr * 16);
        if (h16 < 0)
            h16 = 0;
        y16 = (96 << 4) - h16;
        curve_col(4 + x, y16, 96, body);
    }
    for (x = 1; x < 4; x++)                                          /* where one stage hands over to the next */
        cv_rect(xs[x], 8, 1, 88, PAL3);
    for (x = 0; x < 4; x++) {
        edge[x] = (xs[x] + xs[x + 1]) / 2;
        cv_text(edge[x] - 3, 2, &FONT_T, L[x], x == 2 ? ACC1 : ACC2);
    }
}
/* one LFO: a few cycles of its shape (rate the number of cycles, depth the height), a faint centre line */
static uint32_t lfo_hash(uint32_t i) { i = i * 2654435761u + 12345u; i ^= i >> 15; i *= 2246822519u; return (i >> 8) & 0xFFFFu; }
static void lfo_pic(uint32_t rate, uint32_t depth, uint32_t shape)
{
    int32_t x, amp = 6 + (int32_t)depth * 40 / 100, cy = 50, prev16 = 0;
    uint32_t cyc10 = 10u + rate * 60u / 100u;
    bell_ground();
    cv_rect(4, cy, 232, 1, PAL3);
    for (x = 0; x < 232; x++) {
        uint32_t ph = (uint32_t)x * cyc10 * 1024u / 2320u, t = ph % 1024u, i = ph / 1024u;
        int32_t v, y16, a;
        switch (shape) {
        case 0: v = sin96(t * 96u / 1024u); break;
        case 1: v = t < 512u ? -100 + (int32_t)t * 200 / 512 : 100 - ((int32_t)t - 512) * 200 / 512; break;
        case 2: v = -100 + (int32_t)t * 200 / 1024; break;
        case 3: v = t < 512u ? 100 : -100; break;
        case 4: v = (int32_t)(lfo_hash(i) % 201u) - 100; break;
        default: {                                                   /* drift: a smooth walk between random points */
            int32_t v0 = (int32_t)(lfo_hash(i) % 201u) - 100, v1 = (int32_t)(lfo_hash(i + 1u) % 201u) - 100, f = (int32_t)t * 100 / 1024;
            int32_t sm = f * f * (300 - 2 * f) / 10000;                  /* smoothstep, 0..100 */
            v = v0 + (v1 - v0) * sm / 100;
            break;
        }
        }
        y16 = (cy << 4) - v * amp * 16 / 100;
        if (x && (shape == 3u || shape == 4u) && (y16 > prev16 + 160 || y16 < prev16 - 160)) {   /* the vertical edge of a step */
            int32_t lo = y16 < prev16 ? y16 : prev16, hi = y16 < prev16 ? prev16 : y16, yy;
            for (yy = lo; yy <= hi; yy += 16)
                soft_plot(4 + x, yy, 200u, 9, 56, 26);
        }
        prev16 = y16;
        a = (y16 >> 4) < cy ? y16 >> 4 : cy;
        for (; a < ((y16 >> 4) < cy ? cy : y16 >> 4); a++)            /* the fill between the line and the centre */
            light_px(4 + x, a, 38u, 0, 28, 14);
        soft_plot(4 + x, y16, 255u, 9, 56, 26);
        soft_plot(4 + x, y16 - 32, 80u, 5, 36, 20);
        soft_plot(4 + x, y16 + 32, 80u, 5, 36, 20);
    }
}
static void bell_draw(uint32_t pg, uint32_t sub)
{
    const uint8_t *p = sc.tp[cur_trk];
    uint8_t pt[232];
    uint32_t k, nk = 4;
    int32_t x, cx = 120, cy = 48;
    char t[16];
    cv_rect(0, 0, 240, 100, PAL5);
    if (pg == PG_ENV && sub == 0u) {         /* AMP: the curve of the loudness */
        env_pic(p[TP_ATK], p[TP_DEC], p[TP_SUS], p[TP_REL], 100u, 130u);
    } else if (pg == PG_ENV) {               /* FILTER ENV: its own curve, as tall as the amount (set on EDIT 3) says */
        env_pic(p[TP_FATK], p[TP_FDEC], p[TP_FSUS], p[TP_FREL], 12u + p[TP_FENV] * 88u / 100u, 130u);
    } else if (pg == PG_LFO) {               /* the LFO: its shape at its rate and depth */
        lfo_pic(p[TP_L1RATE + 4u * sub], p[TP_L1DEP + 4u * sub], p[TP_L1SHP + 4u * sub]);
    } else if (sub < 2u && p[TP_INST] != I_BELL) {
        inst_pic(p, sub);
    } else if (sub == 0u) {                  /* METAL: smooth rings and a glow, drawn pixel by pixel (see bell_metal) */
        bell_metal(p);
    } else if (sub == 1u) {                  /* STRIKE: eight partials as bars: strike the height, partial how many sound, spread the gaps, tone the colour */
        uint32_t strike = p[TP_P4], part = p[TP_P5], spread = p[TP_P6], tone = p[TP_P7], i;
        int32_t gap = 8 + (int32_t)spread * 14 / 100;
        for (i = 0; i < 8u; i++) {
            int32_t bx = 120 - (8 * (10 + gap)) / 2 + (int32_t)i * (10 + gap), h = (14 + (int32_t)strike * 78 / 100) * (100 - (int32_t)i * 9) / 100;
            uint16_t c = mixc(PAL0, ACC3, tone);
            if ((int32_t)i * 100 / 8 >= (int32_t)part + 12)
                c = PAL3, h = h / 3;
            cv_rect(bx, 96 - h, 10, h, c);
        }
    } else {                                 /* FILTER: the response: flat, a peak at the cutoff, then down; env amount a ghost where it opens to */
        uint32_t cut = p[TP_CUT], res = p[TP_RES], keyt = p[TP_KEYT], amt = p[TP_FENV];
        int32_t cxp = 20 + (int32_t)cut * 190 / 100 + ((int32_t)keyt - 50) / 6, ox = cxp + (int32_t)amt * (222 - cxp) / 100 / 2, pass;
        for (pass = 0; pass < 2; pass++) {
            int32_t c0 = pass ? ox : cxp;
            for (x = 0; x < 232; x++) {
                int32_t d = x - c0, v = 60, bump = (int32_t)res * 30 / 100;
                if (d > 0)
                    v = 60 - d * 2 / 2 * 60 / 90;
                v += d > -12 && d < 12 ? bump * (12 - (d < 0 ? -d : d)) / 12 : 0;
                pt[x] = (uint8_t)(v < 0 ? 0 : v > 90 ? 90 : v);
            }
            if (pass)
                for (x = 0; x < 232; x += 3)
                    cv_rect(4 + x, 96 - pt[x], 1, 1, ACC2);    /* the ghost: where the envelope opens it to */
            else
                bell_graph(pt, 96, 88, PAL0, PAL4);
        }
    }
    /* the four values, big, under the picture: a small name and the number */
    for (k = 0; k < nk; k++) {
        uint32_t ref = page_ref(pg, sub, k);
        int32_t xx = nk == 1u ? 4 : 4 + (int32_t)k * 59;
        {
            const char *nm = ref_name(ref);
            if (nm[0] == 'F' && nm[1] == 'I' && nm[2] == 'L' && nm[5] == 'R' && nm[6] == ' ')   /* "FILTER ATTACK": the page says FILTER */
                nm += 7;
            cv_text(xx, 102, &FONT_T, nm, ACC2);
        }
        ref_text(ref, t);
        big_value(xx, t);
    }
}

/* FX 4, DELAY SYNC: the echoes on a time line, the beats of the tempo as faint ticks; each echo is quieter by the feedback,
 * the whole row by the mix, the tone sets how bright they are */
static void fx_sync_draw(void)
{
    uint32_t fb = sc.gp[GP_DFB], tone = sc.gp[GP_DTONE], mx = sc.gp[GP_DMIX], k, amp, beat_px;
    int32_t gy, x;
    uint32_t dl_px = (uint32_t)(fx_delay_seconds() * 232.0f / 1.6f + 0.5f);   /* the line shows 1.6 seconds */
    char t[16];
    for (gy = 0; gy < 100; gy++)
        cv_rect(0, gy, 240, 1, mixc(PAL6, PAL4, (uint32_t)gy));
    beat_px = (uint32_t)(60.0f * 232.0f / 1.6f / (float)sc.gp[GP_TEMPO] + 0.5f);
    if (beat_px >= 6u)
        for (x = 4; x < 236; x += (int32_t)beat_px)
            cv_rect(x, 8, 1, 90, PAL3);                      /* the beats */
    cv_rect(4, 98, 232, 1, PAL0);
    amp = 40u + mx * 60u / 100u;                               /* the loudness of the first echo 40..100 */
    for (k = 0; k < 12u; k++) {
        int32_t xx = 4 + (int32_t)(k * dl_px), h;
        if (xx > 236)
            break;
        h = (int32_t)(k ? amp : 100u) * 84 / 100;
        {   /* each echo is the feedback of the one before */
            uint32_t q;
            for (q = 0; q < k; q++)
                h = h * (int32_t)(fb * 92u / 100u + 4u) / 100;
        }
        if (h < 2)
            break;
        glow_at(xx + 1, 98 - h / 2, 9, ACC2, k ? 90u : 160u);
        cv_rect(xx, 98 - h, 3, h, k ? mixc(PAL0, ACC3, tone) : C_WHITE);
    }
    for (k = 0; k < 4u; k++) {
        uint32_t ref = page_ref(PG_FX, 3, k);
        int32_t xx = 4 + (int32_t)k * 59;
        cv_text(xx, 102, &FONT_T, ref_name(ref), ACC2);
        ref_text(ref, t);
        big_value(xx, t);
    }
}

static void draw(void)
{
    /* the tonal voices (1-3) warm, the background voices (4, 5) cool */
    static const uint16_t TCOL[NTRK] = {RGB(76, 224, 210), RGB(34, 170, 161), RGB(140, 215, 170), RGB(170, 90, 185),
                                        RGB(120, 60, 150)};
    char t[32];
    uint32_t k, now = plat_ms(), pg = ui.page, sub = ui.sub[pg];
    /* the header: the track and the page, or a message */
    if (ui.mid_only)                         /* an animation frame: only the middle changes */
        goto middle;
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
        for (k = 0; k < NTRK; k++) {         /* the six tracks, mid-header: the current one in its colour */
            int32_t x = 86 + (int32_t)k * 14 + (k >= 3u ? 8 : 0);   /* a gap between the tonal and background voices */
            char d[2] = {(char)('1' + k), 0};
            cv_rect(x, 3, 12, 14, k == cur_trk ? TCOL[k] : C_LINE);
            cv_text(x + 4, 4, &FONT_T, d, k == cur_trk ? C_BLACK : track_audible(k) ? C_GRAY : C_DIM);
            if (trk_active[k])
                cv_rect(x, 17, 12, 2, TCOL[k]);
        }
        t[0] = 0;
        text_cat(t, PG_NAME[pg]);
        if (pg_n(pg) > 1u) {
            char s[6] = {' ', (char)('1' + sub), '/', (char)('0' + pg_n(pg)), 0, 0};
            text_cat(t, s);
        }
        cv_text(232 - text_w(&FONT_B, t), 2, &FONT_B, t, C_HI);
        cv_text(10, 20, &FONT_XS, page_title(pg, sub), C_GRAY);
        t[0] = 0;
        proj_name(am_store.cur, t);
        if (sc_dirty)
            text_cat(t, " *");
        {                                    /* the load and the tempo go up here, whatever the footer shows */
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
    /* the middle: HOME shows the six tracks, every other page its four values; in scale mode the footer is short, so
     * the middle is taller */
middle:
    cv_begin(240, 162, C_BLACK);
    if (pg == PG_MIX) {                      /* five strips: level, activity, M and S; the track's mix below */
        {
            int32_t gy;
            for (gy = 0; gy < 98; gy++)
                cv_rect(0, gy, 240, 1, mixc(PAL6, PAL4, (uint32_t)gy * 100u / 98u));
        }
        for (k = 0; k < NTRK; k++) {
            int32_t x = 4 + (int32_t)k * 39, h = (int32_t)sc.tp[k][TP_LEVEL] * 56 / 100;
            uint32_t aud = track_audible(k), mu = sc.tp[k][TP_MUTE], so = sc.tp[k][TP_SOLO];
            char n[2] = {(char)('1' + k), 0};
            if (k == cur_trk)
                cv_rect(x - 2, 0, 38, 98, PAL3);
            cv_rect(x, 0, 34, 3, TCOL[k]);
            cv_text(x + 2, 4, &FONT_B, n, aud ? C_WHITE : C_DIM);
            cv_text(x + 14, 7, &FONT_T, INST_3[sc.tp[k][TP_INST]], aud ? C_GRAY : C_DIM);
            cv_rect(x + 4, 24, 8, 56, PAL5);
            {
                int32_t yy;
                for (yy = 0; yy < h; yy++)       /* the level: a rise from teal to bright, the track's own when it sounds */
                    cv_rect(x + 4, 79 - yy, 8, 1, aud ? mixc(PAL0, ACC3, (uint32_t)yy * 100u / 56u) : PAL3);
            }
            if (trk_active[k] && aud)
                glow_at(x + 24, 76, 9, ACC3, 230u);
            else
                cv_rect(x + 21, 73, 6, 6, PAL3);                                   /* sounding */
            cv_rect(x + 16, 26, 16, 14, mu ? PAL9 : PAL5);
            cv_text(x + 20, 26, &FONT_XS, "M", mu ? C_WHITE : PAL0);
            cv_rect(x + 16, 44, 16, 14, so ? ACC3 : PAL5);
            cv_text(x + 21, 44, &FONT_XS, "S", so ? C_BLACK : PAL0);
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
    } else if (pg == PG_SEQ && sub == 2u && sc.tp[cur_trk][TP_SMODE] == 1u) {
        /* EUCL as a ring: the steps round a circle, hits filled, the playhead a spoke; the hits and rotation in play */
        static const int8_t CS[96] = {100,100,99,98,97,95,92,90,87,83,79,75,71,66,61,56,50,44,38,32,26,20,13,7,0,-7,-13,-20,
            -26,-32,-38,-44,-50,-56,-61,-66,-71,-75,-79,-83,-87,-90,-92,-95,-97,-98,-99,-100,-100,-100,-99,-98,-97,-95,
            -92,-90,-87,-83,-79,-75,-71,-66,-61,-56,-50,-44,-38,-32,-26,-20,-13,-7,0,7,13,20,26,32,38,44,50,56,61,66,
            71,75,79,83,87,90,92,95,97,98,99,100};
        const uint8_t *p = sc.tp[cur_trk];
        uint32_t len = p[TP_ELEN], hits = playing ? eu_hits_now[cur_trk] : p[TP_EHITS], rot = playing ? eu_rot_now[cur_trk] : p[TP_EROT] % len;
        uint32_t cur = seq_cur[cur_trk], sel = 99;
        int32_t cx = 62, cy = 62, r = 50, px0 = 0, py0 = 0, fx = 0, fy = 0, hn_sel = -1;
        if (hits > len)
            hits = len;
        if (ui.hold_step >= 0)
            hn_sel = ui.hold_step;
        else
            hn_sel = (int32_t)ui.cursor % (int32_t)(hits ? hits : 1u);
        {   /* the ring in the new style: the ground, a soft band along the circle, glowing hits; the playing one white */
            int32_t gx, gy;
            d3_init();
            for (gy = 0; gy < 122; gy++)
                cv_rect(0, gy, 240, 1, mixc(PAL6, PAL4, (uint32_t)gy * 100u / 122u));
            for (gy = cy - r - 10; gy <= cy + r + 10; gy++)
                for (gx = cx - r - 10; gx <= cx + r + 10; gx++) {
                    int32_t ddx = gx - cx, ddy = gy - cy;
                    uint32_t d2 = (uint32_t)(ddx * ddx + ddy * ddy), w;
                    if (d2 > 4096u)
                        continue;
                    w = (uint32_t)d3_tab[d2] > (uint32_t)(3 * r) ? (uint32_t)d3_tab[d2] - 3u * (uint32_t)r : 3u * (uint32_t)r - d3_tab[d2];
                    if (w < 15u)
                        light_px(gx, gy, (15u - w) * 255u / 15u * 90u / 255u, 9u, 56u, 26u);
                }
            for (k = 0; k < len; k++) {
                uint32_t a = k * 96u / len;
                int32_t x = cx + r * CS[(a + 72u) % 96u] / 100, y = cy - r * CS[a] / 100, h = eu_hit((k + len - rot % len) % len, hits, len);
                if (h >= 0 && h == hn_sel)
                    sel = k;
                if (h < 0) {
                    cv_rect(x - 1, y - 1, 2, 2, PAL0);
                    continue;
                }
                {
                    uint32_t on = sc.hn[cur_trk][h] != REST, now_on = k == cur && playing;
                    uint16_t c = now_on ? C_WHITE : on ? hit_colour((uint32_t)h, hits) : PAL3;
                    if (on)
                        glow_at(x, y, 11, c, now_on ? 255u : 150u);
                    if (k == sel)
                        for (a = 0; a < 96u; a += 3u)
                            cv_rect(x + 8 * CS[(a + 72u) % 96u] / 100, y - 8 * CS[a] / 100, 1, 1, ACC3);
                    disc_at(x, y, now_on ? 5 : 4, c);
                    if (sc.cd[cur_trk][h])         /* a condition: a dark pip */
                        cv_rect(x - 1, y - 1, 2, 2, PAL6);
                }
            }
        }
        {
            int32_t h = hn_sel >= 0 && hn_sel < 16 ? hn_sel : -1;
            int_text((int32_t)hits, t);
            text_cat(t, "/");
            { char c[4]; int_text((int32_t)len, c); text_cat(t, c); }
            cv_text(126, 8, &FONT_B, t, C_WHITE);
            {   /* DRIFT moves the hits and rotation in play away from the knobs: show what the knobs are set to */
                uint32_t sh = p[TP_EHITS] > len ? len : p[TP_EHITS], sr = p[TP_EROT] % len;
                char u[16], n[8];
                u[0] = 0;
                text_cat(u, "HITS");
                if (hits != sh) {
                    text_cat(u, " SET ");
                    int_text((int32_t)sh, n);
                    text_cat(u, n);
                }
                cv_text(126, 26, &FONT_XS, u, hits != sh ? C_HI : C_GRAY);
                int_text((int32_t)rot, t);
                cv_text(126, 46, &FONT_B, t, C_WHITE);
                u[0] = 0;
                text_cat(u, "ROTATE");
                if (rot != sr) {
                    text_cat(u, " SET ");
                    int_text((int32_t)sr, n);
                    text_cat(u, n);
                }
                cv_text(126, 64, &FONT_XS, u, rot != sr ? C_HI : C_GRAY);
            }
            if (h >= 0) {
                uint32_t d = sc.hn[cur_trk][h];
                cv_text(126, 84, &FONT_B, d == REST ? "-" : NOTE_NAME[(uint32_t)degree_note((int32_t)d, 0) % 12u], C_HI);
                cv_text(158, 86, &FONT_XS, COND_NAME[sc.cd[cur_trk][h]], C_HI);
            } else {
                cv_text(126, 90, &FONT_XS, "HOLD A KEY", C_DIM);
            }
        }
        for (k = 0; k < 4u; k++) {
            uint32_t ref = page_ref(PG_SEQ, 2, k);
            int32_t x = 4 + (int32_t)k * 59;
            cv_text(x, 122, &FONT_T, ref_name(ref), C_GRAY);
            ref_text(ref, t);
            cv_text(x, 134, &FONT_S, t, C_HI);
            cv_rect(x, 156, 54, 3, C_DIM);
            cv_rect(x, 156, (int32_t)(ref_frac(ref) * 54u / 200u), 3, TCOL[cur_trk]);
        }
    } else if (pg == PG_SEQ && sub == 2u) {  /* the pattern: 16 cells, each note's name and height, the values */
        const uint8_t *p = sc.tp[cur_trk];
        uint32_t mode = p[TP_SMODE], len = mode == 1u ? p[TP_ELEN] : mode == 2u ? p[TP_SLEN] : p[TP_STEPS];
        int32_t oct = INST_OCT[p[TP_INST]] + (int32_t)p[TP_OCT] - 3;
        uint32_t cur = mode ? seq_cur[cur_trk] : (mo[cur_trk].pos + len - 1u) % len;
        {   /* the new style: an indigo ground, active cells glow, the one playing is white */
            int32_t gy;
            for (gy = 0; gy < 100; gy++)
                cv_rect(0, gy, 240, 1, mixc(PAL6, PAL4, (uint32_t)gy));
        }
        for (k = 0; k < 16u; k++) {
            int32_t x = 8 + (int32_t)k * 14, deg = -1, h = -1, act = 0, hh;
            uint16_t c;
            if (k >= len) {
                cv_rect(x + 5, 15, 2, 2, PAL3);
                continue;
            }
            if (mode == 1u) {
                h = eu_hit((k + len - p[TP_EROT] % len) % len, p[TP_EHITS], len);
                if (h >= 0) {
                    deg = sc.hn[cur_trk][h] == REST ? -2 : sc.hn[cur_trk][h];
                    act = deg >= 0;
                }
            } else if (mode == 2u) {
                deg = sc.sn[cur_trk][k];
                act = (int32_t)(sc.son[cur_trk] >> k & 1u);
            } else if (mo[cur_trk].have || k < mo[cur_trk].pos) {
                deg = mo[cur_trk].deg[k] == -100 ? -1 : mo[cur_trk].deg[k];
                act = deg >= 0;
            }
            c = act ? ACC2 : (mode == 1u && h >= 0) ? PAL0 : PAL5;
            if (act && deg >= 0)
                glow_at(x + 6, 16, 12, ACC2, k == cur && playing ? 230u : 110u);
            cv_rect(x, 6, 12, 20, c);
            if (k == cur && playing)                       /* the step playing */
                cv_rect(x, 0, 12, 4, C_WHITE);
            if ((mode == 1u && h >= 0 && h == (int32_t)ui.cursor) || (mode == 2u && k == ui.cursor))
                cv_rect(x - 1, 28, 14, 3, C_WHITE);          /* where the next note goes */
            if ((mode == 2u && sc.cd[cur_trk][k]) || (mode == 1u && h >= 0 && sc.cd[cur_trk][h]))
                cv_text(x + 6 - text_w(&FONT_T, COND_NAME[sc.cd[cur_trk][mode == 1u ? h : (int32_t)k]]) / 2, 45, &FONT_T,
                        COND_NAME[sc.cd[cur_trk][mode == 1u ? h : (int32_t)k]], C_HI);
            if (deg >= 0 || deg == -2) {
                const char *nm = deg == -2 ? "-" : NOTE_NAME[(uint32_t)degree_note(deg, oct) % 12u];
                cv_text(x + 6 - text_w(&FONT_T, nm) / 2, 33, &FONT_T, nm, act ? C_WHITE : C_DIM);
            }
            if (deg >= 0) {
                hh = (deg + 1) * 3;
                cv_rect(x + 3, 94 - hh, 6, hh, act ? ACC3 : PAL3);
            }
        }
        for (k = 0; k < 4u; k++) {
            uint32_t ref = page_ref(PG_SEQ, 2, k);
            int32_t x = 4 + (int32_t)k * 59;
            cv_text(x, 102, &FONT_T, ref_name(ref), C_GRAY);
            ref_text(ref, t);
            cv_text(x, 116, &FONT_S, t, C_HI);
            cv_rect(x, 136, 54, 3, C_DIM);
            cv_rect(x, 136, (int32_t)(ref_frac(ref) * 54u / 200u), 3, TCOL[cur_trk]);
        }
    } else if (pg == PG_GLO && sub == 3u) {  /* BUILD AND CPU: the biggest type, a line each: now (peak); the build at the foot */
        static const char *const RL[6] = {"SEQ", "SETUP", "VOICES", "FX", "RENDER", "TOTAL"};
        uint32_t cpu = plat_cpu_pct(), v[6], pk[6];
        if (now - ui.cpu_at > 5000u)            /* the page was not on screen: start again */
            ui.cpu_avg = (uint8_t)cpu;
        ui.cpu_at = now;
        if (cpu > ui.cpu_max)
            ui.cpu_max = (uint8_t)cpu;
        ui.cpu_avg = (uint8_t)((ui.cpu_avg * 7u + cpu + 4u) / 8u);   /* drawn about twice a second: some 4 s */
        for (k = 0; k < 5u; k++)                /* prof_*: S, B, V, F, then R, the whole render call */
        {
            if (prof_max[k] > ui.pk[k])         /* the peaks hold until K1 clears them (the engine's own peak fades in 2 s) */
                ui.pk[k] = prof_max[k];
            v[k] = prof_pct[k], pk[k] = ui.pk[k];
        }
        v[5] = cpu, pk[5] = ui.cpu_max;          /* C - R is the rest of the audio interrupt */
        for (k = 0; k < 6u; k++) {
            char c[8];
            int32_t y = (int32_t)k * 20, x;
            t[0] = 0;
            int_text((int32_t)v[k], c);
            text_cat(t, c);
            text_cat(t, " (");
            int_text((int32_t)pk[k], c);
            text_cat(t, c);
            text_cat(t, ")");
            x = 232 - text_w(&FONT_M, t);
            cv_text(8, y, &FONT_M, RL[k], k >= 4u ? C_WHITE : C_GRAY);
            cv_text(x, y, &FONT_M, t, k == 5u && cpu > 80u ? C_AMB : C_HI);
        }
        {
            char c[8];
            cv_text(8, 117, &FONT_M, APP_TAGLINE, C_WHITE);   /* the build, in the big type: what is running */
            t[0] = 0;
            if (amb_stress)
                text_cat(t, "STRESS  ");
            text_cat(t, "AVG ");
            int_text((int32_t)ui.cpu_avg, c);
            text_cat(t, c);
            cv_text(8, 141, &FONT_S, t, C_GRAY);
            tag_drawn = APP_TAGLINE[0] == 'v';
        }
    } else if (pg == PG_FX && sub == 3u) {
        fx_sync_draw();
    } else if (bell_page(pg)) {              /* the bell: one picture per page, drawn from the four values, big numbers under it */
        bell_draw(pg, sub);
    } else if (pg == PG_SAV && sub == 0u) {  /* the projects: four factory (locked), six of yours; K1 moves */
        for (k = 0; k < NLIST; k++) {
            int32_t y = (int32_t)k * 16;
            char nm[12], n[4] = {(char)('0' + (k + 1u) / 10u), (char)('0' + (k + 1u) % 10u), 0, 0};
            uint32_t have = k < NFACT || proj_saved(k);
            if (n[0] == '0')
                n[0] = ' ';
            proj_name(k, nm);
            if (k == ui.proj)
                cv_rect(0, y, 240, 15, C_LINE);
            if (k == am_store.cur)
                cv_rect(2, y + 3, 4, 9, sc_dirty ? C_AMB : C_HI);
            cv_text(12, y, &FONT_XS, n, C_DIM);
            cv_text(36, y, &FONT_XS, nm, have ? (k == ui.proj ? C_WHITE : C_GRAY) : C_DIM);
            if (k < NFACT)
                cv_text(232 - text_w(&FONT_XS, "LOCKED"), y, &FONT_XS, "LOCKED", C_DIM);
        }
    } else if (pg == PG_SAV) {               /* NAME: the eight letters of the project the list is on; K1 the place, K2 the letter */
        char nm[12], one[2] = {0, 0};
        uint32_t i;
        proj_name(ui.proj, nm);
        cv_text(8, 4, &FONT_XS, ui.proj < NFACT ? "FACTORY: LOCKED" : "NAME", C_GRAY);
        for (i = 0; i < NAMELEN; i++) {
            int32_t x = 8 + (int32_t)i * 28;
            one[0] = ui.proj >= NFACT && ui.proj < NLIST && am_store.name[ui.proj - NFACT][i] ? am_store.name[ui.proj - NFACT][i] : ' ';
            cv_rect(x, 40, 24, 3, i == ui.npos ? C_HI : C_LINE);
            cv_text(x + 6, 16, &FONT_B, one, i == ui.npos ? C_WHITE : C_GRAY);
        }
        cv_text(8, 60, &FONT_XS, "K1 PLACE   K2 LETTER", C_DIM);
        cv_text(8, 76, &FONT_XS, "OK SAVES THE NAME", C_DIM);
    } else if (pg == PG_HOME && sub == 0u) {
        {
            int32_t gy;
            for (gy = 0; gy < 100; gy++)
                cv_rect(0, gy, 240, 1, mixc(PAL6, PAL4, (uint32_t)gy));
        }
        for (k = 0; k < NTRK; k++) {
            int32_t y = (int32_t)k * 20 + 1, xx;
            uint32_t w = sc.tp[k][TP_LEVEL] * 100u / 100u, aud = track_audible(k);
            char n[2] = {(char)('1' + k), 0};
            if (k == cur_trk)
                cv_rect(0, y - 1, 240, 19, PAL3);
            cv_rect(4, y + 2, 3, 13, TCOL[k]);
            cv_text(12, y, &FONT_S, n, aud ? C_WHITE : PAL0);
            cv_text(26, y, &FONT_S, INST_NAME[sc.tp[k][TP_INST]], aud ? (k == cur_trk ? C_WHITE : C_GRAY) : PAL0);
            cv_rect(100, y + 6, 100, 5, PAL5);
            for (xx = 0; xx < (int32_t)w; xx++)   /* the level: teal rising to bright */
                cv_rect(100 + xx, y + 6, 1, 5, aud ? mixc(PAL0, ACC3, (uint32_t)xx) : PAL3);
            if (sc.tp[k][TP_MUTE])
                cv_text(206, y, &FONT_S, "M", ACC2);
            if (sc.tp[k][TP_SOLO])
                cv_text(218, y, &FONT_S, "S", C_WHITE);
            if (trk_active[k] && aud)
                glow_at(236, y + 8, 6, ACC3, 230u);
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
            uint32_t ref = page_ref(pg, sub, k);
            int32_t y = (int32_t)k * 40;
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
    if (ui.mid_only)
        return;
    /* the footer: in scale mode the black keys' map (each key's label over its place on the keyboard), else the
     * six tracks, the key and scale, the modes, the load */
    if (scale_mode) {                        /* the six keys that choose (the tracks are in the header): big labels, a bar each, MIX last */
        cv_begin(240, 34, C_BLACK);
        for (k = 5; k < 11u; k++) {
            uint32_t arg, lit, a;
            const char *lbl;
            char l5[6];
            int32_t cx = 20 + (int32_t)(k - 5u) * 40, i, lw;
            uint16_t bright, dim;
            a = slot(k, &arg, &lbl, &lit);
            switch (a) {                      /* the colours say what a key is: a choice, MIX */
            case S_NONE: bright = dim = C_LINE; break;
            case S_MIX: bright = C_WHITE; dim = C_GRAY; break;
            default: bright = ACC2; dim = PAL0; break;
            }
            cv_rect(cx - 15, 23, 30, 9, lit ? bright : dim);
            if (a == S_NONE)
                continue;
            for (i = 0; i < 5 && lbl[i]; i++)
                l5[i] = lbl[i];
            l5[i] = 0;
            while (i > 1 && text_w(&FONT_S, l5) > 38)
                l5[--i] = 0;
            lw = text_w(&FONT_S, l5);
            cv_text(cx - lw / 2, 3, &FONT_S, l5, lit ? C_WHITE : ACC2);
        }
        cv_blit(0, 206);
        return;
    }
    /* scale mode off: the same 34 px strip, the modes and the key and scale (the tracks, tempo and load are in the header) */
    cv_begin(240, 34, C_BLACK);
    cv_text(8, 2, &FONT_XS, playing ? "PLAY" : "STOP", playing ? C_HI : C_DIM);
    cv_text(54, 2, &FONT_XS, "SCALE", scale_mode ? C_HI : C_DIM);
    cv_text(104, 2, &FONT_XS, "LATCH", latch ? C_HI : C_DIM);
    t[0] = 0;
    text_cat(t, NOTE_NAME[sc.gp[GP_KEY] % 12u]);
    text_cat(t, " ");
    text_cat(t, SCALE_NAME[sc.gp[GP_SCALE]]);
    cv_text(232 - text_w(&FONT_XS, t), 2, &FONT_XS, t, C_HI);
    cv_text(8, 18, &FONT_XS, "KEYS ARE CHROMATIC. SEL: SCALE", C_DIM);
    cv_blit(0, 206);
}

