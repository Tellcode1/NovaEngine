#include "GPU/driver.h"
#include "engine/camera.h"
#include "engine/ctext.h"
#include "engine/engine.h"
#include "engine/input.h"
#include "engine/nvsm.h"
#include "engine/renderer.h"
#include "engine/sprite.h"
#include "std/errorcodes.h"
#include "std/image.h"
#include "std/math/vec2.h"
#include "std/print.h"
#include "std/props.h"
#include "std/stdafx.h"
#include "std/string.h"
#include "std/timer.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <freetype2/ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

static inline void
stuff()
{
  FT_Library lib;
  FT_Face    face;
  FT_Init_FreeType(&lib);
  FT_New_Face(lib, "Assets/roboto.ttf", 0, &face);

  FT_Load_Char(face, 'A', FT_LOAD_DEFAULT);

  FT_Outline outline = face->glyph->outline;

  for (size_t i = 0; i < outline.n_contours; i++)
  {
    nv_printf("%u ", (u32)outline.contours[i]);
  }
  nv_printf("\n");
  for (size_t i = 0; i < outline.n_points; i++)
  {
    nv_printf("%l %l", outline.points[i].x, outline.points[i].y);
  }
  nv_printf("\n");

  FT_Done_Face(face);
  FT_Done_FreeType(lib);
}

static inline const char*
get_day_str(const struct tm* t)
{
  return (const char*[]){ "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" }[t->tm_wday];
}

static inline const char*
get_month_str(const struct tm* t)
{
  return (const char*[]){ "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" }[t->tm_mon];
}

typedef struct bezier_t
{
  vec2 p1;
  vec2 p2;
  vec2 ctrl;
} bezier_t;

static inline vec2
bezier_curve(const bezier_t* bz, flt_t t)
{
  nv_assert_else_return(t >= 0 && t < 1, (vec2){});
  return v2add(bz->p1, v2muls(v2sub(bz->p2, bz->p1), t));
}

int
main(int argc, char* argv[])
{
  stuff();

  char        windowname[64]          = "clocker";
  int         window_width            = 800;
  int         window_height           = 600;
  bool        force_recompile_shaders = 0, resizable_window = 0;
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

  const nv_extent2d window_size = (nv_extent2d){ window_width, window_height };

  nv_ctx_t   ctx     = nv_zero_init(nv_ctx_t);
  nvvk_ctx_t nvvkctx = nv_zero_init(nvvk_ctx_t);

  nv_window_init(windowname, (int)window_size.width, (int)window_size.height, &ctx);
  nvvk_ctx_init(&ctx, &nvvkctx);

  nvsm_ctx_t nvsmctx = nv_zero_init(nvsm_ctx_t);
  nvsmctx.list_file  = "Shaders/shaderlist";
  nvsm_init(&nvsmctx);

  if (force_recompile_shaders)
  {
    nvsm_compile_shaders_force(&nvsmctx, true);
  }
  else
  {
    nvsm_compile_shaders(&nvsmctx);
  }

  nvsm_create_shader_modules(&nvvkctx, &nvsmctx);

  nv_renderer_config rdconf   = nv_renderer_config_init();
  rdconf.vsync_enabled        = 1;
  rdconf.buffer_mode          = NOVA_BUFFER_MODE_TRIPLE_BUFFERED;
  rdconf.window_resizable     = resizable_window;
  rdconf.initial_window_size  = window_size;
  rdconf.multisampling_enable = 0;
  rdconf.samples              = NOVA_SAMPLE_COUNT_1_SAMPLES;

  nv_errorc code = NV_SUCCESS;

  nvvk_driver_t driver;
  if ((code = nvvk_driver_init(&nvvkctx, &driver)) != NV_SUCCESS)
  {
    return code;
  }

  nv_renderer_t rdr;
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
  nv_input_ctx_t inputctx = nv_zero_init(nv_input_ctx_t);
  nv_input_init(&inputctx);

  const real_t updateTime = 3.0; // seconds. 1.5f = 1.5 seconds
  real_t       totalTime  = 0.0;
  u32          numFrames  = 0;

  cfont_t amongus = nv_zero_init(cfont_t);

  int curr_showing_fps = 0;

  nv_log_info("Initialized in %fs\n", nv_timer_time_since_start(&tm));

  ctext_load_font(&nvvkctx, &rdr, "Assets/roboto.ttf", 64, &amongus);

  nv_sprite_t angwy = nv_zero_init(nv_sprite_t);
  if ((code = nv_sprite_load_from_disk(&driver, "Assets/i want to die.png", &angwy)) != NV_SUCCESS)
  {
    return code;
  }

  nv_image_t angwy_img;
  nv_image_load("Assets/i want to die.png", &angwy_img);
  nv_image_write_png(&angwy_img, "piss.png");

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

    totalTime += dt;
    numFrames++;
    if (totalTime >= updateTime)
    {
      curr_showing_fps = ceil(numFrames / totalTime);
      nv_log_info("%i FPS %f MS/Frame\n", curr_showing_fps, (totalTime / (flt_t)(numFrames)));
      numFrames = 0;
      totalTime = 0.0;
    }

    if (nv_renderer_begin(&rdr, (vec4){ 0.0F, 0.0F, 0.0F, 1.0F }))
    {
      struct tm* time = _nv_get_time();

      const char* day  = get_day_str(time);
      const char* mon  = get_month_str(time);
      size_t      year = time->tm_year + 1900;

      ctext_text_render_info_t clock_info = ctext_init_text_render_info();
      clock_info.scale                    = 1.0F;
      clock_info.bbox                     = (vec2){ camera.ortho_size.x * 2.0f, camera.ortho_size.y * 2.0f };
      clock_info.scale_for_fit            = 1;
      ctext_render(&amongus, &clock_info, "%i %s %s %zu\n%d:%d:%i\n", time->tm_mday, day, mon, year, time->tm_hour % 12, time->tm_min, time->tm_sec);

      const bezier_t bz = (bezier_t){
        .p1   = (vec2){ .x = 0.0F, .y = 0.0F },
        .p2   = (vec2){ .x = 0.0F, .y = 60.0F },
        .ctrl = (vec2){ .x = 30.0F, .y = -20.0F },
      };

      const u32 segs = 32;
      for (u32 i = 0; i < segs; i++)
      {
        const flt_t t   = 1.0F / (flt_t)segs;
        const vec2  pos = bezier_curve(&bz, t);
        nv_renderer_render_line(&rdr, (vec2f){ 0.0F, 0.0F }, (vec2f){ pos.x, pos.y }, (vec4f){ 1.0F, 1.0F, 1.0F, 1.0F }, 0);
      }

      nv_renderer_render_quad(
          &rdr,
          &angwy,
          (vec2f){ 1.0f, 1.0f },
          (vec3f){ 0.5F * sinf(0.5F * (float)_nv_timer_get_currtime()), 0.5F * cosf(0.5F * (float)_nv_timer_get_currtime()), 0.0f },
          (vec3f){ 1.0f, 1.0f, 1.0f },
          (vec4f){ 1.0f, 1.0f, 1.0f, 1.0f },
          0);

      nv_renderer_end(&rdr);
    }
  }

  nv_sprite_destroy(&nvvkctx, &angwy);

  nv_free(angwy_img.data);

  nv_input_shutdown(&inputctx);
  nvsm_shutdown(&nvvkctx, &nvsmctx);
  ctext_destroy_font(&nvvkctx, &amongus);
  nv_renderer_destroy(&rdr);
  nvvk_driver_destroy(&driver);
  nvvk_ctx_destroy(&nvvkctx);
  nv_window_shutdown(&ctx);
}
