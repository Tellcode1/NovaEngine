layout(set = 0, binding = 0, std140) uniform camera_buffer {
  mat4 perspective;
  mat4 ortho;
  mat4 view;
  vec3 camera_position;
  vec3 camera_front;
  vec3 camera_right;
  // camera_up = cross(right, front)
  vec3 clip_plane;
  uvec2 render_extent;
  u32 image_index;
} cam_ub;