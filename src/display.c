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
static uint32_t bell_page(uint32_t pg)
{
    return (pg == PG_EDIT || pg == PG_ENV) && sc.tp[cur_trk][TP_INST] == I_BELL;
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
static void bell_draw(uint32_t pg, uint32_t sub)
{
    const uint8_t *p = sc.tp[cur_trk];
    uint8_t pt[232];
    uint32_t k, nk = 4;
    int32_t x, cx = 120, cy = 48;
    char t[16];
    cv_rect(0, 0, 240, 100, PAL5);
    if (pg == PG_ENV && sub == 0u) {         /* AMP: the curve of the loudness */
        env_curve(pt, p[TP_ATK], p[TP_DEC], p[TP_SUS], p[TP_REL], 88);
        bell_graph(pt, 96, 88, PAL0, PAL4);
    } else if (pg == PG_ENV) {               /* FILTER ENV: its own curve, as tall as the amount (set on EDIT 3) says */
        env_curve(pt, p[TP_FATK], p[TP_FDEC], p[TP_FSUS], p[TP_FREL], 88);
        for (x = 0; x < 232; x++)
            pt[x] = (uint8_t)(pt[x] * (12 + (int32_t)p[TP_FENV] * 88 / 100) / 100);
        bell_graph(pt, 96, 88, ACC1, PAL3);
        cv_rect(4, 96 - 12 * 88 / 100, 232, 1, PAL0);        /* where the filter rests */
    } else if (sub == 0u) {                  /* METAL: rings; ratio spaces them, index how many, decay how fast they fade, beat a second set */
        uint32_t ratio = p[TP_P0], idx = p[TP_P1], dec = p[TP_P2], beat = p[TP_P3], n = 2u + idx * 7u / 100u, i, a;
        for (i = 0; i < n; i++) {
            int32_t r = 8 + (int32_t)i * (6 + (int32_t)ratio * 8 / 100);
            uint32_t f = 100u - (i * (dec + 10u) / n > 100u ? 100u : i * (dec + 10u) / n);
            if (r > 46)
                break;
            for (a = 0; a < 96u; a += 2u) {
                bell_dot(cx - (int32_t)beat * 10 / 100, cy, r, a, mixc(PAL3, ACC3, f));
                if (beat > 4u)
                    bell_dot(cx + (int32_t)beat * 10 / 100, cy, r, a, mixc(PAL3, ACC1, f));
            }
        }
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
        cv_text(xx, 114, &FONT_L, t, C_WHITE);
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
    cv_begin(240, 162, C_BLACK);
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
        for (k = 0; k < len; k++) {
            uint32_t a = k * 96u / len;
            int32_t x = cx + r * CS[(a + 72u) % 96u] / 100, y = cy - r * CS[a] / 100, h = eu_hit((k + len - rot % len) % len, hits, len);
            if (k)
                cv_line(px0, py0, x, y, C_LINE);
            else
                fx = x, fy = y;
            px0 = x;
            py0 = y;
            if (h >= 0 && h == hn_sel)
                sel = k;
        }
        cv_line(px0, py0, fx, fy, C_LINE);
        if (playing) {                       /* the playhead's spoke */
            uint32_t a = cur % len * 96u / len;
            cv_line(cx, cy, cx + (r - 8) * CS[(a + 72u) % 96u] / 100, cy - (r - 8) * CS[a] / 100, C_DIM);
        }
        for (k = 0; k < len; k++) {
            uint32_t a = k * 96u / len;
            int32_t x = cx + r * CS[(a + 72u) % 96u] / 100, y = cy - r * CS[a] / 100, h = eu_hit((k + len - rot % len) % len, hits, len);
            if (h < 0) {
                cv_rect(x - 1, y - 1, 3, 3, C_DIM);
            } else {
                uint32_t on = sc.hn[cur_trk][h] != REST;
                if (k == sel)
                    cv_rect(x - 7, y - 7, 15, 15, C_HI);
                cv_rect(x - 5, y - 5, 11, 11, k == cur && playing ? C_WHITE : on ? TCOL[cur_trk] : C_LINE);
                if (sc.cd[cur_trk][h])         /* a condition: a dark pip */
                    cv_rect(x - 1, y - 1, 3, 3, C_BLACK);
            }
        }
        {
            int32_t h = hn_sel >= 0 && hn_sel < 16 ? hn_sel : -1;
            int_text((int32_t)hits, t);
            text_cat(t, "/");
            { char c[4]; int_text((int32_t)len, c); text_cat(t, c); }
            cv_text(126, 8, &FONT_B, t, C_WHITE);
            cv_text(126, 26, &FONT_XS, "HITS", C_GRAY);
            int_text((int32_t)rot, t);
            cv_text(126, 46, &FONT_B, t, C_WHITE);
            cv_text(126, 64, &FONT_XS, "ROTATE", C_GRAY);
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
        for (k = 0; k < 16u; k++) {
            int32_t x = 8 + (int32_t)k * 14, deg = -1, h = -1, act = 0, hh;
            uint16_t c;
            if (k >= len) {
                cv_rect(x + 4, 14, 4, 4, C_LINE);
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
            c = act ? TCOL[cur_trk] : (mode == 1u && h >= 0) ? C_DIM : C_LINE;
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
                cv_rect(x + 3, 94 - hh, 6, hh, act ? TCOL[cur_trk] : C_LINE);
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
    /* the footer: in scale mode the black keys' map (each key's label over its place on the keyboard), else the
     * six tracks, the key and scale, the modes, the load */
    if (scale_mode) {                        /* the 11 black keys alone, in their groups (3 2 3 2 1): a label row, the keys */
        static const uint8_t GRP[11] = {0, 0, 0, 1, 1, 2, 2, 2, 3, 3, 4};
        cv_begin(240, 34, C_BLACK);
        for (k = 0; k < 11u; k++) {
            uint32_t arg, lit, a;
            const char *lbl;
            char l4[5];
            int32_t cx = 12 + (int32_t)k * 19 + (int32_t)GRP[k] * 7, x, lw, i;
            uint16_t bright, dim;
            a = slot(k, &arg, &lbl, &lit);
            switch (a) {                      /* the colours say what a key is: pages, track, a choice, MIX */
            case S_NONE: bright = dim = C_LINE; break;
            case S_TRK: case S_MTRK: bright = TCOL[arg]; dim = (uint16_t)((TCOL[arg] >> 1) & 0x7BEFu); break;
            case S_MIX: bright = C_WHITE; dim = C_GRAY; break;
            default: bright = ACC2; dim = PAL0; break;
            }
            cv_rect(cx - 8, 20, 16, 12, lit ? bright : dim);
            if (a == S_NONE)
                continue;
            for (i = 0; i < 4 && lbl[i]; i++)
                l4[i] = lbl[i];
            l4[i] = 0;
            while (i > 1 && text_w(&FONT_T, l4) > 18)   /* keys are 19 px apart: a label that is wider loses a letter */
                l4[--i] = 0;
            lw = text_w(&FONT_T, l4);
            x = clamp(cx - lw / 2, 0, 240 - lw);
            cv_text(x, 3, &FONT_T, l4, lit ? bright : dim);
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

