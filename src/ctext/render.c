#include "../../external/volk/volk.h"
#include "../../include/ctext/ctext.h"
#include "../../include/ctext/fontc.h"
#include "../../include/engine/camera.h"
#include "../../include/engine/renderer.h"
#include "../../include/iris/buffer.h"
#include "../../include/iris/descriptors.h"
#include "../../include/iris/pipeline.h"
#include "../../include/iris/ringbuffer.h"
#include "../../include/std/include/alloc.h"
#include "../../include/std/include/containers/hashmap.h"
#include "../../include/std/include/containers/list.h"
#include "../../include/std/include/errorcodes.h"
#include "../../include/std/include/math/mat.h"
#include "../../include/std/include/math/math.h"
#include "../../include/std/include/math/vec2.h"
#include "../../include/std/include/math/vec3.h"
#include "../../include/std/include/print.h"
#include "../../include/std/include/stdafx.h"
#include "../../include/std/include/string.h"
#include "../../include/std/include/types.h"

#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

static inline bool
ctext_font_resize_buffer_if_needed(nv_renderer* rd, cfont_t* fnt, size_t minimum_size)
{
  if (ctext_validate_font(fnt) != 0)
  {
    // nv_log_error("Broken font\n");
    return false;
  }

  // Also resize if we've less frames than required
  // The first frame somehow initializes frames_in_flight to 1, which is later set to its correct value
  // And besides, we don't change frames_in_flight often

  size_t       new_slice_size       = 0;
  size_t const allocated_slice_size = fnt->buffer.slice_size;

  if (allocated_slice_size < minimum_size)
  {
    new_slice_size = NV_MAX(allocated_slice_size * 2, minimum_size);
  }
  else if (minimum_size < (allocated_slice_size / 3))
  {
    new_slice_size = NV_MAX(allocated_slice_size / 3, minimum_size);
  }
  // We have an equal number of frames as the renderer, no resize needed at all.
  else if (nv_rdr_get_frames_in_flight(rd) == fnt->buffer.frames_in_flight)
  {
    return false;
  }

  const size_t frames_in_flight = nv_rdr_get_frames_in_flight(fnt->rd);
  iris_ring_buffer_resize(&fnt->buffer, new_slice_size, 4, frames_in_flight, false);
  iris_buffer_resize(&fnt->staging_buffer, new_slice_size, 4, false);

  // We rebuilt the buffer this frame
  // So this frame (the vertices that we just generated) are sent to hell
  // Glad to know I won't be alone there.
  fnt->to_render = false;

  return true;
}

static inline void
ctext_render_drawcalls(nv_renderer_t* rd, cfont_t* fnt)
{
  if (ctext_validate_font(fnt) != 0 || !rd->will_render_this_frame)
  {
    // nv_log_error("Broken font\n");
    return;
  }

  VkCommandBuffer cmd = nv_rdr_get_draw_buffer(rd);

  const size_t       vertex_buffer_offset = iris_ring_buffer_offset(&fnt->buffer);
  const size_t       index_buffer_offset  = vertex_buffer_offset + fnt->index_buffer_offset;
  const VkDeviceSize offsets[1]           = { vertex_buffer_offset };

  struct ctext_push_constants pc = nv_zero_init(struct ctext_push_constants);

  VkPipeline       pipeline        = g_Pipelines.ctext.pipeline;
  VkPipelineLayout pipeline_layout = g_Pipelines.ctext.pipeline_layout;

  const VkDescriptorSet sets[]           = { camera.descriptor_sets->set, rd->ctext->desc_set->set };
  const uint32_t        camera_ub_offset = nv_camera_get_read_offset(&camera);

  // Viewport && scissor are set by renderer so no need to set them here
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, nv_arrlen(sets), sets, 1, &camera_ub_offset);
  vkCmdBindVertexBuffers(cmd, 0, 1, &fnt->buffer.backing.handle, offsets);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  vkCmdBindIndexBuffer(cmd, fnt->buffer.backing.handle, index_buffer_offset, VK_INDEX_TYPE_UINT32);

  size_t offset = 0;
  for (size_t i = 0; i < nv_list_size(&fnt->drawcalls); i++)
  {
    ctext_drawcall_t* drawcall = (ctext_drawcall_t*)nv_list_get(&fnt->drawcalls, i);

    const mat4 scale     = m4scale(m4init(1.0), v3init(drawcall->scale, drawcall->scale, 1.0));
    const mat4 rotate    = m4rotatev(scale, drawcall->rotation);
    const mat4 translate = m4translate(rotate, drawcall->position);

    mat4 final_model = m4mul(drawcall->model, translate);

    nvm_mat_copy(pc.model, final_model);
    nvm_vec_copy(pc.color, drawcall->color);
    pc.scale                = (float)drawcall->scale;
    pc.is_orthographic_proj = !drawcall->perspective_projection;

    vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(struct ctext_push_constants), &pc);

    // vkCmdDrawIndexed(cmd, drawcall->index_count, 1, (uint32_t)offset, 0, 0);
    // offset += drawcall->index_count;

    vkCmdDrawIndexed(cmd, drawcall->index_count, 1, 0, (int32_t)offset, 0);
    offset += drawcall->vertex_count;
  }

  iris_ring_buffer_next(&fnt->buffer);
}

