/* SPDX-License-Identifier: MPL-2.0 */
/* DISPLAY: what the screen shows: the header, the pages' four values, HOME, MIX, the pattern view, the black-key map.
 * It reads the scene and the UI's state and draws with fm1-base's gfx.c (cv_*). Part of app.c's unity build. */
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
        cv_text(10, 20, &FONT_XS, page_title(pg, sub), C_GRAY);
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
            if (ui.note_edit && ((mode == 1u && h == (int32_t)ui.cursor) || (mode == 2u && k == ui.cursor)))
                cv_rect(x - 1, 28, 14, 3, C_WHITE);          /* where the next note goes */
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
            uint32_t ref = page_ref(pg, sub, k);
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
    if (scale_mode) {                        /* the 11 black keys alone, in their groups (3 2 3 2 1): labels above */
        static const uint8_t GRP[11] = {0, 0, 0, 1, 1, 2, 2, 2, 3, 3, 4};
        for (k = 0; k < 11u; k++) {
            uint32_t arg, lit, a;
            const char *lbl;
            int32_t cx = 12 + (int32_t)k * 19 + (int32_t)GRP[k] * 7, x, lw;
            uint16_t col;
            a = slot(k, &arg, &lbl, &lit);
            col = a == S_NONE ? C_LINE : lit ? ((a == S_TRK || a == S_MTRK) ? TCOL[arg] : C_HI) : C_DIM;
            cv_rect(cx - 8, 36, 16, 14, col);
            if (a == S_NONE)
                continue;
            lw = text_w(&FONT_XS, lbl);
            x = clamp(cx - lw / 2, 0, 240 - lw);
            cv_text(x, (k & 1u) ? 18 : 2, &FONT_XS, lbl, lit ? (a == S_MTRK && ui.mix_mode == 0u ? C_AMB : C_WHITE)
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
#ifdef AMB_PROFILE
    {   /* S sequencer, B voice setup, V voices, F effects: % of the block time */
        static const char L[4] = {'S', 'B', 'V', 'F'};
        uint32_t q;
        for (q = 0; q < 4u; q++) {
            char c[8], l[2] = {L[q], 0};
            int_text((int32_t)prof_pct[q], c);
            text_cat(t, l);
            text_cat(t, c);
            text_cat(t, " ");
        }
    }
#endif
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

