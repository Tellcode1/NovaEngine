#ifndef __NOVA_UI_H__
#define __NOVA_UI_H__

// implementation: engine.c

#include "../std/math/vec4.h"
#include "object.h"
#include "renderer.h"
#include "sprite.h"

NOVA_HEADER_START

typedef struct nvui_button nvui_button;
typedef struct nvui_slider nvui_slider;
struct nv_input_ctx_t;

typedef void (*nvui_button_on_click)(nvui_button* bton);
typedef void (*nvui_button_on_hover)(nvui_button* bton);

extern struct nvui_context nv_ui_ctx;

struct nvui_button
{
  nv_transform         transform;
  vec4f                color;
  nvui_button_on_hover on_hover;
  nvui_button_on_click on_click;
  nv_sprite*           spr;
  bool                 was_hovered; // was it being hovered in this frame?
  bool                 was_clicked; // was the button pressed?
};

struct nvui_slider
{
  nv_transform transform;
  vec4f        bg_color, slider_color;
  nv_sprite *  bg_sprite, *slider_sprite;
  flt_t        min, max, value;
  bool         moved;        // was the slider's handle moved
  bool         interactable; // whether this slider can be controlled by the mouse.
                             // default off
};

extern void nvui_init(void);
extern void nvui_shutdown(void);

extern nvui_button* nvui_create_button(nv_sprite* spr);

extern nvui_slider* nvui_create_slider(void);

extern void nvui_destroy_button(nvvk_ctx_t* nvvkctx, nvui_button* obj);
extern void nvui_destroy_slider(nvvk_ctx_t* nvvkctx, nvui_slider* obj);

extern void nvui_render(nv_renderer_t* rd);

extern void nvui_update(struct nv_input_ctx_t* input);

NOVA_HEADER_END

#endif //__NOVA_UI_H__
