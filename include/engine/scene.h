#ifndef ENGINE_SCENE_H
#define ENGINE_SCENE_H

#include "../std/include/stdafx.h"

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct nv_scene_t nv_scene_t;
  struct nv_renderer;
  struct nv_ctx;
  struct b2WorldId;

  typedef void (*nv_scene_load_fn)(nv_scene_t* scn);

  // Called when the scene scn changes
  // ie. it's called when scn is being unloaded
  typedef void (*nv_scene_unload_fn)(nv_scene_t* scn);

  extern nv_scene_t* scene_main;

  extern nv_scene_t*      nv_scene_init(void);
  extern void             nv_scene_update(const struct nv_ctx* ctx);
  extern void             nv_scene_render(struct nv_renderer* rd);
  extern void             nv_scene_destroy(nv_scene_t* scene);
  extern void             nv_scene_assign_load_fn(nv_scene_t* scene, nv_scene_load_fn fn);
  extern void             nv_scene_assign_unload_fn(nv_scene_t* scene, nv_scene_unload_fn fn);
  extern void             nv_scene_change_to_scene(nv_scene_t* scene);
  extern struct b2WorldId nv_scene_get_world_id(nv_scene_t* scene);

#ifdef __cplusplus
}
#endif

#endif // ENGINE_SCENE_H
