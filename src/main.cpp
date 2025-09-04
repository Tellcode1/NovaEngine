#define SDL_MAIN_HANDLED

#include "GPU/driver.hpp"

#include "engine/camera.hpp"
#include "engine/ctext.hpp"
#include "engine/engine.h"
#include "engine/input.h"
#include "engine/renderer.hpp"
#include "engine/sprite.hpp"
#include "vk/nvsm.hpp"

#include "std/errorcodes.h"
#include "std/print.h"
#include "std/props.h"
#include "std/stdafx.h"
#include "std/timer.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct layer
{
  size_t width;
  size_t height;
  size_t order;
  uchar* pixels;
} layer_t;

typedef struct canvas
{
  size_t        width;
  size_t        height;
  struct layer* layers;
  size_t        num_layers;
} canvas_t;

int
main(int argc, char* argv[])
{
  char        windowname[64]          = "clocker";
  int         window_width            = 800;
  int         window_height           = 600;
  bool        force_recompile_shaders = false, resizable_window = false;
  nv_option_t options[] = {
    { NV_OP_TYPE_STRING, "wn", "window-name", windowname, sizeof(windowname) },
    { NV_OP_TYPE_INT, "ww", "window-width", &window_width, 0 },
    { NV_OP_TYPE_INT, "wh", "window-height", &window_height, 0 },
    { NV_OP_TYPE_BOOL, NULL, "force-recompile-shaders", &force_recompile_shaders, 0 },
    { NV_OP_TYPE_BOOL, "rw", "resizable-window", &resizable_window, 0 },
  };

  char error[256];
  if (nv_props_parse(argc, argv, options, nv_arrlen(options), error, sizeof(error)) == -1)
  {
    nv_log_error("PROPS error: %s\n", error);
    nv_props_gen_help(options, nv_arrlen(options), error, nv_arrlen(error));
    nv_printf("%s\n", error);
  }

  nv_timer_t tm = nv_timer_begin(0.1);

  const nv_extent2d window_size = (nv_extent2d){ (size_t)window_width, (size_t)window_height };

  nv_ctx_t       ctx      = nv_zero_init(nv_ctx_t);
  nvvk_ctx_t     vkctx    = nv_zero_init(nvvk_ctx_t);
  nvvk_driver    driver   = nv_zero_init(nvvk_driver);
  nv_renderer_t  rdr      = nv_zero_init(nv_renderer_t);
  nv_input_ctx_t inputctx = nv_zero_init(nv_input_ctx_t);
  lr::nvsm_ctx_t nvsmctx  = nv_zero_init(lr::nvsm_ctx_t);

  nv_window_init(windowname, (int)window_size.width, (int)window_size.height, &ctx);

  nv_error code = NV_SUCCESS;

  if ((code = nvvk_ctx_init(&ctx, &vkctx)) != NV_SUCCESS)
  {
    return code;
  }

  nvsmctx.list_file = "Shaders/shaderlist";
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

  if ((code = lr::nvsm_create_shader_modules((nvvk_ctx_t*)&vkctx, &nvsmctx)) != NV_SUCCESS)
  {
    return code;
  }

  nv_renderer_config rdconf   = nv_renderer_config_init();
  rdconf.vsync_enabled        = true;
  rdconf.buffer_mode          = NOVA_BUFFER_MODE_TRIPLE_BUFFERED;
  rdconf.window_resizable     = resizable_window;
  rdconf.initial_window_size  = window_size;
  rdconf.multisampling_enable = false;
  rdconf.samples              = NOVA_SAMPLE_COUNT_1_SAMPLES;

  if ((code = nvvk_driver_init(&vkctx, &driver)) != NV_SUCCESS)
  {
    return code;
  }

  if ((code = nv_renderer_init(&ctx, &nvsmctx, &driver, &rdconf, &rdr)) != NV_SUCCESS)
  {
    nv_log_error("Fatal error in initializing renderer (error:%s)\n", nv_error_str(code));
    return code;
  }

  // If you're wondering why every Action has a +,
  // I want to create a resource system with info about everything like bindings
  // It'll be used to serialize a save, for example
  // and etc. and every event will have a preceding +, booleans will have a 0
  // and integers will have an i I'll drop the + (the user won't have to add it)
  // when i get to it
  if ((code = nv_input_init(&inputctx)) != NV_SUCCESS)
  {
    return code;
  }

  const real_t updateTime = 3.0; // seconds. 1.5f = 1.5 seconds
  real_t       totalTime  = 0.0;
  u32          numFrames  = 0;

  cfont_t amongus = nv_zero_init(cfont_t);

  int curr_showing_fps = 0;

  nv_log_info("Initialized in %fs\n", nv_timer_time_since_start(&tm));

  ctext_load_font(&vkctx, &rdr, "Assets/roboto.ttf", 64, &amongus);

  nv_sprite_t angwy = nv_zero_init(nv_sprite_t);
  if ((code = nv_sprite_load_from_disk(&driver, "Assets/i want to die.png", &angwy)) != NV_SUCCESS)
  {
    return code;
  }

  while (nv_running(&ctx))
  {
    nv_update(&ctx);
    const real_t dt = nv_get_delta_time(&ctx);

    SDL_Event event;
    nv_input_update(&inputctx, &ctx);

    nv_camera_update(&camera, &rdr);

    while (SDL_PollEvent(&event))
    {
      nv_consume_event(&ctx, &event);
    }

    const vec4 background = (vec4){ 0.0F, 0.0F, 0.0F, 1.0F };
    if (nv_renderer_begin(&rdr, background))
    {
      ctext_text_render_info_t i = ctext_init_text_render_info();
      ctext_render(&amongus, &i, "Pee is stored in the balls");
      nv_renderer_end(&rdr);
    }
  }

  nv_sprite_destroy(&vkctx, &angwy);
  ctext_destroy_font(&vkctx, &amongus);

  nv_input_shutdown(&inputctx);
  nvsm_shutdown(&vkctx, &nvsmctx);
  nv_renderer_destroy(&rdr);
  nvvk_driver_destroy(&driver);
  nvvk_ctx_destroy(&vkctx);
  nv_window_shutdown(&ctx);

  return 0;
}
