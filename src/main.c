#include "../common/cvar.h"
#include "../common/mem.h"
#include "../common/stdafx.h"
#include "../common/timer.h"
#include "../external/box2d/include/box2d/box2d.h"
#include "../external/volk/volk.h"
#include "../include/engine/camera.h"
#include "../include/engine/ctext.h"
#include "../include/engine/engine.h"
#include "../include/engine/input.h"
#include "../include/engine/object.h"
#include "../include/engine/scene.h"
#include "../include/engine/ui.h"

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
  self->color = (vec4){ 0.6f, 0.6f, 0.6f, 1.0f };
}

__attribute__((__used__, __noinline__)) void
test_allocator(void)
{
  uchar              buf[1024];

  nv_allocator_stack stack;
  nv_allocator_stack_init(&stack, buf, 1024);

  nv_allocator ac;
  nv_allocator_bind_stack_allocator(&ac, &stack);

  size_t*         allocations[32];
  bool            pass                = 1;

  volatile uchar* TestLargeAllocation = ac.alloc(&ac, 1, 100);
  for (int i = 0; i < 100; i++)
  {
    TestLargeAllocation[i] = (uchar)rand();
  }
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

  if (!pass)
  {
    nv_log_and_abort("Test Failed");
  }
  else
  {
    nv_log_info("Test Passed");
  }
}

int
main(int argc, char* argv[])
{
  test_allocator();

  timer start_timer = timer_begin(0.1);

  (void)argc;
  (void)argv;
  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);

  const nv_extent2d window_size = (nv_extent2d){ 400, 400 };

  nv_initialize_context("NOVA example", window_size.width, window_size.height);

  nv_renderer_config rdconf   = nv_renderer_configInit();
  rdconf.vsync_enabled        = 0;
  rdconf.buffer_mode          = NOVA_BUFFER_MODE_TRIPLE_BUFFERED;
  rdconf.window_resizable     = 0;
  rdconf.initial_window_size  = window_size;
  rdconf.multisampling_enable = 0;
  rdconf.samples              = NOVA_SAMPLE_COUNT_1_SAMPLES;
  nv_renderer_t* rd           = nv_renderer_init(&rdconf);

  // If you're wondering why every Action has a +,
  // I want to create a resource system with info about everything like bindings
  // It'll be used to serialize a save, for example
  // and etc. and every event will have a preceding +, booleans will have a 0 and integers will have an i
  // I'll drop the + (the user won't have to add it) when i get to it

  nv_input_init();
  nv_input_bind_key_to_action(SDL_SCANCODE_SPACE, "+jump");
  nv_input_bind_key_to_action(SDL_SCANCODE_RIGHT, "+right");
  nv_input_bind_key_to_action(SDL_SCANCODE_LEFT, "+left");
  nv_input_bind_key_to_action(SDL_SCANCODE_RETURN, "+Enter");

  const float updateTime = 3.0f; // seconds. 1.5f = 1.5 seconds
  float       totalTime  = 0.0f;
  u32         numFrames  = 0;

  cfont_t*    amongus;
  ctext_load_font(rd, "./bakedfont", 256.0f, &amongus);

  int         curr_showing_fps = 0;

  float       accumulator      = 0.0f;
  const float timeStep         = 1.0f / 60.0f;

  nvui_init();

  nvui_button* bton      = nvui_create_button(nv_sprite_load_from_disk("../Assets/x.png"));
  bton->transform.size.x = 5.0f;
  bton->transform.size.y = 5.0f;
  bton->on_click         = leave_game;
  bton->on_hover         = hoover;

  nv_scene_t* scn        = nv_scene_init();

  const vec2  r1pos      = (vec2){ 0.0f, 10.0f };
  const vec2  r2pos      = (vec2){ 0.0f, 0.0f };

  const vec2  r1siz      = (vec2){ 0.5f, 0.5f };
  const vec2  r2siz      = v2muls(camera.ortho_size, 0.4);

  nv_object*  r1         = nv_object_create(scene_main, "Rect1", NOVA_COLLIDER_TYPE_DYNAMIC, B2_DEFAULT_CATEGORY_BITS, B2_DEFAULT_MASK_BITS, r1pos, v2muls(r1siz, 2.0f), 0);

  nv_object*  r2         = nv_object_create(scene_main, "Rect2", NOVA_COLLIDER_TYPE_STATIC, B2_DEFAULT_CATEGORY_BITS, B2_DEFAULT_MASK_BITS, r2pos, v2muls(r2siz, 2.0f), 0);
  nv_object_get_sprite_renderer(r1)->color = (vec4){ 1.0f, 0.0f, 0.0f, 1.0f };
  nv_object_get_sprite_renderer(r2)->color = (vec4){ 0.0f, 0.0f, 1.0f, 1.0f };

  // What in the unholy f%$ where you doing
  nv_log_debug("Initialized in %li ms (%.3f s)", (long)(timer_time_since_start(&start_timer) * 1000.0), timer_time_since_start(&start_timer));
  while (nv_running())
  {
    nv_update();
    const double dt = nv_get_delta_time();

    if (nv_input_is_action_just_signalled("+jump"))
    {
      nv_object_move(r1, (vec2){ 0.0f, 5.0f });
    }
    if (nv_input_is_action_signalled("+left"))
    {
      nv_object_move(r1, (vec2){ 25.0f * -dt, 0.0f });
    }
    if (nv_input_is_action_signalled("+right"))
    {
      nv_object_move(r1, (vec2){ 25.0f * dt, 0.0f });
    }

    accumulator += dt;
    while (accumulator >= timeStep)
    {
      // fixed update
      nv_scene_update();
      accumulator -= timeStep;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      nv_consume_event(&event);
    }
    nv_input_update();

    // Profiling code
    totalTime += dt;
    numFrames++;
    if (totalTime >= updateTime)
    {
      curr_showing_fps = ceilf(numFrames / totalTime);
      nv_log_info("%i FPS %f MS/Frame", curr_showing_fps, (totalTime / (float)(numFrames)));
      numFrames = 0;
      totalTime = 0.0;
    }

    nv_camera_update(&camera, rd);

    nvui_update();
    if (!bton->was_hovered)
    { // If the mouse is NOT on the button, change it
      // back to it's default color
      bton->color = (vec4){ 1.0f, 1.0f, 1.0f, 1.0f };
    }
    bton->transform.position = v2add((vec2){ camera.position.x, camera.position.y }, (vec2){ -9.0f, 9.0f });
    bton->transform.size     = (vec2){ 1.0f, 1.0f };

    nv_renderer_set_clear_color(rd, (vec4){ .x = 0.2f, .y = 0.2f, .z = 0.2f, .w = 1.0f });

    if (nv_renderer_begin(rd))
    {
      nv_scene_render(rd);

      nv_renderer_end(rd);
    }
  }

  // Destroying everything isn't that performance intensive...
  // oh wait I was using a global stack allocator...
  timer finish_timer = timer_begin(0.1);

  nv_scene_destroy(scn);

  nvui_destroy_button(bton);

  vkDeviceWaitIdle(device);
  ctext_destroy_font(amongus);
  nvui_shutdown();
  ctext_shutdown(rd);
  nv_renderer_destroy(rd);
  nv_input_shutdown();
  nv_log_info("done");
  nv_log_info("Took %f seconds to clean up", timer_time_since_start(&finish_timer));
  return 0;
}
