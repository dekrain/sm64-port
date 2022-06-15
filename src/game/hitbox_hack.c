// Hitbox hack by @dkorpel (https://gist.github.com/dkorpel/dc7c435bf937fe886b67bfb51b7ec43a)
// Ported by @DeKrain in C

#include "behavior_data.h"
#include "game/game_init.h"
#include "engine/graph_node.h"
#include <PR/ultratypes.h>
#include <PR/gbi.h>

#include "engine/math_util.h"
#include "game/hitbox_hack.h"
#include "game/memory.h"
#include "object_list_processor.h"
#include "object_helpers.h"
#include "object_fields.h"
#include "rendering_graph_node.h"
#include "sm64.h"
#include "surface_terrains.h"
#include "types.h"
#include "engine/surface_load.h"
#include "engine/graph_node.h"

#ifndef TARGET_N64
#include <stdio.h>
#include <stdlib.h>
#endif

// External data segment declarations
extern const Gfx dl_hitbox_level[];
extern const Gfx dl_hitbox_cylinder[];
#if SHOW_HURTBOXES
extern const Gfx dl_hurtbox_cylinder[];
#endif

extern Gfx dl_hitbox_level_dynamic_pointer;

extern Gfx dl_hitbox_level_dynamic[0x900 + 0x900 / 5];
extern Vtx vertex_hitbox_level[0x900 * 3];

#ifndef TARGET_N64
// To detect overflow
static Gfx* const dl_hitbox_level_dynamic_end = dl_hitbox_level_dynamic + ARRAY_COUNT(dl_hitbox_level_dynamic);
static Vtx* const vertex_hitbox_level_end = vertex_hitbox_level + ARRAY_COUNT(vertex_hitbox_level);
#endif

/* 0x3D6FF0 */
static s32 s_mystatcount = -1;
/* 0x3D6FF4 */
static u32 s_cylinder_enabled = TRUE;
/* 0x3D6FF8*/
static u32 s_level_enabled = TRUE;
/* 0x3D6FFC */
static u32 s_previous_enable = TRUE;

// Referenced environment
extern s16 gMatStackIndex;
extern Mat4 gMatStack[32];
extern Mtx *gMatStackFixed[32];
//extern const BehaviorScript D_800EDBC8[];

#ifdef USE_SYSTEM_MALLOC
extern struct AllocOnlyPool *sStaticSurfacePool;
extern struct AllocOnlyPool *sDynamicSurfacePool;
#endif

extern void geo_append_display_list(void *displayList, s16 layer);

/* 0x3D6ED0 */
static u8 s_hitbox_object_groups[] = {
	1, 0, 1, 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0,
};

// Lookup table for G_TRI commands, 5 possibilities 

#if defined(F3D_OLD) || defined(F3D_NEW)
#define USE_F3D_LUT
// Use the prebaked lookup table
/* 0x3D6EE0 */
static u32 s_tri_lut[] = {
	0x000A14,
	0x1E2832,
	0x3C4650,
	0x5A646E,
	0x78828C,
};
#else
// Calculating the indices programatically is probably faster
#endif

/* 0x3D7000 */
void update_triangle_hook(void) {
	if (s_level_enabled) {
		gCurrentObject->header.gfx.node.flags |= GRAPH_RENDER_INVISIBLE;
	}
}

void update_triangle_hook_start(void) {
	gCurrentObject->header.gfx.node.flags &= ~GRAPH_RENDER_INVISIBLE;
}

// Declarations
static void draw_level_triangles(u32 prev_enable);
void obj_draw_hitboxes(u32 in_view, struct Object *node);
static void set_landscape_invisible(u32 enable);
void hitbox_draw_main(void);
static void set_displaylists_visible(struct GraphNode *node, u32 enable);
static void add_triangle_to_vertexbuffer(struct Surface *triangle, Vtx destination[3]);

static void detect_overflow_dl(UNUSED Gfx* listpointer) {
	#ifndef TARGET_N64
	if (listpointer >= dl_hitbox_level_dynamic_end) {
		fputs("Level collision display list got overflown!\n", stderr);
		abort();
	}
	#endif
}

static void detect_overflow_vtx(UNUSED Vtx* vertexpointer) {
	#ifndef TARGET_N64
	if (vertexpointer >= vertex_hitbox_level_end) {
		fputs("Level collision vertices got overflown!\n", stderr);
		abort();
	}
	#endif
}

