#ifndef __NOVA_SCENE_H__
#define __NOVA_SCENE_H__

#include "../../common/stdafx.h"

NOVA_HEADER_START;

typedef struct nv_scene_t nv_scene_t;
typedef struct nv_renderer_t nv_renderer_t;

typedef void (*nv_scene_load_fn)(nv_scene_t *scn);

// Called when the scene scn changes
// ie. it's called when scn is being unloaded
typedef void (*nv_scene_unload_fn)(nv_scene_t *scn);

extern nv_scene_t *scene_main;

extern nv_scene_t *nv_scene_init();
extern void nv_scene_update();
extern void nv_scene_render(nv_renderer_t *rd);
extern void nv_scene_destroy(nv_scene_t *scene);
extern void nv_scene_assign_load_fn(nv_scene_t *scene, nv_scene_load_fn fn);
extern void nv_scene_assign_unload_fn(nv_scene_t *scene, nv_scene_unload_fn fn);
extern void nv_scene_change_to_scene(nv_scene_t *scene);

NOVA_HEADER_END;

#endif //__NOVA_SCENE_H__