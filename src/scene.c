/* SPDX-License-Identifier: MPL-2.0 */
/* SCENE: the four scenes: the factory ones, saving and loading them, and the check of a loaded one. Part of app.c's
 * unity build (the UI side). */
static store_t am_store;
_Static_assert(sizeof(store_t) <= PLAT_STORE_MAX, "the scenes must fit one store object");
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
    for (t = 0; t < NTRK; t++) {           /* the sequencers' defaults: a five-in-eight, a melody on six steps */
        static const uint8_t DEF_HN[16] = {0, 2, 4, 7, 4, 2, 0, 5, 0, 2, 4, 7, 9, 7, 4, 2};
        static const uint8_t DEF_SN[16] = {0, 4, 2, 7, 4, 9, 7, 2, 0, 4, 2, 7, 4, 11, 9, 7};
        uint32_t i;
        uint8_t *p = s->tp[t];
        p[TP_EHITS] = 5;
        p[TP_ELEN] = 8;
        p[TP_ECHANCE] = 100;
        p[TP_SLEN] = 16;
        p[TP_SCHANCE] = 100;
        for (i = 0; i < 16u; i++)
            s->hn[t][i] = DEF_HN[i], s->sn[t][i] = DEF_SN[i];
        s->son[t] = 0x4949u;
    }
    if (n == 2) {                            /* TIDE: a Euclidean pluck and a stepped one */
        s->tp[1][TP_SMODE] = 1, s->tp[1][TP_RATE] = 1, s->tp[1][TP_EHITS] = 5, s->tp[1][TP_ELEN] = 8;
        s->tp[5][TP_SMODE] = 2, s->tp[5][TP_RATE] = 1, s->tp[5][TP_OCT] = 4, s->tp[5][TP_SDIR] = 2;
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
    for (t = 0; t < NTRK; t++)
        for (i = 0; i < 16u; i++)
            if ((s->hn[t][i] > 15u && s->hn[t][i] != REST) || s->sn[t][i] > 15u)
                return 0;
    return 1;
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

extern uint8_t amb_two_cores;
