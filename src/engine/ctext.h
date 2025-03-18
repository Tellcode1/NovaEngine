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

extern void ctext_load_font(nv_renderer_t* rd, const char* font_path, int scale, cfont_t* dst);

/*
  Returns 0 if the font is ok and anything else if it is on life support (hasn't crashed your program yet)
*/
extern int ctext_validate_font(const cfont_t* fnt);

extern void ctext_destroy_font(cfont_t* fnt);

extern void ctext_render(cfont_t* fnt, const ctext_text_render_info_t* pInfo, const char* fmt, ...);

extern void ctext_flush_renders(nv_renderer_t* rd);
extern void _ctext_flush_font(nv_renderer_t* rd, cfont_t* fnt);

// Get the scale needed to fit the string in a box
// The scale is calculated as if both the string and the box were at (0,0)
extern flt_t ctext_get_scale_for_fit(const cfont_t* fnt, const char* str, vec2 bbox);

struct ctext_text_render_info_t
{
  mat4f            m_model;
  ctext_hori_align m_horizontal;
  ctext_vert_align m_vertical;
  vec4f            m_color;
  vec3f            m_position;
  flt_t            m_scale;         // if scale_for_fit is 1, this is multiplied by the calculated scale.
  vec2             m_bbox;          // The bounding box that the scale will be determined for. Only when scale_for_fit is 1
  bool             m_scale_for_fit; // calculates the scale needed to fit the text into a box
};

struct ctext_glyph_t
{
  flt_t m_x0, m_x1, m_y0, m_y1;
  flt_t m_l, m_r, m_b, m_t;
  flt_t m_advance;
};

/* Internal CFont struct. Do not modify yourselves! */
struct cfont_t
{
  flt_t m_line_height;
  flt_t m_space_width;

  // nv_async_task_t load_task;

  nv_gpu_texture* m_texture;
  nv_gpu_memory_t m_texture_mem;
  nv_gpu_sampler* m_sampler;

  size_t          m_allocated_size;
  nv_gpu_buffer_t m_buffer;
  nv_gpu_memory_t m_buffer_mem;
  void*           m_mapped;

  size_t m_index_buffer_offset;
  size_t m_index_count;
  bool   m_to_render;
  bool   m_buffer_resized;
  bool   m_rendered_this_frame;

  size_t       m_chars_drawn;
  nv_list_t    m_drawcalls;
  nv_hashmap_t m_glyph_map;

  struct nv_renderer_t* m_rd;
};

static inline ctext_text_render_info_t
ctext_init_text_render_info(void)
{
  return (ctext_text_render_info_t){
    .m_model         = m4finit(1.0f),
    .m_horizontal    = CTEXT_HORI_ALIGN_CENTER,
    .m_vertical      = CTEXT_VERT_ALIGN_CENTER,
    .m_color         = (vec4f){ 1.0f, 1.0f, 1.0f, 1.0f },
    .m_position      = (vec3f){ 0.0f, 0.0f, 0.0f },
    .m_scale         = 1.0f,
    .m_bbox          = nv_zero_init(vec2),
    .m_scale_for_fit = false,
  };
}

NOVA_HEADER_END

#endif