static nv_list_t
split_string_by_lines(char* buffer)
{
  nv_list_t result;
  nv_list_init(sizeof(char*), 16, nv_allocator_c, NULL, &result);

  char* start = buffer;

  for (char* p = buffer; *p; p++)
  {
    if (*p == '\r' || *p == '\n')
    {
      *p = '\0'; // terminate substring
      nv_list_push_back(&result, &start);

      // CRLF handling
      if (*p == '\r' && *(p + 1) == '\n')
        p++;

      start = p + 1; // move to next segment
    }
  }

  if (*start) // last line
    nv_list_push_back(&result, &start);

  return result;
}

// Get the unscaled size of the string
void
ctext_get_text_size(const cfont_t* fnt, const char* str, vec2* dst)
{
  if (ctext_validate_font(fnt) != 0)
  {
    // nv_log_error("Broken font\n");
    dst->x = 0.0;
    dst->y = 0.0;
    return;
  }

  double width = 0.0f, height = fnt->line_height, prev_width = 0.0f;

  bool is_new_line = true;

  u32 codepoint = 0;

  while (*str != 0)
  {
    codepoint = (u32)((uchar)*str);
    switch (*str)
    {
      case ' ':
        width += fnt->space_width;
        is_new_line = false;
        break;
      case '\t':
        width += fnt->space_width * 4.0f;
        is_new_line = false;
        break;
      case '\n':
      case '\r':
        if (!is_new_line)
        {
          height += fnt->line_height;
          prev_width = NV_MAX(width, prev_width);
        }
        width       = 0.0f;
        is_new_line = true;
        break;
      default:
      {
        const fontc_glyph_t* glyph = (fontc_glyph_t*)nv_hashmap_find(&fnt->glyph_map, &codepoint);
        if (glyph == NULL)
        {
          break;
        }
        width += glyph->advance_x256 * (1.0 / 256.0);
        is_new_line = false;
        break;
      }
    }
    str++;
  }

  if (!is_new_line)
  {
    prev_width = NV_MAX(width, prev_width);
  }

  if (dst != NULL)
  {
    dst->x = prev_width;
    dst->y = -height;
  }
}

