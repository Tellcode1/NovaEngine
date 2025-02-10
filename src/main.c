#include "../common/cvar.h"
#include "../common/mem.h"
#include "../external/volk/volk.h"
#include "../include/engine/camera.h"
#include "../include/engine/ctext.h"
#include "../include/engine/engine.h"
#include "../include/engine/input.h"
#include "../include/engine/scene.h"
#include "../include/engine/shadermanager.h"
#include "../include/engine/ui.h"
#include "../std/print.h"
#include "../std/props.h"
#include "../std/stdafx.h"
#include "../std/timer.h"

#include <math.h>
#include <stdio.h>

cvar* g_vars  = NULL;
int   g_nvars = 0;

void
leave_game(nvui_button* self)
{
  (void)self;
  nv_application_running = 0;
}

void
hoover(nvui_button* self)
{
  self->color = (vec4f){ 0.6f, 0.6f, 0.6f, 1.0f };
}

__attribute__((__used__, __noinline__)) void
test_allocator(void)
{
  uchar buf[1024];

  nv_allocator_stack stack;
  nv_allocator_stack_init(&stack, buf, 1024);

  nv_allocator_t ac;
  nv_allocator_bind_stack_allocator(&ac, &stack);

  size_t* allocations[32];
  bool    pass = 1;

  volatile uchar* TestLargeAllocation = ac.alloc(&ac, 1, 100);
  for (int i = 0; i < 100; i++) { TestLargeAllocation[i] = (uchar)rand(); }
  if (!TestLargeAllocation)
  {
    nv_log_error("Large allocation failed");
    pass = 0;
  }
  if (nv_memset((uchar*)TestLargeAllocation, 0, 100) == NULL)
  {
    nv_log_error("Large allocation memset failed");
    pass = 0;
  }

  for (size_t i = 0; i < nv_arrlen(allocations); i++)
  {
    size_t* allocation = ac.alloc(&ac, 1, sizeof(size_t));
    if (!allocation)
    {
      pass           = 0;
      allocations[i] = NULL;
      nv_log_error("allocation failed %d", i);
      continue;
    }
    *allocation    = i;
    allocations[i] = allocation;
  }

  for (size_t i = 0; i < nv_arrlen(allocations); i++)
  {
    if (allocations[i] == NULL)
    {
      nv_log_error("NULL allocation at index %d", i);
      pass = 0;
      continue;
    }
    size_t test = *(allocations[i]);
    if (i != test)
    {
      nv_log_error("addr %p (index %d) has incorrect data. expected %d got %d", allocations[i], i, i, test);
      pass = 0;
    }
    for (size_t j = 0; j < i; j++)
    {
      if (allocations[i] == allocations[j])
      {
        nv_log_error("duplicate allocations at index %d and %d: %p", i, j, allocations[i]);
        pass = 0;
      }
    }
  }

  for (size_t i = 0; i < nv_arrlen(allocations); i++)
  {
    if (allocations[i] != NULL)
    {
      ac.free(&ac, (void*)(allocations[i]));
      allocations[i] = NULL;
    }
  }

  if (!pass) { nv_log_and_abort("Test Failed"); }
  else { nv_log_info("Test Passed"); }
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

extern char** environ;

int
main(int argc, char* argv[])
{
  char        windowname[64] = "clocker";
  int         window_width = 200, window_height = 200;
  bool        recompile_shaders = 0;
  nv_option_t options[]         = { { NV_OP_TYPE_STRING, NULL, "window-name", windowname, sizeof(windowname) },
                                    { NV_OP_TYPE_INT, NULL, "window-width", &window_width, 0 },
                                    { NV_OP_TYPE_INT, NULL, "window-height", &window_height, 0 },
                                    { NV_OP_TYPE_BOOL, NULL, "recompile-shaders", &recompile_shaders, 0 } };

  char error[256];
  if (nv_props_parse(argc, argv, options, nv_arrlen(options), error, sizeof(error)) == -1) {
    nv_log_error("PROPS error: %s", error);
    nv_props_gen_help(options, nv_arrlen(options), error, nv_arrlen(error));
    nv_printf("%s\n", error);
  }

  timer tm = timer_begin(0.1);
  // test_allocator();

  (void)argc;
  (void)argv;
  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);

  const nv_extent2d window_size = (nv_extent2d){ window_width, window_height };

  nv_initialize_context(windowname, window_size.width, window_size.height);

  if (recompile_shaders) { nvsm_compile_all(); }
  else { nvsm_compile_updated(); }

  nv_renderer_config rdconf   = nv_renderer_config_init();
  rdconf.vsync_enabled        = 1;
  rdconf.buffer_mode          = NOVA_BUFFER_MODE_TRIPLE_BUFFERED;
  rdconf.window_resizable     = 0;
  rdconf.initial_window_size  = window_size;
  rdconf.multisampling_enable = 0;
  rdconf.samples              = NOVA_SAMPLE_COUNT_1_SAMPLES;
  nv_renderer_t* rd           = nv_renderer_init(&rdconf);

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

  cfont_t OpenSans;
  ctext_load_font(rd, "./OpenSans.ff", 1.0f, &OpenSans);

  int curr_showing_fps = 0;

  const flt_t scale = 2.0f;

  nv_log_info("Initialized in %fs", timer_time_since_start(&tm));

  while (nv_running())
  {
    nv_update();
    const real_t dt = nv_get_delta_time();

    SDL_Event event;
    while (SDL_PollEvent(&event)) { nv_consume_event(&event); }
    nv_input_update();
    nv_camera_update(&camera, rd);

    totalTime += dt;
    numFrames++;
    if (totalTime >= updateTime)
    {
      curr_showing_fps = ceilf(numFrames / totalTime);
      nv_log_info("%i FPS %f MS/Frame", curr_showing_fps, (totalTime / (flt_t)(numFrames)));
      numFrames = 0;
      totalTime = 0.0;
    }

    if (nv_renderer_begin(rd))
    {
      struct tm* time = _nv_get_time();

      const char* day  = get_day_str(time);
      const char* mon  = get_month_str(time);
      size_t      year = time->tm_year + 1900;

      ctext_text_render_info_t clock_info = ctext_init_text_render_info();
      clock_info.scale                    = scale;
      clock_info.bbox                     = (vec2){ camera.ortho_size.x, camera.ortho_size.y };
      clock_info.scale_for_fit            = 1;
      ctext_render(&OpenSans, &clock_info, "%i %s %s %d\n%d:%d:%zu", time->tm_mday, day, mon, year, time->tm_hour % 12, time->tm_min, time->tm_sec);

      nv_renderer_end(rd);
    }
  }

  nv_input_shutdown();

  vkDeviceWaitIdle(device);
  ctext_destroy_font(&OpenSans);
  nv_renderer_destroy(rd);
}