static void draw_level_triangles(u32 prev_enable) {
    u32 lastindex, offset, totalcount;
    Gfx *listpointer;
    struct Surface *triangle;
    Vtx *vertexpointer;

	if (!prev_enable) {
		return;
	}
	#ifdef USE_SYSTEM_MALLOC
	return;
	// :C no fun
	#endif

	lastindex = gNumStaticSurfaces;
	if ((s32)lastindex != s_mystatcount) {
		s_previous_enable = FALSE;
		s_mystatcount = lastindex;
		lastindex = 0;
	}

	offset = lastindex % 5;
	listpointer = (Gfx *)segmented_to_virtual(dl_hitbox_level_dynamic) + lastindex + lastindex / 5;

	//is THIS *the* fix of ages?
	// SPOILER: probably no
	if (offset != 0) {
		++listpointer;
	}
	detect_overflow_dl(listpointer);

	totalcount = gSurfacesAllocated;
	#ifdef USE_SYSTEM_MALLOC
	triangle = 0 /*sStaticSurfacePool-> + lastindex*/;
	#else
	triangle = sSurfacePool + lastindex;
	#endif
	vertexpointer = (Vtx *)segmented_to_virtual(vertex_hitbox_level) + 3 * lastindex;

	while (lastindex < totalcount && lastindex < 0x8D0) {
		if (offset == 0) {
			gSPVertex(listpointer++, vertexpointer, 0xF, 0);
			detect_overflow_dl(listpointer);
		}
		#ifdef USE_F3D_LUT
		gDma0p(listpointer++, G_TRI1, s_tri_lut[offset], 0);
		#else
		gSP1Triangle(listpointer++, 3 * offset + 0, 3 * offset + 1, 3 * offset + 2, 0);
		#endif
		detect_overflow_dl(listpointer);
		detect_overflow_vtx(vertexpointer + 3);
		add_triangle_to_vertexbuffer(triangle, vertexpointer);
		++lastindex;
		vertexpointer += 3;
		++triangle;
		if (++offset == 5) {
			offset = 0;
		}
	}

	detect_overflow_dl(listpointer + 3);
	gSPEndDisplayList(listpointer++);
	gSPNoOp(listpointer++);
	gSPNoOp(listpointer++);
	gSPNoOp(listpointer++);
	gSPBranchList(segmented_to_virtual(&dl_hitbox_level_dynamic_pointer), dl_hitbox_level_dynamic);
	geo_append_display_list(segmented_to_virtual(dl_hitbox_level), LAYER_OPAQUE);
}

void obj_draw_hitboxes(u32 in_view, struct Object *node) {
	f32 x, y, z, height, radius;
	Mat4 mtx, *stack;
	Mtx *mat;

	if (!in_view) {
		return;
	}

	if (!s_cylinder_enabled) {
		return;
	}

	// FIXED: No crash when object has no behavior (mirror Mario)
	if (!node->behavior) {
		#ifndef TARGET_N64
		//printf("Object has no behavior attached. Its current command pointer is %p\n", node->curBhvCommand);
		#endif
		return;
	}

	if (!s_hitbox_object_groups[get_object_list_from_behavior(node->behavior)]) {
		return;
	}

	// Draw cylinder
	x = node->oPosX;
	z = node->oPosZ;
	y = node->oPosY - node->hitboxDownOffset;
	height = node->hitboxHeight * /*0x1p-10f*/ (1.0f/1024);
	radius = node->hitboxRadius * /*0x1p-10f*/ (1.0f/1024);
	mtx[0][0] = radius;
	mtx[0][1] = 0;
	mtx[0][2] = 0;
	mtx[0][3] = 0;
	mtx[1][0] = 0;
	mtx[1][1] = height;
	mtx[1][2] = 0;
	mtx[1][3] = 0;
	mtx[2][0] = 0;
	mtx[2][1] = 0;
	mtx[2][2] = radius;
	mtx[2][3] = 0;
	mtx[3][0] = x;
	mtx[3][1] = y;
	mtx[3][2] = z;
	mtx[3][3] = 1.0f;

	stack = &gMatStack[gMatStackIndex];
	mtxf_mul(stack[1], mtx, stack[0]);
	++gMatStackIndex;
	mat = alloc_display_list(sizeof(Mtx));
	gMatStackFixed[gMatStackIndex] = mat;
	mtxf_to_mtx(mat, stack[1]);
	geo_append_display_list(segmented_to_virtual(dl_hitbox_cylinder), LAYER_TRANSPARENT);
	--gMatStackIndex;

	#if SHOW_HURTBOXES
	height = node->hurtboxHeight * /*0x1p-10f*/ (1.0f/1024);
	radius = node->hurtboxRadius * /*0x1p-10f*/ (1.0f/1024);
	mtx[0][0] = radius;
	mtx[1][1] = height;
	mtx[2][2] = radius;

	stack = &gMatStack[gMatStackIndex];
	mtxf_mul(stack[1], mtx, stack[0]);
	++gMatStackIndex;
	mat = alloc_display_list(sizeof(Mtx));
	gMatStackFixed[gMatStackIndex] = mat;
	mtxf_to_mtx(mat, stack[1]);
	geo_append_display_list(segmented_to_virtual(dl_hurtbox_cylinder), LAYER_TRANSPARENT);
	--gMatStackIndex;
	#endif
}

static void set_landscape_invisible(u32 enable) {
	struct Object *head = (struct Object *)(gObjectLists + 8);
	struct Object *current = head;

	while ((current = (struct Object *)current->header.next) != head) {
		if (current->behavior != segmented_to_virtual(bhvStaticObject)) {
			continue;
		}
		if (enable) {
			current->header.gfx.node.flags |= GRAPH_RENDER_INVISIBLE;
		} else {
			current->header.gfx.node.flags &= ~GRAPH_RENDER_INVISIBLE;
		}
	}
}

