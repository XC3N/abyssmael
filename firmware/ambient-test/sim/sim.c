/* Browser simulator of AMBIENT TEST: the firmware's own app.c, unmodified, built to WebAssembly against a small host
 * platform (no screen: the page draws the controls). Needs fm1-base's include/plat.h and include/fastmath.h (MPL-2.0). */
#include <stdint.h>
#include "plat.h"
#include "app.h"

typedef unsigned long size_t_;
void *memcpy(void *d, const void *s, unsigned long n) { unsigned char *a = d; const unsigned char *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int c, unsigned long n) { unsigned char *a = d; while (n--) *a++ = (unsigned char)c; return d; }
unsigned long strlen(const char *s) { unsigned long n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }
typedef struct { int dummy; } font_t;
static font_t FONT_B, FONT_S, FONT_XS;
#define C_BLACK 0
#define C_WHITE 1
#define C_HI 2
#define C_DIM 3
#define C_GRAY 4
#define C_LINE 5
#define C_AMB 6
static void cv_begin(int w, int h, int c) { (void)w; (void)h; (void)c; }
static int32_t cv_text(int x, int y, font_t *f, const char *s, int c) { (void)y; (void)f; (void)c; return x + (int32_t)strlen(s) * 6; }
static void cv_rect(int x, int y, int w, int h, int c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static void cv_blit(int x, int y) { (void)x; (void)y; }
static void lcd_fill(int x, int y, int w, int h, int c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static void ann_tick(uint32_t now) { (void)now; }
#define ANN_NOW 0
static int ann_on(void) { return 1; }
static void ann_say(const char *t, int a, int b);

/* ---- the host platform ---- */
static uint32_t now_ms, held_keys, held_btn, midi_q[64], midi_n;
static int32_t enc_acc[NE];
static uint8_t store[256]; static int store_len = -1;
uint32_t plat_ms(void) { return now_ms; }
uint32_t plat_buttons(void) { return held_btn; }
uint32_t plat_keys(void) { return held_keys; }
int32_t plat_enc(int r) { int32_t v = enc_acc[r]; enc_acc[r] = 0; return v; }
void plat_leds(uint32_t b, uint32_t k) { (void)b; (void)k; }
void plat_glow(int on) { (void)on; }
int plat_midi_in(uint32_t *pkt, int *port) {
    static uint32_t rd;
    if (rd == midi_n) { rd = midi_n = 0; return 0; }
    *pkt = midi_q[rd++]; *port = 0; return 1;
}
void plat_midi_send(uint32_t pkt, uint32_t to) { (void)pkt; (void)to; }
int plat_store_load(uint32_t obj, void *dst, uint32_t max) { (void)obj; if (store_len < 0) return -1; memcpy(dst, store, (unsigned long)store_len < max ? (unsigned long)store_len : max); return store_len; }
int plat_store_save(uint32_t obj, const void *src, uint32_t len) { (void)obj; if (len > sizeof store) return -8; memcpy(store, src, len); store_len = (int)len; return 0; }
uint32_t plat_cpu_pct(void) { return 0; }

static char say_text[48]; static uint32_t say_seq;
static void ann_say(const char *t, int a, int b) { (void)a; (void)b; unsigned i = 0; while (t[i] && i < 47) { say_text[i] = t[i]; i++; } say_text[i] = 0; say_seq++; }
#include "app.c"
__attribute__((export_name("sim_say"))) const char *sim_say(void) { return say_text; }

/* ---- the page's side ---- */
#define OUT_FRAMES 4096
static int32_t out_i32[2 * APP_BLOCK];
static float out_f32[2 * OUT_FRAMES];
static uint32_t carry;
__attribute__((export_name("sim_init"))) void sim_init(uint32_t seed) { now_ms = seed; app_init(); app_ui_init(); }
__attribute__((export_name("sim_buf"))) float *sim_buf(void) { return out_f32; }
/* render n frames (any n up to OUT_FRAMES) as the device does, APP_BLOCK at a time, into sim_buf() as -1..1 floats */
__attribute__((export_name("sim_render"))) void sim_render(uint32_t n)
{
    uint32_t done = 0;
    while (done < n) {
        uint32_t b = n - done < APP_BLOCK ? n - done : APP_BLOCK;
        app_render(out_i32, b, 4096u);
        for (uint32_t i = 0; i < 2 * b; i++) out_f32[2 * done + i] = (float)out_i32[i] / 8388608.0f;
        done += b;
        now_ms += (uint32_t)((float)b * 1000.0f / 44100.0f + 0.5f);
        carry = done;
    }
    app_frame();
}
__attribute__((export_name("sim_key"))) void sim_key(uint32_t k, int down) { if (down) held_keys |= 1u << k; else held_keys &= ~(1u << k); app_input_only(); }
__attribute__((export_name("sim_button"))) void sim_button(uint32_t b, int down) { if (down) held_btn |= 1u << b; else held_btn &= ~(1u << b); app_input_only(); }
__attribute__((export_name("sim_enc"))) void sim_enc(int32_t role, int32_t d) { enc_acc[role] += d; app_input_only(); }
__attribute__((export_name("sim_midi"))) void sim_midi(uint32_t pkt) { if (midi_n < 64) midi_q[midi_n++] = pkt; }
__attribute__((export_name("sim_get"))) int32_t sim_get(uint32_t what)
{
    switch (what) {
    case 0: return playing; case 1: return root_now; case 2: return pad_note; case 3: return (int32_t)bell_count;
    case 4: return last_bell; case 5: return set.mac[0]; case 6: return set.mac[1]; case 7: return set.mac[2]; case 8: return set.mac[3];
    case 9: return set.octave; case 10: return set_dirty;
    case 11: return (int32_t)say_seq;
    }
    return 0;
}