static inline void
gen_vert_data_for_char(const cfont_t* fnt, const ctext_drawcall_t* drawcall, u32 codepoint, vec2* offset, size_t* chars_drawn)
{
  if (codepoint == (u32)' ')
  {
    offset->x += fnt->space_width;
    return;
  }
  else if (codepoint == (u32)'\t')
  {
    offset->x += fnt->space_width * 4.0f;
    return;
  }

  const fontc_glyph_t* glyph = (const fontc_glyph_t*)nv_hashmap_find(&fnt->glyph_map, &codepoint);
  if (glyph == NULL)
  {
    // nv_log_info("no glyph when rendering char [%i|ASCII:%c]\n", codepoint, (char)codepoint);
    return;
  }

  // Since we are providing vertices to the GPU in floats, we have to convert here from doubles
  const float x0 = (float)((glyph->x0) + offset->x);
  const float x1 = (float)((glyph->x1) + offset->x);
  const float y0 = (float)((glyph->y0) + offset->y);
  const float y1 = (float)((glyph->y1) + offset->y);

  const size_t          index_offset = *chars_drawn * 4;
  ctext_glyph_vertex_t* v_out        = drawcall->vertices + (*chars_drawn * 4); // 4 characters per glyph

  // can we just use 2^16 here? It'll be like a tad bit less precise but theoretically faster?
  // since compiler can just issue multiply by 1/2^16 which can be perfectly represented?
  // TODO: test theory
  const float l = glyph->l / (float)UINT16_MAX;
  const float b = glyph->b / (float)UINT16_MAX;
  const float r = glyph->r / (float)UINT16_MAX;
  const float t = glyph->t / (float)UINT16_MAX;

  // clang-format off
    v_out[0] = (ctext_glyph_vertex_t){ (vec3f){x0, y0, 0.0F}, (vec2f){l, b} };
    v_out[1] = (ctext_glyph_vertex_t){ (vec3f){x1, y0, 0.0F}, (vec2f){r, b} };
    v_out[2] = (ctext_glyph_vertex_t){ (vec3f){x1, y1, 0.0F}, (vec2f){r, t} };
    v_out[3] = (ctext_glyph_vertex_t){ (vec3f){x0, y1, 0.0F}, (vec2f){l, t} };
  // clang-format on

  u32* i_out = drawcall->indices + (*chars_drawn * 6);
  i_out[0]   = index_offset;
  i_out[1]   = index_offset + 1;
  i_out[2]   = index_offset + 2;
  i_out[3]   = index_offset + 2;
  i_out[4]   = index_offset + 3;
  i_out[5]   = index_offset;

  offset->x += glyph->advance_x256 * (1.0 / 256.0);
  (*chars_drawn)++;
}

static inline size_t
ctext_get_effective_length(const char* buf, size_t buflen)
{
  // I've tried to use isprint here
  // It causes some weird artefacts for some damned reason.
  size_t len = 0;
  for (size_t i = 0; i < buflen; i++)
  {
    char const c = buf[i];
    if (!(bool)isprint(c))
    {
      continue;
    }
    len++;
  }
  return len;
}

static inline int
ctext_gen_vertices(cfont_t* fnt, ctext_drawcall_t* drawcall, const ctext_text_render_info_t* pInfo, char* buffer)
{
  if ((buffer == NULL) || *buffer == 0) // nv_strlen == 0
  {
    return 1;
  }
  if (ctext_validate_font(fnt) != 0)
  {
    // nv_log_error("Broken font\n");
    return -1;
  }

  nv_list_t lines;

  vec2 text_size       = v2zero;
  vec2 position_offset = v2zero;

  lines = split_string_by_lines(buffer);
  if (nv_list_size(&lines) == 0)
  {
    return 0;
  }

  ctext_get_text_size(fnt, buffer, &text_size);

  // text_h = fnt->line_height * (double)(nv_list_size(&lines) - 1);

  if (pInfo->scale_for_fit)
  {
    double scale_x = (pInfo->bbox.x) / text_size.x;
    double scale_y = (pInfo->bbox.y) / text_size.y;
    // multiply with normal scale to get new scale
    drawcall->scale *= NV_MIN(scale_x, scale_y);
  }

  position_offset.y = 0.0;
  switch (pInfo->vertical)
  {
    case CTEXT_VERT_ALIGN_CENTER: position_offset.y -= text_size.y / 2.0; break;
    case CTEXT_VERT_ALIGN_BOTTOM: position_offset.y -= text_size.y; break;
    case CTEXT_VERT_ALIGN_TOP: break; // already at top
  }

  size_t chars_drawn = 0;

  for (size_t i = 0; i < nv_list_size(&lines); i++)
  {
    const char* line = ((char**)nv_list_data(&lines))[i];

    ctext_get_text_size(fnt, line, &text_size);

    position_offset.x = 0.0;
    switch (pInfo->horizontal)
    {
      case CTEXT_HORI_ALIGN_CENTER: position_offset.x -= text_size.x / 2.0f; break;
      case CTEXT_HORI_ALIGN_RIGHT: position_offset.x -= text_size.x; break;
      case CTEXT_HORI_ALIGN_LEFT: break;
    }

    for (const char* ch = line; *ch != 0; ch++)
    {
      gen_vert_data_for_char(fnt, drawcall, (u32)*ch, &position_offset, &chars_drawn);
    }

    position_offset.x += fnt->line_height;
  }

  nv_list_destroy(&lines);

  return 0;
}

