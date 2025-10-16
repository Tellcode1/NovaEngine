#include "../include/iris/types.h"
#include "../include/std/include/math/vec4.h"
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_filesystem.h>

#include "../include/iris/driver.h"
#include "../include/shadersystem/nvsm.h"

#include "../include/ctext/ctext.h"
#include "../include/engine/camera.h"
#include "../include/engine/engine.h"
#include "../include/engine/input.h"
#include "../include/engine/renderer.h"
#include "../include/engine/sprite.h"

#include "../include/sets/runtime.h"
#include "../include/sets/sets.h"
#include "../include/std/include/errorcodes.h"
#include "../include/std/include/file.h"
#include "../include/std/include/print.h"
#include "../include/std/include/props.h"
#include "../include/std/include/stdafx.h"
#include "../include/std/include/timer.h"

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// #define double long double
const double G = 6.674010551359e-11;

/**
 * TODO: Benchmark whether loading the fonts at runtime is more expensive or not!!
 */

static inline void
simple_camera_movement_controller(double dt, void* arg)
{
  const nv_input_ctx_t* inputctxp = (nv_input_ctx_t*)arg;

  vec3   camera_movement_aggregate = nv_zero_init(vec3);
  double movespeed                 = 20.0;

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

typedef struct body
{
  vec2   pos;
  vec2   acc;
  vec2   vel;
  double mass;
  double radius;
} circle;

static inline void
verlet_accelerations(circle* bodies, size_t nbodies)
{
  for (size_t i = 0; i < nbodies; i++)
  {
    bodies[i].acc = v2zero; // reset
    for (size_t j = 0; j < nbodies; j++)
    {
      if (i == j)
        continue;

      vec2 d = v2sub(bodies[j].pos, bodies[i].pos);

      double r2 = v2dot(d, d); // small softening factor
      if (r2 < 1e1)
      {
        continue;
      }

      double inv_r3 = pow(r2, -1.5);

      bodies[i].acc = v2add(bodies[i].acc, v2muls(d, G * bodies[j].mass * inv_r3));
    }
  }
}

static inline void
verlet_step(circle* bodies, size_t nbodies, double dt)
{
  for (size_t i = 0; i < nbodies; i++)
  {
    // half step

    // bodies[i]->vel += 0.5 * bodies[i]->acc * dt;
    bodies[i].vel = v2add(bodies[i].vel, v2muls(bodies[i].acc, 0.5 * dt));

    // bodies[i]->pos += bodies[i]->vel * dt;
    bodies[i].pos = v2add(bodies[i].pos, v2muls(bodies[i].vel, dt));
  }

  // recompute acceleration
  verlet_accelerations(bodies, nbodies);

  for (size_t i = 0; i < nbodies; i++)
  {
    // bodies[i]->vel += 0.5 * bodies[i]->acc * dt;
    bodies[i].vel = v2add(bodies[i].vel, v2muls(bodies[i].acc, 0.5 * dt));
  }
}

int
main(int argc, char* argv[])
{
  nv_error code = NV_SUCCESS;
  if ((code = sets_init()) != NV_SUCCESS)
  {
    return code;
  }

  char windowname[64] = "clocker";
  if (sets_get_setting_defined("window_name"))
  {
    nv_strlcpy(windowname, sets_get_setting_string("window_name"), sizeof(windowname));
  }

  int         window_width            = SETS_GET_INT_WITH_FALLBACK("window_width", 800);
  int         window_height           = SETS_GET_INT_WITH_FALLBACK("window_height", 600);
  bool        force_recompile_shaders = false;
  bool        window_is_resizable     = false;
  nv_option_t options[]               = {
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

  const nv_extent2 window_size = (nv_extent2){ (size_t)window_width, (size_t)window_height };

  nv_ctx_t       ctx      = nv_zero_init(nv_ctx_t);
  nvvk_ctx_t     vkctx    = nv_zero_init(nvvk_ctx_t);
  iris_driver_t  driver   = nv_zero_init(iris_driver_t);
  nv_renderer_t  rdr      = nv_zero_init(nv_renderer_t);
  nv_input_ctx_t inputctx = nv_zero_init(nv_input_ctx_t);
  nvsm_ctx_t     nvsmctx  = nv_zero_init(nvsm_ctx_t);

  nv_window_init(windowname, (int)window_size.width, (int)window_size.height, &ctx);

  // If you're wondering why every Action has a +,
  // I want to create a resource system with info about everything like bindings
  // It'll be used to serialize a save, for example
  // and etc. and every event will have a preceding +, booleans will have a 0
  // and integers will have an i I'll drop the + (the user won't have to add it)
  // when i get to it
  nv_return_error_if_fail(nv_input_init(&ctx, &inputctx));
  nv_return_error_if_fail(nvvk_ctx_init(&ctx, &vkctx));

  nvsmctx.list_file = "shaders/shaderlist";
  nv_return_error_if_fail(nvsm_init(&nvsmctx));

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
  rdconf.vsync_enabled        = true;
  rdconf.buffer_mode          = NOVA_BUFFER_MODE_TRIPLE_BUFFERED;
  rdconf.window_resizable     = window_is_resizable;
  rdconf.initial_window_size  = window_size;
  rdconf.multisampling_enable = sets_get_setting_defined("rdr_samples");

  const sets_var_t* samples_setting = sets_get_setting("rdr_samples");
  if (samples_setting)
  {
    if (samples_setting->type == SETS_VAR_TYPE_STR && nv_strcmp(samples_setting->value.str, "max") == 0)
    {
      rdconf.samples = NOVA_SAMPLE_COUNT_MAX_SUPPORTED;
    }
    else if (samples_setting->type == SETS_VAR_TYPE_INT)
    {
      bool samples_is_power_of_two = (samples_setting->value.num & (samples_setting->value.num - 1)) == 0;
      nv_assert_else_return(samples_is_power_of_two, NV_ERROR_INVALID_ARG);

      rdconf.samples = samples_setting->value.num;
    }
  }
  else
  {
    rdconf.samples = NOVA_SAMPLE_COUNT_1_SAMPLES;
  }

  if ((code = iris_driver_init(&vkctx, &driver)) != NV_SUCCESS)
  {
    return code;
  }

  if ((code = nv_rdr_init(&ctx, &nvsmctx, &driver, &rdconf, &rdr)) != NV_SUCCESS)
  {
    nv_log_error("Fatal error in initializing renderer (error:%s)\n", nv_error_str(code));
    return code;
  }

  cfont_t amongus = nv_zero_init(cfont_t);

  nv_log_info("Initialized in %fs\n", nv_timer_time_since_start(&tm));

  ctext_load_font(&vkctx, &rdr, "Assets/roboto.ttf", 64, &amongus);

  nv_sprite_t circle_sprite = nv_zero_init(nv_sprite_t);
  if ((code = nv_sprite_load_from_disk(&driver, "Assets/circle.png", &circle_sprite)) != NV_SUCCESS)
  {
    return code;
  }

  nv_ctx_register_fixed_update(&ctx, simple_camera_movement_controller, (void*)&inputctx);

  // const char* buff  = "It's a red guitar.";
  double scale = 1.0;

  char* the_bible_for_some_fucking_reason = NULL;
  nv_fs_file_read_all("./piss", nv_allocator_c, NULL, &the_bible_for_some_fucking_reason, NULL);

  // const double M_sun   = 1.9891e30;
  // const double M_earth = 5.97219e24;
  // const double M_moon  = 7.35e22;
  // const double r_e     = 1.5e11;
  // double       r_m     = 3.844e5;

  // avg distance of sun and earth
  // // avg distance of earth and moon

  // double v_e = sqrt(G * M_sun / r_e);
  // double v_m = sqrt(G * M_earth / r_m);

  // The planets figure out a way to escape the matrix by generating energy
  // body moon  = { .pos = v2init(05.0, 10.0), .vel = v2init(-5.5e-5, -2.5e-5), .mass = M_moon };
  // body earth = { .pos = v2init(10.0, 00.0), .vel = v2init(-1e-5, 5.5e-5), .mass = M_earth };
  // body sun   = { .pos = v2init(00.0, 00.0), .vel = v2init(0.0, 0.0), .mass = M_sun };

  // double v_e = sqrt(G * M_sun / r_e);
  // double v_m = sqrt(G * M_earth / r_m);

  circle partices[33] = { 0 };
  partices[0]         = (circle){ .pos = v2init((SDL_randf() - 0.5) * 10.0, (SDL_randf() - 0.5) * 10.0), .vel = v2zero, .mass = 100.0 };
  for (size_t i = 1; i < nv_arrlen(partices); i++)
  {
    circle* p = &partices[i];
    p->mass   = (SDL_randf() + 0.1) * 2.5;
    p->radius = (SDL_randf() + 0.1) * 2.5;
    p->pos.x  = (SDL_randf() - 0.5) * 20.0;
    p->pos.y  = (SDL_randf() - 0.5) * 20.0;
    p->vel.x  = (SDL_randf() - 0.5) * 1e-5;
    p->vel.y  = (SDL_randf() - 0.5) * 1e-5;
  }

  while (nv_ctx_running(&ctx))
  {
    iris_begin_upload_batch(&driver);

    nv_update(&ctx);
    nv_input_update(&inputctx);
    nv_camera_update(&camera, &rdr);

    scale += nv_input_get_mouse_scroll(&inputctx) / 10.0;

    // we're like nearly fixed with some random lag spikes so ignore them lmao
    // 1 hour per step. ~60 hours per second
    const double simulation_dt = 60.0 * 10.0;
    const int    substeps      = simulation_dt / 60;
    const double substep_dt    = simulation_dt / substeps;

    verlet_accelerations(partices, nv_arrlen(partices));
    for (size_t i = 0; i < substeps; i++)
    {
      verlet_step(partices, nv_arrlen(partices), substep_dt);
    }

    // if (nv_input_is_mouse_signalled(&inputctx, 1))
    // {
    //   moon.pos = v2muls(nv_camera_get_global_mouse_position(&inputctx, &camera), scaling_factor);
    //   moon.acc = v2zero;
    //   moon.vel = v2zero;
    // }

    const vec4 background = (vec4){ 0.1, 0.05, 0.1, 1.0F };
    if (nv_rdr_begin_render(&rdr, background)) // calls nv_camera_upload_uniform_buffer
    {
      vec3 camera_position = camera.position;

      ctext_text_render_info_t txt = ctext_init_text_render_info();
      // i.bbox                     = (vec2){ camera.ortho_size.x, camera.ortho_size.y };
      // i.scale_for_fit            = true;
      txt.vertical   = CTEXT_VERT_ALIGN_CENTER;
      txt.scale      = scale;
      txt.position.x = nv_camera_get_global_mouse_position(&inputctx, &camera).x;
      txt.position.y = nv_camera_get_global_mouse_position(&inputctx, &camera).y;
      txt.position.z = 0.0;

      txt.rotation.x = 0.0;
      txt.rotation.y = 0.0;
      // txt.rotation.z = ctx.time;

      txt.perspective_projection = false;
      // nv_printf("%f  %f\n", txt.color.x, txt.color.y);
      // ctext_render(&amongus, &txt, "vec2(%.3f, %.3f) vs vec2(%.3f, %.3f)", earth.vel.x, earth.vel.y, moon.vel.x, moon.vel.y);
      // ctext_render(&amongus, &txt, "%s", the_bible_for_some_fucking_reason);

      const vec3 axes_begin = v3init(camera_position.x, camera_position.y, 0.0F);
      const vec3 x_axis_end = v3init(100, 0, 0);
      const vec3 y_axis_end = v3init(0, 100, 0);
      const vec3 z_axis_end = v3init(0, 0, 100);
      nv_rdr_render_line(&rdr, axes_begin, v3add(y_axis_end, axes_begin), v4init(0, 0, 1, 1), 0); // Y axis: blue
      nv_rdr_render_line(&rdr, axes_begin, v3add(x_axis_end, axes_begin), v4init(1, 0, 0, 1), 0); // X axis: red
      nv_rdr_render_line(&rdr, axes_begin, v3add(z_axis_end, axes_begin), v4init(0, 1, 0, 1), 0); // Z axis: green

      for (size_t i = 0; i < nv_arrlen(partices); i++)
      {
        const circle* p = &partices[i];
        nv_rdr_render_quad(&rdr, &circle_sprite, v2init(1, 1), v3init(p->pos.x, p->pos.y, 0.0), v3init(p->radius, p->radius, 0), v4init(0, 0, 1, 1), 0);
        nv_rdr_render_line(
            &rdr,
            v3init(p->pos.x, p->pos.y, 0.0),
            v3add(v3init(p->vel.x, p->vel.y, 0.0), v3init(p->pos.x, p->pos.y, 0.0)),
            v4init(0, 1, 0, 1),
            0); // EARTH: Green line
      }

      txt.rotation.z *= NVM_DEG2RAD(ctx.time * 10.0 + 180.0);
      txt.position.x             = 0.0;
      txt.position.y             = 0.0;
      txt.position.z             = 5.0;
      txt.horizontal             = CTEXT_HORI_ALIGN_LEFT;
      txt.vertical               = CTEXT_VERT_ALIGN_CENTER;
      txt.color                  = v4one;
      txt.perspective_projection = true;
      ctext_render(&amongus, &txt, "%d", (int)(1.0 / ctx.delta_time));

      nv_rdr_end_render(&rdr);
    }

    iris_end_upload_batch(&driver);
  }

  nv_free(the_bible_for_some_fucking_reason);

  nv_sprite_destroy(&circle_sprite);
  ctext_destroy_font(&amongus);

  nvsm_shutdown(&vkctx, &nvsmctx);
  nv_rdr_destroy(&rdr);
  iris_driver_destroy(&driver);
  nvvk_ctx_destroy(&vkctx);

  nv_input_shutdown(&inputctx);
  nv_window_shutdown(&ctx);

  sets_shutdown();

  return 0;
}
