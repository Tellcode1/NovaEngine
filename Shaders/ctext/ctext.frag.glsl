// output: Shaders/ctext/text.spv stage: frag name: ctext/frag

#version 450

layout(location = 0) out vec4 outColor;

layout(location = 0) in vec2 texCoords;
layout(location = 1) in vec4 f_col;

/**
  * The SDF texture
*/
layout(set = 1, binding = 0) uniform sampler2D bitmap;

layout(push_constant) uniform push_constants
{
  mat4  model;
  vec4  color;
  vec4  outline_color;
  float scale;
}
pc;

// THis is a modified (improved for ctext) version of this:
// https://julhe.github.io/posts/always_sharp_sdf_textures/

/**
  * Defined here for uniformity, even though this shader uses smoothstep
  * I just like smoothstep more, use this if you like this more :shrug:
*/
#define inverse_lerp(a, b, x) ((x - a) / (b - a))

float
saturate(float x)
{
  return clamp(x, 0.0, 1.0);
}

float
filter_sdf_texture_approx()
{
  float sdf           = texture(bitmap, texCoords).r * 2.0 - 1.0; // [-1, 1]
  vec2  gradient      = fwidth(texCoords) * textureSize(bitmap, 0);
  float texelCoverage = saturate(max(gradient.x, gradient.y));
  return (smoothstep(-texelCoverage, texelCoverage, sdf));
}

float
filter_sdf_texture_nicely()
{
  float sdf = texture(bitmap, texCoords).r * 2.0 - 1.0; // [-1, 1]

  vec2 derivative_tex_coords = vec2(dFdx(texCoords.x), dFdy(texCoords.y));

  float pixel_footprint_area = abs(derivative_tex_coords.x * derivative_tex_coords.y);

  ivec2 tex_size = textureSize(bitmap, 0);
  pixel_footprint_area *= float(tex_size.x * tex_size.y);
  pixel_footprint_area *= pc.scale;

  /**
    * Not doing saturate() here envelops the glyph in a white haze
    * Don't know why but don't want to find out!
  */
  float pixel_footprint_diameter = saturate(sqrt(pixel_footprint_area));

  return smoothstep(-pixel_footprint_diameter, pixel_footprint_diameter, sdf);
}
void
main()
{
  float alpha = filter_sdf_texture_nicely();

  if (alpha <= 0.001)
  {
    discard;
  }

  outColor = vec4(pc.color.rgb, pc.color.a * alpha);
}