// TODO: Replace with a better system
// that renders the characters all at once.
static inline void
ctext_render_and_queue_drawcall(cfont_t* fnt, const ctext_text_render_info_t* pInfo, char* buffer, size_t buffer_size)
{
  if (ctext_validate_font(fnt) != 0)
  {
    nv_raise_error(NV_ERROR_BROKEN_STATE, "Broken font\n");
    return;
  }

  size_t const effective_length = ctext_get_effective_length(buffer, buffer_size);
  if (effective_length == 0)
  {
    return;
  }

  const size_t vertex_size     = (effective_length * 4) * sizeof(ctext_glyph_vertex_t);
  const size_t index_size      = (effective_length * 6) * sizeof(u32);
  const size_t allocation_size = vertex_size + index_size;

  void* allocation = nv_calloc(allocation_size);

  ctext_drawcall_t drawcall = nv_zero_init(ctext_drawcall_t);
  drawcall.vertices         = (ctext_glyph_vertex_t*)allocation;
  drawcall.index_offset     = vertex_size;
  drawcall.indices          = (u32*)((uchar*)allocation + vertex_size);

  nvm_vec_copy(drawcall.color, pInfo->color);
  nvm_mat_copy(drawcall.model, pInfo->model);
  drawcall.scale    = pInfo->scale;
  drawcall.position = pInfo->position;
  drawcall.rotation = pInfo->rotation;

  drawcall.perspective_projection = pInfo->perspective_projection;

  drawcall.vertex_count = effective_length * 4;
  drawcall.index_count  = effective_length * 6;

  // can we not have a bounds check before making the vertices?
  // probably not..
  if (ctext_gen_vertices(fnt, &drawcall, pInfo, buffer) != 0)
  {
    nv_free(allocation);
    return;
  }

  nv_list_push_back(&fnt->drawcalls, &drawcall);
}

void
ctext_render(cfont_t* fnt, const ctext_text_render_info_t* pInfo, const char* fmt, ...)
{
  if ((fnt == NULL) || (pInfo == NULL) || (fmt == NULL))
  {
    return;
  }

  // if (!nv_async_is_task_complete(&fnt->load_task)) { return; }
  if (ctext_validate_font(fnt) != 0)
  {
    // nv_log_error("Broken font\n");
    return;
  }

  va_list args;
  va_start(args, fmt);

  // the +1 is for NULL terminator
  size_t const buffer_size = nv_vsnprintf(args, NULL, SIZE_MAX, fmt) + 1;

  va_end(args);

  if (buffer_size == 0)
  {
    return;
  }

  va_start(args, fmt);

  char* buffer = (char*)nv_malloc(buffer_size * sizeof(char));
  nv_vsnprintf(args, buffer, buffer_size, fmt);

  va_end(args);

  ctext_render_and_queue_drawcall(fnt, pInfo, buffer, buffer_size);

  nv_free(buffer);

  fnt->rendered_this_frame = true;
}

