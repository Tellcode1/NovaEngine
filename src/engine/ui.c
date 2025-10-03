#include "../../include/engine/ui.h"
#include "../../include/engine/camera.h"
#include "../../include/engine/input.h"
#include "../../include/engine/object.h"
#include "../../include/engine/renderer.h"
#include "../../include/engine/sprite.h"

#include "../../include/std/include/alloc.h"
#include "../../include/std/include/containers/list.h"
#include "../../include/std/include/errorcodes.h"
#include "../../include/std/include/math/math.h"
#include "../../include/std/include/math/vec2.h"
#include "../../include/std/include/math/vec3.h"
#include "../../include/std/include/stdafx.h"

#include <SDL3/SDL_mouse.h>
#include <stddef.h>
#include <stdint.h>

typedef struct nvui_context
{
  nv_list_t btons;
  nv_list_t sliders;
  void*     ubmapped;
  bool      active;
} nvui_context;

nvui_context nvui_ctx;

void
nvui_init(void)
{
  nvui_ctx.active = true;
  nv_list_init(sizeof(nvui_button), 4, nv_allocator_c, NULL, &nvui_ctx.btons);
  nv_list_init(sizeof(nvui_slider), 4, nv_allocator_c, NULL, &nvui_ctx.sliders);
}

void
nvui_shutdown(void)
{
  if (!nvui_ctx.active)
  {
    return;
  }
  nv_list_destroy(&nvui_ctx.btons);
  nv_list_destroy(&nvui_ctx.sliders);
  nvui_ctx.active = false;
}

nvui_button*
nvui_create_button(nv_sprite_t* spr)
{
  if (!nvui_ctx.active)
  {
    nv_raise_error(NV_ERROR_BROKEN_STATE, "nvui not initialized\n");
    return NULL;
  }
  nvui_button bton        = nv_zero_init(nvui_button);
  bton.transform.position = v2zero;
  bton.transform.size     = (vec2){ 0.5f, 0.5f };
  bton.color              = (vec4){ 1.0f, 1.0f, 1.0f, 1.0f };
  bton.spr                = spr;
  nv_list_push_back(&nvui_ctx.btons, &bton);
  return &((nvui_button*)nv_list_data(&nvui_ctx.btons))[nv_list_size(&nvui_ctx.btons) - 1];
}

nvui_slider*
nvui_create_slider(nv_sprite_t* foreground, nv_sprite_t* background)
{
  if (!nvui_ctx.active)
  {
    nv_log_error("nvui not initialized\n");
    return NULL;
  }
  nvui_slider slider        = nv_zero_init(nvui_slider);
  slider.transform.position = v2zero;
  slider.transform.size     = (vec2){ 0.5f, 1.5f };
  slider.min                = 0.0f;
  slider.max                = 1.0f;
  slider.value              = 0.0f;
  slider.bg_color           = (vec4){ 1.0f, 1.0f, 1.0f, 1.0f };
  slider.slider_color       = (vec4){ 1.0f, 0.0f, 0.0f, 1.0f };
  slider.bg_sprite          = foreground;
  slider.slider_sprite      = background;
  slider.interactable       = false;
  nv_list_push_back(&nvui_ctx.sliders, &slider);
  return (nvui_slider*)nv_list_get(&nvui_ctx.sliders, nv_list_size(&nvui_ctx.sliders) - 1);
}

void
nvui_destroy_button(nvui_button* obj)
{
  if (obj == NULL)
  {
    return;
  }
  nv_sprite_release(obj->spr);
}

void
nvui_destroy_slider(nvui_slider* obj)
{
  if (obj == NULL)
  {
    return;
  }
  nv_sprite_release(obj->slider_sprite);
  nv_sprite_release(obj->bg_sprite);
}

