/* Browser simulator of AMBIENT: the firmware's own app.c, unmodified, built to WebAssembly against a small host
 * platform. The screen is fm1-base's own renderer (gfx.c, with the app's generated fonts) drawing into a 240 x 240
 * framebuffer the page reads. Needs fm1-base's include/plat.h, plat_uid.h and fastmath.h (MPL-2.0). */
#include <stdint.h>
#include "plat.h"
#include "app.h"

void *memcpy(void *d, const void *s, unsigned long n) { unsigned char *a = d; const unsigned char *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int c, unsigned long n) { unsigned char *a = d; while (n--) *a++ = (unsigned char)c; return d; }
unsigned long strlen(const char *s) { unsigned long n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }

/* ---- the screen: RGB565, byte-swapped as the panel takes it ---- */
static uint16_t fb[240 * 240];
static uint32_t blits;
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint16_t s = (uint16_t)((c >> 8) | (c << 8));
    for (uint32_t j = y; j < y + h && j < 240u; j++)
        for (uint32_t i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = s;
    blits++;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    for (uint32_t j = 0; j < h && y + j < 240u; j++)
        for (uint32_t i = 0; i < w && x + i < 240u; i++)
            fb[(y + j) * 240u + x + i] = px[j * w + i];
    blits++;
}
#include "gfx.c"

/* ---- the screen reader stand-in: what the app says goes to the page (aria-live) ---- */
#define ANN_NOW 1u
static char say_text[64];
static uint32_t say_seq;
static int ann_on(void) { return 1; }
static void ann_tick(uint32_t now) { (void)now; }
static void ann_say(const char *a, const char *b, uint32_t flags)
{
    unsigned i = 0;
    (void)flags;
    while (a && *a && i < 62) say_text[i++] = *a++;
    if (b && *b && i < 61) { say_text[i++] = ' '; while (*b && i < 63) say_text[i++] = *b++; }
    say_text[i] = 0;
    say_seq++;
}

/* ---- the host platform ---- */
static uint32_t now_ms, held_keys, held_btn, midi_q[64], midi_n, lit_b, lit_k, dim_b, dim_k;
static int32_t enc_acc[NE];
static uint8_t st_buf[4096]; static int st_len = -1;
uint32_t plat_ms(void) { return now_ms; }
uint32_t plat_buttons(void) { return held_btn; }
uint32_t plat_keys(void) { return held_keys; }
int32_t plat_enc(int r) { int32_t v = enc_acc[r]; enc_acc[r] = 0; return v; }
void plat_leds(uint32_t b, uint32_t k) { lit_b = b; lit_k = k; dim_b = dim_k = 0; }
void plat_lights(uint32_t b, uint32_t k, uint32_t db, uint32_t dk, int glow) { (void)glow; lit_b = b; lit_k = k; dim_b = db; dim_k = dk; }
void plat_glow(int on) { (void)on; }
int plat_midi_in(uint32_t *pkt, int *port) {
    static uint32_t rd;
    if (rd == midi_n) { rd = midi_n = 0; return 0; }
    *pkt = midi_q[rd++]; *port = 0; return 1;
}
void plat_midi_send(uint32_t pkt, uint32_t to) { (void)pkt; (void)to; }
int plat_store_load(uint32_t obj, void *dst, uint32_t max) { (void)obj; if (st_len < 0) return -1; memcpy(dst, st_buf, (unsigned long)st_len < max ? (unsigned long)st_len : max); return st_len; }
int plat_store_save(uint32_t obj, const void *src, uint32_t len) { (void)obj; if (len > sizeof st_buf) return -8; memcpy(st_buf, src, len); st_len = (int)len; return 0; }
uint32_t plat_cpu_pct(void) { return 0; }
/* no second core in the browser: one core does it all */
int plat_cpu2_set(int on) { (void)on; return -1; }
int plat_cpu2_on(void) { return 0; }
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg) { (void)fn; (void)arg; return 0; }
int plat_cpu2_wait(void) { return 0; }
void plat_cpu2_sync(void) {}
int plat_cpu2_busy(void) { return 0; }

#include "app.c"

/* ---- the page's side ---- */
#define OUT_FRAMES 4096
static int32_t out_i32[2 * APP_BLOCK];
static float out_f32[2 * OUT_FRAMES];
__attribute__((export_name("sim_init"))) void sim_init(uint32_t seed) { now_ms = seed; app_init(); app_ui_init(); }
__attribute__((export_name("sim_buf"))) float *sim_buf(void) { return out_f32; }
__attribute__((export_name("sim_fb"))) uint16_t *sim_fb(void) { return fb; }
__attribute__((export_name("sim_say"))) const char *sim_say(void) { return say_text; }
/* render n frames (any n up to OUT_FRAMES) as the device does, APP_BLOCK at a time, into sim_buf() as -1..1 floats */
__attribute__((export_name("sim_render"))) void sim_render(uint32_t n)
{
    static float ms_frac;
    uint32_t done = 0;
    while (done < n) {
        uint32_t b = n - done < APP_BLOCK ? n - done : APP_BLOCK;
        app_render(out_i32, b, 4096u);
        for (uint32_t i = 0; i < 2 * b; i++) out_f32[2 * done + i] = (float)out_i32[i] / 8388608.0f;
        done += b;
        ms_frac += (float)b * 1000.0f / 44100.0f;
        while (ms_frac >= 1.0f) { now_ms++; ms_frac -= 1.0f; }
    }
    app_frame();
}
__attribute__((export_name("sim_key"))) void sim_key(uint32_t k, int down) { if (down) held_keys |= 1u << k; else held_keys &= ~(1u << k); app_input_only(); }
__attribute__((export_name("sim_button"))) void sim_button(uint32_t b, int down) { if (down) held_btn |= 1u << b; else held_btn &= ~(1u << b); app_input_only(); }
__attribute__((export_name("sim_enc"))) void sim_enc(int32_t role, int32_t d) { enc_acc[role] += d; app_input_only(); }
__attribute__((export_name("sim_midi"))) void sim_midi(uint32_t pkt) { if (midi_n < 64) midi_q[midi_n++] = pkt; }
/* 0 playing, 1 track, 2 page*10+sub, 3 scale mode, 4 dirty, 5 scene, 6 voices, 7 lit buttons, 8 lit keys,
 * 9 dim buttons, 10 dim keys, 11 what was said (a count), 12 the screen's blits (a count) */
__attribute__((export_name("sim_get"))) int32_t sim_get(uint32_t what)
{
    static const char *const K[] = {"playing", "track", "page", "scale_mode", "dirty", "scene", "voices"};
    int got = 0;
    if (what < 7u) { app_expect(K[what], &got); return got; }
    switch (what) {
    case 7: return (int32_t)lit_b; case 8: return (int32_t)lit_k; case 9: return (int32_t)dim_b; case 10: return (int32_t)dim_k;
    case 11: return (int32_t)say_seq; case 12: return (int32_t)blits;
    }
    return 0;
}
