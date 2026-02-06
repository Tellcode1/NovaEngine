#include "../../include/iris/mesh.h"

void
iris_mesh_draw(nv_renderer_t* rdr, iris_mesh_t* mesh)
{
  if (!rdr->will_render_this_frame)
    return;

  VkCommandBuffer cmd = nv_rdr_get_draw_buffer(rdr);

  // bind pipeline and descriptors
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mesh->pipeline);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mesh->pipeline_layout, 0, mesh->descriptor_set_count, mesh->descriptor_sets, 0, NULL);

  // bind vertex and index buffers
  VkDeviceSize offsets[1]       = { mesh->vertex_offset };
  VkBuffer     vertex_buffers[] = { iris_buffer_get_backing(mesh->vertex_buffer) };
  vkCmdBindVertexBuffers(cmd, 0, 1, vertex_buffers, offsets);

  if (mesh->index_count > 0)
  {
    vkCmdBindIndexBuffer(cmd, iris_buffer_get_backing(mesh->index_buffer), mesh->index_offset, VK_INDEX_TYPE_UINT32);
  }

  vkCmdPushConstants(cmd, mesh->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, mesh->push_constants_size, mesh->push_constants);

  if (mesh->index_count > 0)
  {
    vkCmdDrawIndexed(cmd, mesh->index_count, 1, 0, 0, 0);
  }
  else
  {
    vkCmdDraw(cmd, mesh->vertex_count, 1, 0, 0);
  }
}