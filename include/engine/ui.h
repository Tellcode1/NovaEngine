#ifndef __NOVA_UI_H__
#define __NOVA_UI_H__

// implementation: engine.c

#include "../../std/math/vec4.h"
#include "object.h"
#include "sprite.h"
#include "renderer.h"

NOVA_HEADER_START

typedef struct nvui_button nvui_button;
typedef struct nvui_slider nvui_slider;

typedef void (*nvui_button_on_click)(nvui_button* bton);
typedef void (*nvui_button_on_hover)(nvui_button* bton);

extern struct nvui_context nv_ui_ctx;

struct nvui_button
{
  nv_transform         m_transform;
  vec4f                m_color;
  nvui_button_on_hover m_on_hover;
  nvui_button_on_click m_on_click;
  nv_sprite*           m_spr;
  bool                 m_was_hovered; // was it being hovered in this frame?
  bool                 m_was_clicked; // was the button pressed?
};

struct nvui_slider
{
  nv_transform m_transform;
  vec4f        m_bg_color, m_slider_color;
  nv_sprite *  m_bg_sprite, *m_slider_sprite;
  flt_t        m_min, m_max, m_value;
  bool         m_moved;        // was the slider's handle moved
  bool         m_interactable; // whether this slider can be controlled by the mouse.
                               // default off
};

extern void nvui_init(void);
extern void nvui_shutdown(void);

extern nvui_button* nvui_create_button(nv_sprite* spr);

extern nvui_slider* nvui_create_slider(void);

extern void nvui_destroy_button(nvui_button* obj);
extern void nvui_destroy_slider(nvui_slider* obj);

extern void nvui_render(nv_renderer_t* rd);

extern void nvui_update(void);

NOVA_HEADER_END

#endif //__NOVA_UI_H__
