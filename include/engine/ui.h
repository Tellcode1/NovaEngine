#ifndef __NOVA_UI_H__
#define __NOVA_UI_H__

#include "../../common/math/vec4.h"
#include "object.h"
#include "sprite.h"

NOVA_HEADER_START;

typedef struct nvui_button nvui_button;
typedef struct nvui_slider nvui_slider;

typedef void (*nvui_button_on_click)(nvui_button *bton);
typedef void (*nvui_button_on_hover)(nvui_button *bton);

extern struct nvui_context nv_ui_ctx;

struct nvui_button {
  nv_transform transform;
  vec4 color;
  nvui_button_on_hover on_hover;
  nvui_button_on_click on_click;
  nv_sprite *spr;
  bool was_hovered; // was it being hovered in this frame?
  bool was_clicked; // was the button pressed?
};

struct nvui_slider {
  nv_transform transform;
  vec4 bg_color, slider_color;
  nv_sprite *bg_sprite, *slider_sprite;
  float min, max, value;
  bool moved;        // was the slider's handle moved
  bool interactable; // whether this slider can be controlled by the mouse.
                     // default off
};

extern void nvui_init();
extern void nvui_shutdown();

extern nvui_button *nvui_create_button(struct nv_sprite *spr);

extern nvui_slider *nvui_create_slider();

extern void nvui_destroy_button(nvui_button *obj);
extern void nvui_destroy_slider(nvui_slider *obj);

extern void nvui_render(nv_renderer_t *rd);

extern void nvui_update();

NOVA_HEADER_END;

#endif //__NOVA_UI_H__