void hitbox_draw_main(void) {
	draw_level_triangles(s_previous_enable);
	//if (gPlayer1Controller->buttonDown & L_TRIG) {
		if (gPlayer1Controller->buttonPressed & U_JPAD) {
			s_cylinder_enabled ^= 1;
		}
		if (gPlayer1Controller->buttonPressed & D_JPAD) {
			s_level_enabled ^= 1;
		}
	//}
	if (s_level_enabled != s_previous_enable) {
		s_previous_enable = s_level_enabled;
		// gCurrentArea?.unk04?.node.children?.next?.children?.children
		if (gCurrentArea && gCurrentArea->unk04 && gCurrentArea->unk04->node.children && gCurrentArea->unk04->node.children->next
			&& gCurrentArea->unk04->node.children->next->children && gCurrentArea->unk04->node.children->next->children->children) {

			set_displaylists_visible(gCurrentArea->unk04->node.children->next->children->children->children, s_level_enabled);
		}
		set_landscape_invisible(s_level_enabled);
	}
}

static void set_displaylists_visible(struct GraphNode *head, u32 disable) {
	struct GraphNode *node = head;
	if (!head) {
		return;
	}
	do {
		if (node->type == GRAPH_NODE_TYPE_SWITCH_CASE || node->type == GRAPH_NODE_TYPE_SCALE || node->type == GRAPH_NODE_TYPE_START) {
			set_displaylists_visible(node->children, disable);
			continue;
		}
		if (node->type != GRAPH_NODE_TYPE_DISPLAY_LIST) {
			continue;
		}
		if (disable) {
			node->flags &= ~GRAPH_RENDER_ACTIVE;
		} else {
			node->flags |= GRAPH_RENDER_ACTIVE;
		}
	} while ((node = node->next) != head);
}

#ifndef GBI_FLOATS
#define vec3g_set vec3s_set
#define vec3g_copy vec3s_copy
#else
#define vec3g_set vec3f_set
static inline void vec3g_copy(Vec3f dst, Vec3s src) {
	dst[0] = src[0];
	dst[1] = src[1];
	dst[2] = src[2];
}
#endif

static void add_triangle_to_vertexbuffer(struct Surface *triangle, Vtx destination[3]) {
	u32 idx;
	f64 ny;
	u8 out_r, out_g, out_b;
	u16 const tc0 = 0x10, tc1 = 0x3F0;

	if (triangle->type == SURFACE_BURNING) {
		vec3g_set(destination[0].v.ob, 0, 0, 0);
		vec3g_set(destination[1].v.ob, 0, 0, 0);
		vec3g_set(destination[2].v.ob, 0, 0, 0);
	} else {
		vec3g_copy(destination[0].v.ob, triangle->vertex1);
		vec3g_copy(destination[1].v.ob, triangle->vertex2);
		vec3g_copy(destination[2].v.ob, triangle->vertex3);
	}
	destination[0].v.flag = 0;
	destination[1].v.flag = 0;
	destination[2].v.flag = 0;

	destination[0].v.tc[0] = tc0;
	destination[0].v.tc[1] = tc1;
	destination[1].v.tc[0] = tc0;
	destination[1].v.tc[1] = tc1;
	if (triangle->type == SURFACE_DEFAULT) {
		destination[2].v.tc[0] = tc1;
		destination[2].v.tc[1] = tc1;
	} else {
		destination[2].v.tc[0] = tc0;
		destination[2].v.tc[1] = tc0;
	}
	// Determine surface type
	// normal y > 0.01 => floor
	// normal y < -0.01 => ceiling
	// otherwise wall
	ny = triangle->normal.y;
	if (ny > 0.01) {
		// Floor
		f32 ynorm = ny + 0.5f;
		f32 sat = ynorm * 170.0f;
		f32 undersat = ynorm * 96.0f;
		out_r = (u32)undersat;
		out_g = (u32)undersat;
		out_b = (u32)sat;
	} else if (ny < -0.01) {
		// Ceiling
		f32 ynorm = -ny + 0.5f;
		f32 sat = ynorm * 170.0f;
		f32 undersat = ynorm * 96.0f;
		out_r = (u32)sat;
		out_g = (u32)undersat;
		out_b = (u32)undersat;
	} else {
		// Wall
		if ((triangle->flags & SURFACE_FLAG_X_PROJECTION) == 0) {
			// Light green
			out_r = 0x80;
			out_g = 0xE0;
			out_b = 0x80;
		} else {
			// Dark green
			out_r = 0x40;
			out_g = 0x80;
			out_b = 0x40;
		}
	}
	for (idx = 0; idx != 3; ++idx) {
		destination[idx].v.cn[0] = out_r;
		destination[idx].v.cn[1] = out_g;
		destination[idx].v.cn[2] = out_b;
		destination[idx].v.cn[3] = 0xFF;
	}
}
