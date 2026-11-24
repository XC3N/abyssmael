/* SPDX-License-Identifier: MPL-2.0 */
/* ABYSSMAEL: a generative ambient firmware on fm1-base. Six tracks, each with its own instrument, envelope, filter,
 * two LFOs, a generator and an arpeggio; a global delay, a shimmer reverb and an AGE layer (hiss, crackle, wow). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "ABYSSMAEL"
#define APP_SPLASH_RGB RGB(90, 150, 220)
/* the splash's second line: the version. scripts/build-fw.sh rewrites AMB_BUILD_TAG to "v<src/VERSION> [label] <commit>";
 * a plain build keeps this default. Bump src/VERSION for a release. */
#define AMB_BUILD_TAG "v0.5"
#define APP_TAGLINE AMB_BUILD_TAG
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */

/* the store: the four scenes, and the panel calibration (the platform's) */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
