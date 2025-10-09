#version 450 core

#include "../camera.glsl"

layout(location = 0) in vec3 v_vertices;
layout(location = 1) in vec2 v_uv;

layout(push_constant) uniform push_constant_block
{
  mat4 model;
  vec4 color;
  vec4 outline_color;
  float scale;
}
pc;

layout(location = 0) out vec2 f_uv;

layout(location = 1) out vec4 f_col;

void
main()
{
  gl_Position = cam_ub.perspective * cam_ub.view * pc.model * vec4(v_vertices, 1.0);
  f_col       = pc.color;
  f_uv = v_uv;
}
