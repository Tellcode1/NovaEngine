#ifndef C_TEXT_H
#define C_TEXT_H

#include "../iris/buffer.h"
#include "../iris/ringbuffer.h"
#include "../iris/sampler.h"
#include "../iris/texture.h"
#include "../iris/types.h"
#include "../std/include/containers/hashmap.h"
#include "../std/include/containers/list.h"
#include "../std/include/errorcodes.h"
#include "../std/include/math/mat.h"
#include "../std/include/math/vec2.h"
#include "../std/include/math/vec3.h"
#include "../std/include/math/vec4.h"
#include <stddef.h>

// THE PLAN
// Write all the glyph vertices to the GPU
// Index them and render them as needed...

// why does the above text sound like an andrew tate quote

#ifdef __cplusplus
extern "C"
{
#endif

  struct nvvk_ctx;

  // DEPRECATE THIS YOU FOOL
  // IT WAS ONLY MEANT FOR SIMPLE TESTING
  static const int           CTEXT_MAX_FONT_COUNT = 8;
  typedef struct cfont       cfont_t;
  typedef struct ctext_label ctext_label_t;

  typedef struct ctext_drawcall         ctext_drawcall_t;
  typedef struct ctext_text_render_info ctext_text_render_info_t;

  // TODO: Implement
  typedef enum ctext_text_style
  {
    CTEXT_TEXT_STYLE_NORMAL      = 0,
    CTEXT_TEXT_STYLE_ITALIC      = 1,
    CTEXT_TEXT_STYLE_BOLD        = 2,
    CTEXT_TEXT_STYLE_BOLD_ITALIC = 3,
  } ctext_text_style;

  typedef enum ctext_hori_align
  {
    CTEXT_HORI_ALIGN_LEFT   = 0,
    CTEXT_HORI_ALIGN_CENTER = 1,
    CTEXT_HORI_ALIGN_RIGHT  = 2,
  } ctext_hori_align;

  typedef enum ctext_vert_align
  {
    CTEXT_VERT_ALIGN_TOP    = 0,
    CTEXT_VERT_ALIGN_CENTER = 1,
    CTEXT_VERT_ALIGN_BOTTOM = 2,
  } ctext_vert_align;

  static inline int
  ctext_compare_glyph_keys(const void* key1, const void* key2, size_t nbytes, void* user_data)
  {
    (void)nbytes;
    (void)user_data;
    return *(char*)key1 - *(char*)key2;
  }

  // Initializes the text renderer for ONLY that renderer
  extern nv_error ctext_init(struct nv_renderer* rd);
  extern void     ctext_shutdown(struct nv_renderer* rd);

  extern void ctext_load_font(struct nvvk_ctx* vkctx, struct nv_renderer* rd, const char* font_path, int scale, cfont_t* dst);

  /*
    Returns 0 if the font is ok and anything else if it is on life support (hasn't crashed your program yet)
  */
  extern int ctext_validate_font(const cfont_t* fnt);

  extern void ctext_destroy_font(cfont_t* fnt);

  extern void ctext_render(cfont_t* fnt, const ctext_text_render_info_t* pInfo, const char* fmt, ...) NOVA_ATTR_FORMAT(3, 4);

  extern void ctext_flush_renders(struct nv_renderer* rd);
  extern void ctext_flush_font_renders(cfont_t* fnt);

  extern void ctext_get_text_size(const cfont_t* fnt, const char* str, vec2* dst);

  // Get the scale needed to fit the string in a box
  // The scale is calculated as if both the string and the box were at (0,0)
  extern double ctext_get_scale_for_fit(const cfont_t* fnt, const char* str, vec2 bbox);

  struct ctext_text_render_info
  {
    mat4             model;
    ctext_hori_align horizontal;
    ctext_vert_align vertical;

    vec4 color;
    vec3 position;
    // Radians, as always.
    vec3 rotation; // TODO: Quaternion :3

    double scale;         // if scale_for_fit is 1, this is multiplied by the calculated scale.
    vec2   bbox;          // The bounding box that the scale will be determined for. Only when scale_for_fit is 1
    bool   scale_for_fit; // calculates the scale needed to fit the text into a box

    /**
     * If set to true, the camera's perspective matrix is used,
     * Otherwise, the orthographic projection matrix is used.
     */
    bool perspective_projection;
  };

  struct ctext_push_constants
  {
    mat4f model;
    vec4f color;
    vec4f outline_color;
    float scale;
    // If false, perspective projection matrix is used, else orthographic.
    int is_orthographic_proj;
  };

  typedef struct ctext_glyph_vertex
  {
    vec3f pos;
    vec2f uv;
  } ctext_glyph_vertex_t;

  struct ctext_drawcall
  {
    mat4 model;

    vec3   position;
    vec3   rotation;
    vec4   color;
    double scale;
    bool   perspective_projection;

    size_t vertex_count;
    size_t index_count;
    size_t index_offset;

    ctext_glyph_vertex_t* vertices;
    u32*                  indices;
  };

  /* Internal CFont struct. Do not modify yourselves! */
  struct cfont
  {
    nv_hashmap_t glyph_map;
    nv_list_t    drawcalls;

    // nv_async_task_t load_task;

    iris_texture_t texture;
    iris_sampler_t sampler;

    iris_ring_buffer_t buffer;
    iris_buffer_t      staging_buffer;

    size_t index_buffer_offset; // aka vertices size
    bool   to_render;
    bool   buffer_resized;
    bool   rendered_this_frame;
    size_t index_count;

    double line_height;
    double space_width;

    void*               mapped;
    struct nv_renderer* rd;
  };

  static inline ctext_text_render_info_t
  ctext_init_text_render_info(void)
  {
    return (ctext_text_render_info_t){
      .model         = m4init(1.0F),
      .horizontal    = CTEXT_HORI_ALIGN_CENTER,
      .vertical      = CTEXT_VERT_ALIGN_CENTER,
      .color         = v4one,
      .position      = v3zero,
      .rotation      = v3zero,
      .scale         = 1.0f,
      .bbox          = v2zero,
      .scale_for_fit = false,
    };
  }

#ifdef __cplusplus
}
#endif

#endif
