layout(set = 0, binding = 0, std140) uniform camera_buffer {
  mat4 perspective;
  mat4 ortho;
  mat4 view;
  vec4 camera_position;
  vec4 camera_front;
  vec4 camera_right;
  // camera_up = cross(right, front)
  vec4 clip_plane;
  uvec2 render_extent;
  uint image_index;
  uint _padding;
} cam_ub;