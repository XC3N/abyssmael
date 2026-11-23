/* SPDX-License-Identifier: MPL-2.0 */
/* AMBIENT TEST: a first generative ambient firmware on fm1-base. A drone and random bells play from power-on;
 * four macro knobs steer them; the keys play a soft pad over the top. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "AMBIENT TEST"
#define APP_SPLASH_RGB RGB(90, 150, 220)
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */

/* the store: the app's settings and the panel calibration (the platform's), and their flash offsets */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}

/* the contract: the platform calls these */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

/* the host simulator's checks: playing, bells (bells started since boot), root, cpu */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