static inline void
ctext_upload_vertices_and_render_drawcalls(cfont_t* fnt)
{
  u32 vertices_size = 0;
  u32 index_count   = 0;

  for (size_t i = 0; i < nv_list_size(&fnt->drawcalls); i++)
  {
    const ctext_drawcall_t* drawcall = (ctext_drawcall_t*)nv_list_get(&fnt->drawcalls, i);
    vertices_size += drawcall->vertex_count * sizeof(ctext_glyph_vertex_t);
    index_count += drawcall->index_count;
  }

  if (vertices_size == 0 || index_count == 0)
  {
    return;
  }

  const u32 indices_size = index_count * sizeof(u32);
  const u32 buffer_size  = indices_size + vertices_size;

  nv_renderer_t* rd = fnt->rd;

  // TODO: We need atleast frames_in_flight sets of the buffer for maximum efficiency
  const bool fnt_buffer_resized = ctext_font_resize_buffer_if_needed(rd, fnt, buffer_size);

  if (fnt->to_render && !fnt_buffer_resized)
  {
    ctext_render_drawcalls(rd, fnt);
  }

  uint8_t* write_cache = (uint8_t*)nv_calloc(buffer_size);
  if (write_cache == NULL)
  {
    return;
  }

  // this may be dumb but I am too

  u32 vertex_copy_iterator = 0;
  u32 index_copy_iterator  = 0;
  for (size_t i = 0; i < nv_list_size(&fnt->drawcalls); i++)
  {
    const ctext_drawcall_t* drawcall = (ctext_drawcall_t*)nv_list_get(&fnt->drawcalls, i);

    size_t const drawcall_vertices_size = drawcall->vertex_count * sizeof(ctext_glyph_vertex_t);
    size_t const drawcall_indices_size  = drawcall->index_count * sizeof(u32);

    nv_memcpy(write_cache + vertex_copy_iterator, drawcall->vertices, drawcall_vertices_size);
    nv_memcpy(write_cache + vertices_size + index_copy_iterator, drawcall->indices, drawcall_indices_size);

    vertex_copy_iterator += drawcall_vertices_size;
    index_copy_iterator += drawcall_indices_size;
  }

  fnt->index_buffer_offset = vertices_size;
  fnt->index_count         = index_count;
  fnt->to_render           = true;

  if (ctext_font_resize_buffer_if_needed(rd, fnt, buffer_size))
  {
    nv_free(write_cache);
    return;
  }

  iris_buffer_write_data(&fnt->staging_buffer, write_cache, buffer_size, 0);

  size_t write_offset = iris_ring_buffer_offset(&fnt->buffer);
  iris_buffer_copy(&fnt->buffer.backing, &fnt->staging_buffer, buffer_size, write_offset, 0);

  /**
   * The buffers are swapped after inserting the render commands
   * We'll get synchronization errors otherwise.
   */

  nv_free(write_cache);
}

void
ctext_flush_font_renders(cfont_t* fnt)
{
  if (!fnt->rendered_this_frame)
  {
    return;
  }
  else
  {
    fnt->rendered_this_frame = false;
  }

  ctext_upload_vertices_and_render_drawcalls(fnt);

  for (size_t i = 0; i < nv_list_size(&fnt->drawcalls); i++)
  {
    ctext_drawcall_t* drawcall = (ctext_drawcall_t*)nv_list_get(&fnt->drawcalls, i);
    if ((drawcall != NULL) && (drawcall->vertices != NULL))
    {
      nv_free(drawcall->vertices);
    }
  }
  nv_list_clear(&fnt->drawcalls);
}

void
ctext_flush_renders(nv_renderer_t* rd)
{
  for (size_t i = 0; i < nv_list_size(&rd->ctext->fonts); i++)
  {
    cfont_t* fnt = *(cfont_t**)nv_list_get(&rd->ctext->fonts, i);
    ctext_flush_font_renders(fnt);
  }
}
