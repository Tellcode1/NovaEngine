#ifndef NOVA_UI_H
#define NOVA_UI_H

#include "../iris/types.h"
#include "../std/include/math/vec4.h"
#include "../std/include/stdafx.h"
#include "object.h"
#include "renderer.h"
#include "sprite.h"

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct nvui_button nvui_button;
  typedef struct nvui_slider nvui_slider;
  struct nv_input_ctx_t;

  typedef void (*nvui_button_on_click)(nvui_button* bton);
  typedef void (*nvui_button_on_hover)(nvui_button* bton);

  extern struct nvui_context nv_ui_ctx;

  struct nvui_button
  {
    nv_transform         transform;
    vec4                 color;
    nvui_button_on_hover on_hover;
    nvui_button_on_click on_click;
    nv_sprite_t*         spr;
    bool                 was_hovered; // was it being hovered in this frame?
    bool                 was_clicked; // was the button pressed?
  };

  struct nvui_slider
  {
    nv_transform transform;
    vec4         bg_color, slider_color;
    nv_sprite_t *bg_sprite, *slider_sprite;
    double       min, max, value;
    bool         moved;        // was the slider's handle moved
    bool         interactable; // whether this slider can be controlled by the mouse.
                               // default off
  };

  extern void nvui_init(void);
  extern void nvui_shutdown(void);

  extern nvui_button* nvui_create_button(nv_sprite_t* spr);

  nvui_slider* nvui_create_slider(nv_sprite_t* foreground, nv_sprite_t* background);

  extern void nvui_destroy_button(nvvk_ctx_t* vkctx, nvui_button* obj);
  extern void nvui_destroy_slider(nvvk_ctx_t* vkctx, nvui_slider* obj);

  extern void nvui_render(nv_renderer_t* rd);

  extern void nvui_update(struct nv_input_ctx_t* input);

#ifdef __cplusplus
}
#endif

#endif // NOVA_UI_H
