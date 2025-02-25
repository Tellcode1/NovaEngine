#include "../include/engine/camera.h"
#include "../include/engine/ctext.h"
#include "../include/engine/engine.h"
#include "../include/engine/input.h"
// #include "../include/engine/mesh.h"
#include "../include/engine/shadermanager.h"
// #include "../ssl/sslang.h"
// #include "../std/async.h"
#include "../ssl/ssl.h"
#include "../std/print.h"
#include "../std/props.h"
#include "../std/stdafx.h"
#include "../std/timer.h"

#include <sys/stat.h>

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

int
main(int argc, char* argv[])
{
  char        windowname[64]    = "clocker";
  int         window_width      = 800;
  int         window_height     = 600;
  bool        recompile_shaders = 0, resizable_window = 0;
  nv_option_t options[] = {
    { NV_OP_TYPE_STRING, "wn", "window-name", windowname, sizeof(windowname) },
    { NV_OP_TYPE_INT, "ww", "window-width", &window_width, 0 },
    { NV_OP_TYPE_INT, "wh", "window-height", &window_height, 0 },
    { NV_OP_TYPE_BOOL, NULL, "recompile-shaders", &recompile_shaders, 0 },
    { NV_OP_TYPE_BOOL, "rw", "resizable-window", &resizable_window, 0 },
  };

  char error[256];
  if (nv_props_parse(argc, argv, options, nv_arrlen(options), error, sizeof(error)) == -1)
  {
    nv_push_error("PROPS error: %s", error);
    nv_props_gen_help(options, nv_arrlen(options), error, nv_arrlen(error));
    nv_printf("%s\n", error);
  }

  timer tm = timer_begin(0.1);

  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);

  const nv_extent2d window_size = (nv_extent2d){ window_width, window_height };

  nv_initialize_context(windowname, window_size.width, window_size.height);

  if (recompile_shaders)
  {
    nvsm_compile_all();
  }
  else
  {
    nvsm_compile_updated();
  }

  nv_renderer_config rdconf   = nv_renderer_config_init();
  rdconf.vsync_enabled        = 1;
  rdconf.buffer_mode          = NOVA_BUFFER_MODE_TRIPLE_BUFFERED;
  rdconf.window_resizable     = resizable_window;
  rdconf.initial_window_size  = window_size;
  rdconf.multisampling_enable = 0;
  rdconf.samples              = NOVA_SAMPLE_COUNT_1_SAMPLES;
  nv_renderer_t* rdr          = nv_renderer_init(&rdconf);

  // If you're wondering why every Action has a +,
  // I want to create a resource system with info about everything like bindings
  // It'll be used to serialize a save, for example
  // and etc. and every event will have a preceding +, booleans will have a 0
  // and integers will have an i I'll drop the + (the user won't have to add it)
  // when i get to it
  nv_input_init();

  const flt_t updateTime = 3.0f; // seconds. 1.5f = 1.5 seconds
  flt_t       totalTime  = 0.0f;
  u32         numFrames  = 0;

  cfont_t amongus;

  int curr_showing_fps = 0;

  nv_log_info("Initialized in %fs", timer_time_since_start(&tm));

  ctext_load_font(rdr, "Assets/roboto.ttf", 128, &amongus);

  const char* errors = NULL;
  while (nv_running())
  {
    while ((errors = nv_pop_error()))
    {
      nv_printf("%s\n", errors);
    }
    nv_update();
    const real_t dt = nv_get_delta_time();

    SDL_Event event;
    nv_input_update();

    nv_camera_update(&camera, rdr);

    while (SDL_PollEvent(&event))
    {
      nv_consume_event(&event);
    }

    totalTime += dt;
    numFrames++;
    if (totalTime >= updateTime)
    {
      curr_showing_fps = ceilf(numFrames / totalTime);
      nv_log_info("%i FPS %f MS/Frame", curr_showing_fps, (totalTime / (flt_t)(numFrames)));
      numFrames = 0;
      totalTime = 0.0;
    }

    if (nv_renderer_begin(rdr))
    {
      struct tm* time = _nv_get_time();

      const char* day  = get_day_str(time);
      const char* mon  = get_month_str(time);
      size_t      year = time->tm_year + 1900;

      ctext_text_render_info_t clock_info = ctext_init_text_render_info();
      clock_info.scale                    = 1.0;
      clock_info.bbox                     = (vec2){ camera.ortho_size.x * 2.0f, camera.ortho_size.y * 2.0f };
      clock_info.scale_for_fit            = 1;
      ctext_render(&amongus, &clock_info, "%i %s %s %d\n%d:%d:%zu\n", time->tm_mday, day, mon, year, time->tm_hour % 12, time->tm_min, time->tm_sec);

      nv_renderer_end(rdr);
    }
  }

  nv_input_shutdown();

  vkDeviceWaitIdle(device);
  ctext_destroy_font(&amongus);
  nv_renderer_destroy(rdr);
}