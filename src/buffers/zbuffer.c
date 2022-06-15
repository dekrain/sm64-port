#include <ultra64.h>

#include "config.h"
#include "zbuffer.h"

ALIGNED8 u16 gZBuffer[SCREEN_WIDTH * SCREEN_HEIGHT];

/* 0x3E0000 */
ALIGNED8 Gfx dl_hitbox_level_dynamic[0x900 + 0x900 / 5];

/* 0x3E5500 */
ALIGNED8 Vtx vertex_hitbox_level[0x900 * 3];