void
nvui_render(nv_renderer_t* rd)
{
  if (!nvui_ctx.active)
  {
    return;
  }
  for (int i = 0; i < (int)nv_list_size(&nvui_ctx.btons); i++)
  {
    const nvui_button* bton = (nvui_button*)nv_list_get(&nvui_ctx.btons, i);

    const nv_transform* t = &bton->transform;

    nv_renderer_render_quad(rd, bton->spr, (vec2){ 1.0f, 1.0f }, (vec3){ t->position.x, t->position.y, 0.0f }, (vec3){ t->size.x, t->size.y, 1.0f }, bton->color, 0);
  }

  for (int i = 0; i < (int)nv_list_size(&nvui_ctx.sliders); i++)
  {
    const nvui_slider* slider = (nvui_slider*)nv_list_get(&nvui_ctx.sliders, i);

    if (slider->max == slider->min)
    {
      nv_raise_error(NV_ERROR_INVALID_OPERATION, "Slider %i has equal min and max\n", i);
      continue;
    }

    const nv_transform* t = &slider->transform;

    nv_renderer_render_quad(
        rd, slider->bg_sprite, (vec2){ 1.0f, 1.0f }, (vec3){ t->position.x, t->position.y, 0.0f }, (vec3){ t->size.x, t->size.y, 1.0f }, slider->bg_color, 0);

    double pcent = ((slider->value - slider->min) / (slider->max - slider->min));
    pcent        = NVM_CLAMP(pcent, 0.0f, 1.0f);

    nv_renderer_render_quad(
        rd,
        slider->slider_sprite,
        (vec2){ 1.0f, 1.0f },
        (vec3){ t->position.x + 0.5f * t->size.x * (pcent - 1.0f), t->position.y, 0.0f },
        (vec3){ t->size.x * pcent, t->size.y, 1.0f },
        slider->slider_color,
        1);
  }
}

void
nvui_update(nv_input_ctx_t* inputctx)
{
  const bool mouse_pressed  = nv_input_is_mouse_just_signalled(inputctx, (nv_input_mouse_button)SDL_BUTTON_LEFT);
  const vec2 mouse_position = nv_camera_get_global_mouse_position(inputctx, &camera);

  for (int i = 0; i < (int)nv_list_size(&nvui_ctx.btons); i++)
  {
    nvui_button*        bton = (nvui_button*)nv_list_get(&nvui_ctx.btons, i);
    const nv_transform* t    = &bton->transform;

    const nvm_rect2d bton_rect = (nvm_rect2d){ .position = t->position, .size = v2muls(t->size, 2.0f) };
    if (nvm_is_point_inside_rect(&mouse_position, &bton_rect))
    {
      bton->was_hovered = true;
      if (mouse_pressed && (bton->on_click != NULL))
      {
        bton->was_clicked = true;
        bton->on_click(bton);
      }
      else if (bton->on_hover != NULL)
      {
        bton->was_clicked = false;
        bton->on_hover(bton);
      }
    }
    else
    {
      bton->was_hovered = false;
      bton->was_clicked = false;
    }
  }

  for (int i = 0; i < (int)nv_list_size(&nvui_ctx.sliders); i++)
  {
    nvui_slider*        slider = (nvui_slider*)nv_list_get(&nvui_ctx.sliders, i);
    const nv_transform* t      = &slider->transform;

    const nvm_rect2d slider_rect = (nvm_rect2d){ .position = t->position, .size = v2muls(t->size, 2.0f) };
    if (slider->interactable && nvm_is_point_inside_rect(&mouse_position, &slider_rect))
    {
      if (nv_input_is_mouse_signalled(inputctx, NOVA_MOUSE_BUTTON_LEFT))
      {
        double rel_mx     = mouse_position.x - (t->position.x - t->size.x * 0.5);
        double clamped_mx = NVM_CLAMP(rel_mx, 0.0f, t->size.x);
        double percentage = clamped_mx / t->size.x;
        slider->value     = slider->min + (percentage * (slider->max - slider->min));
        slider->moved     = true;
      }
      else
      {
        slider->moved = false;
      }
    }
  }
}
