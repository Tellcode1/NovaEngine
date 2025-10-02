#include "../include/iris/types.h"
#include "../include/std/include/math/vec4.h"
#include "../include/std/include/types.h"
#include <SDL3/SDL_events.h>

#include "../include/iris/driver.h"
#include "../include/shadersystem/nvsm.h"

#include "../include/engine/camera.h"
#include "../include/engine/ctext.h"
#include "../include/engine/engine.h"
#include "../include/engine/input.h"
#include "../include/engine/renderer.h"
#include "../include/engine/sprite.h"

#include "../include/std/include/chrclass.h"
#include "../include/std/include/errorcodes.h"
#include "../include/std/include/print.h"
#include "../include/std/include/props.h"
#include "../include/std/include/stdafx.h"
#include "../include/std/include/timer.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline void
simple_camera_movement_controller(double dt, void* arg)
{
  const nv_input_ctx_t* inputctxp = (nv_input_ctx_t*)arg;

  vec3  camera_movement_aggregate = (vec3){};
  float movespeed                 = 50.0;

  if (nv_input_is_key_signalled(inputctxp, SDL_SCANCODE_UP))
  {
    camera_movement_aggregate = v3add(camera_movement_aggregate, (vec3){ 0.0, movespeed, 0.0 });
  }
  if (nv_input_is_key_signalled(inputctxp, SDL_SCANCODE_DOWN))
  {
    camera_movement_aggregate = v3add(camera_movement_aggregate, (vec3){ 0.0, -movespeed, 0.0 });
  }
  if (nv_input_is_key_signalled(inputctxp, SDL_SCANCODE_LEFT))
  {
    camera_movement_aggregate = v3add(camera_movement_aggregate, (vec3){ -movespeed, 0.0, 0.0 });
  }
  if (nv_input_is_key_signalled(inputctxp, SDL_SCANCODE_RIGHT))
  {
    camera_movement_aggregate = v3add(camera_movement_aggregate, (vec3){ movespeed, 0.0, 0.0 });
  }
  if (nv_input_is_key_signalled(inputctxp, SDL_SCANCODE_W))
  {
    camera_movement_aggregate = v3add(camera_movement_aggregate, (vec3){ 0.0, 0.0, movespeed / 3.0 });
  }
  if (nv_input_is_key_signalled(inputctxp, SDL_SCANCODE_S))
  {
    camera_movement_aggregate = v3add(camera_movement_aggregate, (vec3){ 0.0, 0.0, -movespeed / 3.0 });
  }
  nv_camera_move(&camera, v3muls(camera_movement_aggregate, (float)dt));
}

