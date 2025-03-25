#ifndef __C_TEXT_H__
#define __C_TEXT_H__

// implementation: vk.c

#include "../std/math/mat.h"
#include "../std/math/vec2.h"
#include "../std/math/vec3.h"

#include "../containers/hashmap.h"
#include "../containers/list.h"

#include "../GPU/buffer.h"
#include "../GPU/texture.h"

// THE PLAN
// Write all the glyph vertices to the GPU
// Index them and render them as needed...

// why does the above text sound like an andrew tate quote

NOVA_HEADER_START

// DEPRECATE THIS YOU FOOL
// IT WAS ONLY MEANT FOR SIMPLE TESTING
static const int             CTEXT_MAX_FONT_COUNT = 8;
typedef struct cfont_t       cfont_t;
typedef struct ctext_label_t ctext_label_t;

typedef struct ctext_glyph_t            ctext_glyph_t;
typedef struct ctext_drawcall_t         ctext_drawcall_t;
typedef struct ctext_text_render_info_t ctext_text_render_info_t;

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
extern void ctext_init(struct nv_renderer_t* rd);
extern void ctext_shutdown(struct nv_renderer_t* rd);

extern void ctext_load_font(struct nv_renderer_t* rd, const char* font_path, int scale, cfont_t* dst);

/*
  Returns 0 if the font is ok and anything else if it is on life support (hasn't crashed your program yet)
*/
extern int ctext_validate_font(const cfont_t* fnt);

extern void ctext_destroy_font(cfont_t* fnt);

extern void ctext_render(cfont_t* fnt, const ctext_text_render_info_t* pInfo, const char* fmt, ...);

extern void ctext_flush_renders(struct nv_renderer_t* rd);
extern void _ctext_flush_font(struct nv_renderer_t* rd, cfont_t* fnt);

// Get the scale needed to fit the string in a box
// The scale is calculated as if both the string and the box were at (0,0)
extern flt_t ctext_get_scale_for_fit(const cfont_t* fnt, const char* str, vec2 bbox);

struct ctext_text_render_info_t
{
  mat4f            model;
  ctext_hori_align horizontal;
  ctext_vert_align vertical;
  vec4f            color;
  vec3f            position;
  flt_t            scale;         // if scale_for_fit is 1, this is multiplied by the calculated scale.
  vec2             bbox;          // The bounding box that the scale will be determined for. Only when scale_for_fit is 1
  bool             scale_for_fit; // calculates the scale needed to fit the text into a box
};

struct ctext_glyph_t
{
  flt_t x0, x1, y0, y1;
  flt_t l, r, b, t;
  flt_t advance;
};

/* Internal CFont struct. Do not modify yourselves! */
struct cfont_t
{
  flt_t line_height;
  flt_t space_width;

  // nv_async_task_t load_task;

  nv_gpu_texture  texture;
  nv_gpu_memory_t texture_mem;
  nv_gpu_sampler  sampler;

  size_t          allocated_size;
  nv_gpu_buffer_t buffer;
  nv_gpu_memory_t buffer_mem;
  void*           mapped;

  size_t index_buffer_offset;
  size_t index_count;
  bool   to_render;
  bool   buffer_resized;
  bool   rendered_this_frame;

  size_t       chars_drawn;
  nv_list_t    drawcalls;
  nv_hashmap_t glyph_map;

  struct nv_renderer_t* rd;
};

static inline ctext_text_render_info_t
ctext_init_text_render_info(void)
{
  return (ctext_text_render_info_t){
    .model         = m4finit(1.0f),
    .horizontal    = CTEXT_HORI_ALIGN_CENTER,
    .vertical      = CTEXT_VERT_ALIGN_CENTER,
    .color         = (vec4f){ 1.0f, 1.0f, 1.0f, 1.0f },
    .position      = (vec3f){ 0.0f, 0.0f, 0.0f },
    .scale         = 1.0f,
    .bbox          = nv_zero_init(vec2),
    .scale_for_fit = false,
  };
}

NOVA_HEADER_END

#endif
