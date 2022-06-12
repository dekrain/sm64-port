#ifndef HITBOX_HACK_H
#define HITBOX_HACK_H

#include <PR/ultratypes.h>

#include "types.h"

#define SHOW_HURTBOXES 1

void update_triangle_hook(void);
void update_triangle_hook_start(void);
void obj_draw_hitboxes(u32 in_view, struct Object *node);
void hitbox_draw_main(void);

#endif // HITBOX_HACK_H