int
main(int argc, char* argv[])
{
  char        windowname[64]          = "clocker";
  int         window_width            = 800;
  int         window_height           = 600;
  bool        force_recompile_shaders = false, window_is_resizable = false;
  nv_option_t options[] = {
    { NV_OP_TYPE_STRING, "wn", "window-name", windowname, sizeof(windowname) },
    { NV_OP_TYPE_INT, "ww", "window-width", &window_width, 0 },
    { NV_OP_TYPE_INT, "wh", "window-height", &window_height, 0 },
    { NV_OP_TYPE_BOOL, NULL, "force-recompile-shaders", &force_recompile_shaders, 0 },
    { NV_OP_TYPE_BOOL, "rw", "resizable-window", &window_is_resizable, 0 },
  };

  char error[256];
  if (nv_props_parse(argc, argv, options, nv_arrlen(options), error, sizeof(error)) == -1)
  {
    nv_log_error("PROPS error: %s\n", error);
    nv_props_gen_help(options, nv_arrlen(options), error, nv_arrlen(error));
    nv_printf("%s\n", error);
  }

  nv_timer_t const tm = nv_timer_begin(0.1);

  const nv_extent2d window_size = (nv_extent2d){ (size_t)window_width, (size_t)window_height };

  nv_ctx_t       ctx      = nv_zero_init(nv_ctx_t);
  nvvk_ctx_t     vkctx    = nv_zero_init(nvvk_ctx_t);
  iris_driver_t  driver   = nv_zero_init(iris_driver_t);
  nv_renderer_t  rdr      = nv_zero_init(nv_renderer_t);
  nv_input_ctx_t inputctx = nv_zero_init(nv_input_ctx_t);
  nvsm_ctx_t     nvsmctx  = nv_zero_init(nvsm_ctx_t);

  nv_window_init(windowname, (int)window_size.width, (int)window_size.height, &ctx);

  nv_error code = NV_SUCCESS;

  // If you're wondering why every Action has a +,
  // I want to create a resource system with info about everything like bindings
  // It'll be used to serialize a save, for example
  // and etc. and every event will have a preceding +, booleans will have a 0
  // and integers will have an i I'll drop the + (the user won't have to add it)
  // when i get to it
  if ((code = nv_input_init(&ctx, &inputctx)) != NV_SUCCESS)
  {
    return code;
  }

  if ((code = nvvk_ctx_init(&ctx, &vkctx)) != NV_SUCCESS)
  {
    return code;
  }

  nvsmctx.list_file = "shaders/shaderlist";
  if ((code = nvsm_init(&nvsmctx)) != NV_SUCCESS)
  {
    return code;
  }

  if (force_recompile_shaders)
  {
    nvsm_compile_shaders_force(&nvsmctx, true);
  }
  else
  {
    nvsm_compile_shaders(&nvsmctx);
  }

  if ((code = nvsm_create_shader_modules((nvvk_ctx_t*)&vkctx, &nvsmctx)) != NV_SUCCESS)
  {
    return code;
  }

  nv_renderer_config rdconf   = nv_renderer_config_init();
  rdconf.vsync_enabled        = false;
  rdconf.buffer_mode          = NOVA_BUFFER_MODE_TRIPLE_BUFFERED;
  rdconf.window_resizable     = window_is_resizable;
  rdconf.initial_window_size  = window_size;
  rdconf.multisampling_enable = false;
  rdconf.samples              = NOVA_SAMPLE_COUNT_32_SAMPLES;

  if ((code = iris_driver_init(&vkctx, &driver)) != NV_SUCCESS)
  {
    return code;
  }

  iris_begin_upload_batch(&driver);

  if ((code = nv_renderer_init(&ctx, &nvsmctx, &driver, &rdconf, &rdr)) != NV_SUCCESS)
  {
    nv_log_error("Fatal error in initializing renderer (error:%s)\n", nv_error_str(code));
    return code;
  }

  const double updateTime = 3.0; // seconds. 1.5f = 1.5 seconds
  double       totalTime  = 0.0;
  u32 const    numFrames  = 0;

  cfont_t amongus = nv_zero_init(cfont_t);

  nv_log_info("Initialized in %fs\n", nv_timer_time_since_start(&tm));

  ctext_load_font(&vkctx, &rdr, "Assets/roboto.ttf", 64, &amongus);

  nv_sprite_t angwy = nv_zero_init(nv_sprite_t);
  if ((code = nv_sprite_load_from_disk(&driver, "Assets/i want to die.png", &angwy)) != NV_SUCCESS)
  {
    return code;
  }

  iris_end_upload_batch(&driver);

  nv_ctx_register_fixed_update(&ctx, simple_camera_movement_controller, (void*)&inputctx);

  const char* buff = "It's a red guitar.";

  while (nv_ctx_running(&ctx))
  {
    nv_update(&ctx);
    nv_input_update(&inputctx, &ctx);
    const double dt = nv_ctx_get_delta_time(&ctx);

    const vec4 background = (vec4){ 0.1, 0.05, 0.1, 1.0F };
    if (nv_renderer_begin(&rdr, background))
    {
      iris_begin_upload_batch(&driver);

      ctext_text_render_info_t i = ctext_init_text_render_info();
      // i.bbox                     = (vec2){ camera.ortho_size.x, camera.ortho_size.y };
      // i.scale_for_fit            = true;
      i.position.x = 0.0F;
      i.position.y = 0.0F;
      ctext_render(&amongus, &i, "%s", buff);

      nv_renderer_render_quad(&rdr, &angwy, v2init(1, 1), v3zero, v3init(100.0, 100.0, 1.0), v4init(1.0, 0.0, 0.0, 0.0), 0);

      vec3       camera_position = camera.position;
      const vec3 axes_begin      = v3init(camera_position.x, camera_position.y, 0.0F);
      const vec3 x_axis_end      = v3init(100.0, 0.0, 0.0);
      const vec3 y_axis_end      = v3init(0.0, 100.0, 0.0);
      nv_renderer_render_line(&rdr, axes_begin, v3add(y_axis_end, axes_begin), v4init(0.0F, 1.0F, 0.0F, 1.0F), 0); // Y axis: green
      nv_renderer_render_line(&rdr, axes_begin, v3add(x_axis_end, axes_begin), v4init(1.0F, 0.0F, 0.0F, 1.0F), 0); // X axis: red

      nv_renderer_render_quad(&rdr, &angwy, v2init(1.0, 1.0), v3zero, v3init(100.0, 100.0, 0.0), v4init(1.0, 1.0, 1.0, 1.0), 0);
      i.position.x = camera.ortho_size.x;
      i.position.y = camera.ortho_size.y;
      i.horizontal = CTEXT_HORI_ALIGN_LEFT;
      i.vertical   = CTEXT_VERT_ALIGN_CENTER;
      i.color      = v4one;
      ctext_render(&amongus, &i, "%d", (int)(1.0 / dt));

      iris_end_upload_batch(&driver);

      nv_renderer_end(&rdr);
    }
  }

  nv_sprite_destroy(&vkctx, &angwy);
  ctext_destroy_font(&vkctx, &amongus);

  nv_input_shutdown(&inputctx);
  nvsm_shutdown(&vkctx, &nvsmctx);
  nv_renderer_destroy(&rdr);
  iris_driver_destroy(&driver);
  nvvk_ctx_destroy(&vkctx);
  nv_window_shutdown(&ctx);

  return 0;
}
