#include "GPU/driver.h"

#include "GPU/dynamicbuffer.h"
#include "GPU/pipeline.h"
// #include "GPU/texture.h"
#include "GPU/types.h"
#include "GPU/vk_buffer_freelist.h"

#include "engine/camera.h"
#include "std/containers/list.h"
#include "std/errorcodes.h"
#include "std/stdafx.h"
#include "std/string.h"

#include "external/volk/volk.h"
#include <vulkan/vulkan_core.h>

static inline bool
has_flag(u32 flags, u32 want)
{
  return (flags & want) == 0;
}

nv_errorc
nvvk_driver_init(nvvk_ctx_t* ctx, nvvk_driver_t* dst)
{
  nv_return_if_fail(ctx != NULL, NV_ERROR_CODE_INVALID_ARG);
  nv_return_if_fail(dst != NULL, NV_ERROR_CODE_INVALID_ARG);

  nv_bzero(dst, sizeof(nvvk_driver_t));

  dst->ctx = ctx;

  nv_errorc code = NV_SUCCESS;

  /* NOTE: POINTERS. We're storing POINTERS */
  code = nv_list_init(sizeof(nv_gpu_dynamic_buffer_t*), 16, nv_allocator_c, NULL, &dst->buffers);
  nv_return_if_fail(code == NV_SUCCESS, code);

  code = nv_list_init(sizeof(nv_gpu_sampler_t), 16, nv_allocator_c, NULL, &dst->samplers);
  nv_return_if_fail(code == NV_SUCCESS, code);

  return NV_SUCCESS;
}

void
nvvk_driver_destroy(nvvk_driver_t* driver)
{
  if (!driver)
  {
    return;
  }

  if (nv_list_size(&driver->buffers) != 0)
  {
    nv_log_error("Driver still held %zu buffers at time of destruction.\n", nv_list_size(&driver->buffers));
  }

  for (size_t i = 0; i < nv_list_size(&driver->samplers); i++)
  {
    nv_gpu_sampler_t* sampler = (nv_gpu_sampler_t*)nv_list_get(&driver->samplers, i);
    if (sampler && sampler->vksampler)
    {
      vkDestroySampler(driver->ctx->device, sampler->vksampler, NOVA_VK_ALLOCATOR);
    }
  }

  nv_list_destroy(&driver->buffers);
  nv_list_destroy(&driver->samplers);

  nv_bzero(driver, sizeof(nvvk_driver_t));
}

nv_errorc
nv_gpu_buffer_freelist_init(vk_size_t capacity, nv_gpu_buffer_freelist_t* dst)
{
  nv_return_if_fail(capacity != 0, NV_ERROR_CODE_INVALID_ARG);
  nv_return_if_fail(dst != NULL, NV_ERROR_CODE_INVALID_ARG);

  dst->head = nv_calloc(sizeof(nv_gpu_buffer_freelist_block_t));
  if (!dst->head)
  {
    return NV_ERROR_MALLOC_FAILED;
  }

  dst->head->offset = 0;
  dst->head->size   = capacity;
  dst->head->next   = NULL;

  return NV_SUCCESS;
}

void
nv_gpu_buffer_freelist_destroy(nv_gpu_buffer_freelist_t* list)
{
  nv_return_if_fail(list != NULL, );

  nv_gpu_buffer_freelist_block_t* cur = list->head;
  while (cur)
  {
    nv_gpu_buffer_freelist_block_t* next = cur->next;
    nv_free(cur);
    cur = next;
  }
  list->head = NULL;
}

static void
insert_block(nv_gpu_buffer_freelist_t* list, nv_gpu_buffer_freelist_block_t* block)
{
  if (!list->head || block->offset < list->head->offset)
  {
    block->next = list->head;
    list->head  = block;
  }
  else
  {
    nv_gpu_buffer_freelist_block_t* cur = list->head;
    while (cur->next && cur->next->offset < block->offset)
    {
      cur = cur->next;
    }
    block->next = cur->next;
    cur->next   = block;
  }
}

// Coalesce adjacent blocks in the freelist
static void
coalesce(nv_gpu_buffer_freelist_t* list)
{
  nv_gpu_buffer_freelist_block_t* cur = list->head;
  while (cur && cur->next)
  {
    // if current blocks end equals next blocks offset, merge them
    if (cur->offset + cur->size == cur->next->offset)
    {
      nv_gpu_buffer_freelist_block_t* next = cur->next;
      cur->size += next->size;
      cur->next = next->next;

      // free the merged block
      nv_free(next);
    }
    else
    {
      cur = cur->next;
    }
  }
}

bool
nv_gpu_buffer_freelist__allocate(nv_gpu_buffer_freelist_t* list, vk_size_t size, vk_size_t* out_offset)
{
  nv_gpu_buffer_freelist_block_t* cur = list->head;

  nv_gpu_buffer_freelist_block_t* best_fit      = NULL;
  vk_size_t                       best_size_fit = __INT_MAX__;

  while (cur)
  {
    if (cur->size >= size && cur->size < best_size_fit)
    {
      best_fit      = cur;
      best_size_fit = cur->size;
    }
    cur = cur->next;
  }

  if (best_fit)
  {
    if (out_offset)
    {
      *out_offset = best_fit->offset;
    }
  }

  // If best_fit is NULL, evaluates to false, vice versa
  return best_fit != NULL;
}

void
nv_gpu_buffer_freelist_free(nv_gpu_buffer_freelist_t* list, vk_size_t offset, vk_size_t size)
{
  nv_gpu_buffer_freelist_block_t* block = nv_malloc(sizeof(nv_gpu_buffer_freelist_block_t));
  nv_return_if_fail(block != NULL, );

  block->offset = offset;
  block->size   = size;
  block->next   = NULL;

  insert_block(list, block);
  coalesce(list);
}

static inline nv_gpu_dynamic_buffer_t*
_get_buffer_for_transfer(nvvk_driver_t* driver, vk_size_t size)
{
  nv_return_if_fail(driver != NULL, NULL);
  nv_return_if_fail(size != 0, NULL);

  nv_gpu_dynamic_buffer_t* best_fit      = NULL;
  vk_size_t                best_size_fit = __INT_MAX__;

  for (size_t i = 0; i < nv_list_size(&driver->buffers); i++)
  {
    nv_gpu_dynamic_buffer_t* buffer = (nv_gpu_dynamic_buffer_t*)nv_list_get(&driver->buffers, i);

    if (buffer->drv_transfer_only && !buffer->drv_in_use && buffer->size < best_size_fit)
    {
      best_fit      = buffer;
      best_size_fit = buffer->size;
    }
  }

  return best_fit;
}

//  const vk_size_t aligned_size = ALIGN_UP(size, alignment);
//
//  if (has_flag(flags, NV_GPU_DYNAMIC_BUFFER_SINGLE_TIME_TRANSFER_BIT))
//  {
//    nv_gpu_dynamic_buffer_t* transfer_buffer = _get_buffer_for_transfer(driver, aligned_size);
//
//    if (transfer_buffer)
//    {
//      /* Note that no freelist or anything is created. we expect that it will just be a single transfer */
//      dst->driver = driver;
//      dst->size   = aligned_size;
//
//      /* Store driver info to tell that this buffer is just a pointer to another buffer */
//      dst->drv_transfer_only = true;
//      dst->drv_payload       = (void*)transfer_buffer;
//
//      return NV_SUCCESS;
//    }
//
//    /**
//     * no buffer was found and free. just create a new one.
//     * Also note that dst->drv_transfer_only is not set, because this
//     * buffer isn't a pointer to another.
//     */
//  }
//
//  VkBufferUsageFlags vk_buffer_flags = 0;
//  switch (flags)
//  {
//    /* This needs both src and transfer bits because when resizing, we copy from the buffer SRC to the new one that needs the DST */
//    case NV_GPU_DYNAMIC_BUFFER_RESIZABLE_BIT: vk_buffer_flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT; break;
//
//    /* TODO: Implement */
//    case NV_GPU_DYNAMIC_BUFFER_TRANSIENT_BIT: vk_buffer_flags |= 0;
//
//    /* Readbacks need transfer_src bit because we transfer to a temporary buffer, map it and copy the data over */
//    case NV_GPU_DYNAMIC_BUFFER_READBACK_CAPABLE_BIT:
//    /* This needs only the src bit because it's on host coherent memory  */
//    case NV_GPU_DYNAMIC_BUFFER_SINGLE_TIME_TRANSFER_BIT: vk_buffer_flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
//  }
//
//  VkMemoryAllocateFlags vk_property_flags = 0;
//  switch (flags)
//  {
//  }
//
//  VkBufferCreateInfo buffer_info = nv_zero_init(VkBufferCreateInfo);
//  buffer_info.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
//  buffer_info.size               = aligned_size;
//  buffer_info.usage              = vk_buffer_flags;
//  nvvk_result_check(*driver->ctx, vkCreateBuffer(driver->ctx->device, &buffer_info, NOVA_VK_ALLOCATOR, &dst->buffer));
//
//  VkMemoryRequirements memory_requirements;
//  vkGetBufferMemoryRequirements(driver->ctx->device, dst->buffer, &memory_requirements);
//
//  VkMemoryAllocateInfo allocInfo = nv_zero_init(VkMemoryAllocateInfo);
//  allocInfo.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
//  allocInfo.allocationSize       = memory_requirements.size;
//  allocInfo.memoryTypeIndex      = nv_vk_get_mem_type(driver->ctx, memory_requirements.memoryTypeBits, vk_property_flags);
//  nvvk_result_check(*driver->ctx, vkAllocateMemory(driver->ctx->device, &allocInfo, NOVA_VK_ALLOCATOR, &dst->memory));
//
//  nvvk_result_check(*driver->ctx, vkBindBufferMemory(driver->ctx->device, dst->buffer, dst->memory, 0));
//
//  dst->size      = memory_requirements.size;
//  dst->alignment = memory_requirements.alignment;
//  dst->flags     = flags;
//
//  return NV_SUCCESS;

static inline VkBufferUsageFlags
_nv_to_vk_buffer_usage(nv_gpu_dynamic_buffer_flags flags)
{
  VkBufferUsageFlags usage = 0;

  // Volatile buffer: accessed externally, must always be up-to-date
  if (flags & NV_GPU_DYNAMIC_BUFFER_VOLATILE_BIT)
  {
    // or any usage that implies frequent access
    usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  }

  if (flags & NV_GPU_DYNAMIC_BUFFER_TRANSIENT_BIT)
  {
    usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  }

  // needs to support GPU -> CPU transfers
  if (flags & NV_GPU_DYNAMIC_BUFFER_READBACK_CAPABLE_BIT)
  {
    usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  }

  if (flags & NV_GPU_DYNAMIC_BUFFER_VERTEX_BUFFER_BIT)
  {
    usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  }
  if (flags & NV_GPU_DYNAMIC_BUFFER_INDEX_BUFFER_BIT)
  {
    usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  }
  if (flags & NV_GPU_DYNAMIC_BUFFER_SS_BUFFER_BIT)
  {
    usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  }
  if (flags & NV_GPU_DYNAMIC_BUFFER_UNIFORM_BUFFER_BIT)
  {
    usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  }

  if (usage == 0)
  {
    usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  }

  return usage;
}

static inline VkMemoryPropertyFlags
_nv_to_vk_memory_properties(nv_gpu_dynamic_buffer_flags flags)
{
  VkMemoryPropertyFlags props = 0;

  // readback requires CPU visible memory
  if (flags & NV_GPU_DYNAMIC_BUFFER_READBACK_CAPABLE_BIT)
  {
    props |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  }

  if (flags & NV_GPU_DYNAMIC_BUFFER_PERSISTENT_MAPPED)
  {
    props |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
  }

  // transient/volatile buffers are often best in device-local memory
  /* Vertex, index and SS buffers need to be in device local memory. */
  if ((flags & NV_GPU_DYNAMIC_BUFFER_TRANSIENT_BIT) || (flags & NV_GPU_DYNAMIC_BUFFER_VOLATILE_BIT) || (flags & NV_GPU_DYNAMIC_BUFFER_VERTEX_BUFFER_BIT)
      || (flags & NV_GPU_DYNAMIC_BUFFER_INDEX_BUFFER_BIT) || (flags & NV_GPU_DYNAMIC_BUFFER_SS_BUFFER_BIT))
  {
    props |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  }

  if (flags & NV_GPU_DYNAMIC_BUFFER_CPU_VISIBLE)
  {
    props |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
  }

  // device local if nothing else is specified
  if (props == 0)
  {
    props = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  }

  return props;
}

nv_errorc
nv_gpu_dynamic_buffer_init(nvvk_driver_t* driver, vk_size_t size, size_t alignment, nv_gpu_dynamic_buffer_flags flags, nv_gpu_dynamic_buffer_t* dst)
{
  nv_return_if_fail(driver != NULL, NV_ERROR_CODE_INVALID_ARG);
  nv_return_if_fail(size != 0, NV_ERROR_CODE_INVALID_ARG);
  nv_return_if_fail(dst != NULL, NV_ERROR_CODE_INVALID_ARG);

  nv_bzero(dst, sizeof(nv_gpu_dynamic_buffer_t));

  const vk_size_t aligned_size = ALIGN_UP(size, alignment);

  VkMemoryAllocateFlags vk_property_flags = _nv_to_vk_memory_properties(flags);
  VkBufferUsageFlags    vk_buffer_flags   = _nv_to_vk_buffer_usage(flags);

  VkBufferCreateInfo buffer_info = nv_zero_init(VkBufferCreateInfo);
  buffer_info.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size               = aligned_size;
  buffer_info.usage              = vk_buffer_flags;
  nvvk_result_check(*driver->ctx, vkCreateBuffer(driver->ctx->device, &buffer_info, NOVA_VK_ALLOCATOR, &dst->buffer));
  nv_return_if_fail(dst->buffer != VK_NULL_HANDLE, NV_ERROR_CODE_EXTERNAL);

  VkMemoryRequirements memory_requirements;
  vkGetBufferMemoryRequirements(driver->ctx->device, dst->buffer, &memory_requirements);
  nv_return_if_fail(memory_requirements.size != 0, NV_ERROR_CODE_EXTERNAL);
  nv_return_if_fail(memory_requirements.alignment != 0, NV_ERROR_CODE_EXTERNAL);

  VkMemoryAllocateInfo allocInfo = nv_zero_init(VkMemoryAllocateInfo);
  allocInfo.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize       = memory_requirements.size;
  allocInfo.memoryTypeIndex      = nv_vk_get_mem_type(driver->ctx, memory_requirements.memoryTypeBits, vk_property_flags);
  nvvk_result_check(*driver->ctx, vkAllocateMemory(driver->ctx->device, &allocInfo, NOVA_VK_ALLOCATOR, &dst->memory));
  nv_return_if_fail(dst->memory != VK_NULL_HANDLE, NV_ERROR_CODE_EXTERNAL);

  nvvk_result_check(*driver->ctx, vkBindBufferMemory(driver->ctx->device, dst->buffer, dst->memory, 0));

  dst->size      = memory_requirements.size;
  dst->alignment = memory_requirements.alignment;
  dst->flags     = flags;

  if (flags & NV_GPU_DYNAMIC_BUFFER_PERSISTENT_MAPPED)
  {
    VkResult result = vkMapMemory(driver->ctx->device, dst->memory, 0, size, 0, &dst->drv_mapped);
    nv_return_if_fail(result == VK_SUCCESS, NV_ERROR_CODE_EXTERNAL);
    nv_return_if_fail(dst->drv_mapped != NULL, NV_ERROR_CODE_EXTERNAL);

    dst->drv_mapped_size   = size;
    dst->drv_mapped_offset = 0;
  }

  return NV_SUCCESS;
}

void
nv_gpu_dynamic_buffer_destroy(nv_gpu_dynamic_buffer_t* buffer)
{
  if (!buffer)
  {
    return;
  }

  nvvk_ctx_t* ctx = buffer->driver->ctx;

  vkDestroyBuffer(ctx->device, buffer->buffer, NULL);
  vkFreeMemory(ctx->device, buffer->memory, NULL);
}

static inline nv_errorc
_nv_transfer_to_cpu_visible_buffer(nv_gpu_dynamic_buffer_t* buffer, const void* data, vk_size_t data_size, vk_size_t offset)
{
  VkDevice device  = buffer->driver->ctx->device;
  void*    mapping = NULL;

  VkResult map_result = vkMapMemory(device, buffer->memory, offset, data_size, 0, &mapping);
  if (map_result != VK_SUCCESS)
  {
    return NV_ERROR_CODE_EXTERNAL;
  }

  nv_memcpy(mapping, data, data_size);

  VkMappedMemoryRange range = {
    .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
    .memory = buffer->memory,
    .offset = offset,
    .size   = data_size,
  };
  vkFlushMappedMemoryRanges(device, 1, &range);

  vkUnmapMemory(device, buffer->memory);

  return NV_SUCCESS;
}

static inline nv_errorc
_nv_stage_transfer_to_buffer(nv_gpu_dynamic_buffer_t* buffer, const void* data, vk_size_t data_size, vk_size_t offset)
{
  nvvk_ctx_t* const nvvkctx = buffer->driver->ctx;

  nv_gpu_buffer_t staging_buffer;
  nv_gpu_create_buffer(nvvkctx, data_size, NOVA_GPU_ALIGNMENT_UNNECESSARY, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &staging_buffer);

  nv_gpu_memory_t staging_memory;
  nv_gpu_allocate_memory(nvvkctx, data_size, NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE | NOVA_GPU_MEMORY_USAGE_CPU_COHERENT, &staging_memory);

  nv_gpu_bind_buffer_to_memory(nvvkctx, &staging_memory, 0, &staging_buffer);

  void* mapped = NULL;
  nv_gpu_map_memory(nvvkctx, &staging_memory, data_size, 0, &mapped);
  nv_memcpy(mapped, data, data_size);
  nv_gpu_unmap_memory(nvvkctx, &staging_memory);

  VkCommandBuffer cmd = nv_vk_begin_command_buffer(nvvkctx);

  VkBufferCopy copy = {
    .srcOffset = 0,
    .dstOffset = offset,
    .size      = data_size,
  };
  vkCmdCopyBuffer(cmd, staging_buffer.buffer, buffer->buffer, 1, &copy);

  if (nv_vk_end_command_buffer(buffer->driver->ctx, cmd, nvvkctx->graphics_queue, 1) != VK_SUCCESS)
  {
    nv_log_error("Failed to write data to GPU buffer\n");
    return NV_ERROR_CODE_BROKEN_STATE;
  }

  return NV_SUCCESS;
}

nv_errorc
nv_gpu_dynamic_buffer_write_data(nv_gpu_dynamic_buffer_t* buffer, const void* data, vk_size_t data_size, vk_size_t offset)
{
  nv_return_if_fail(buffer != NULL, NV_ERROR_CODE_INVALID_ARG);
  nv_return_if_fail(data != NULL, NV_ERROR_CODE_INVALID_ARG);
  nv_return_if_fail(data_size != 0, NV_ERROR_CODE_INVALID_ARG);
  nv_return_if_fail(offset <= buffer->size, NV_ERROR_CODE_INVALID_ARG);

  if (buffer->flags & NV_GPU_DYNAMIC_BUFFER_CPU_VISIBLE)
  {
    return _nv_transfer_to_cpu_visible_buffer(buffer, data, data_size, offset);
  }

  _nv_stage_transfer_to_buffer(buffer, data, data_size, offset);

  return NV_SUCCESS;
}

nv_errorc
nv_gpu_dynamic_buffer_map_memory(nv_gpu_dynamic_buffer_t* buffer, vk_size_t size, vk_size_t offset, void** mapping)
{
  nv_return_if_fail(buffer != NULL, NV_ERROR_CODE_INVALID_ARG);
  nv_return_if_fail(size != 0, NV_ERROR_CODE_INVALID_ARG);
  nv_return_if_fail(offset < buffer->size, NV_ERROR_CODE_INVALID_ARG);
  nv_return_if_fail(has_flag(buffer->flags, NV_GPU_DYNAMIC_BUFFER_CPU_VISIBLE) == true, NV_ERROR_CODE_INVALID_ARG);

  /* If we're already mapped and the current mapping isn't out of bounds of the request */
  if ((buffer->flags & NV_GPU_DYNAMIC_BUFFER_CPU_VISIBLE) && buffer->drv_mapped != NULL && buffer->drv_mapped_size >= size && buffer->drv_mapped_offset <= offset)
  {
    *mapping = (unsigned char*)buffer->drv_mapped + offset;
    return NV_SUCCESS;
  }

  VkDevice device = buffer->driver->ctx->device;

  /* being CPU visible is an assertion */
  VkResult result = vkMapMemory(device, buffer->memory, 0, size, 0, &buffer->drv_mapped);
  nv_return_if_fail(result == VK_SUCCESS, NV_ERROR_CODE_EXTERNAL);
  nv_return_if_fail(buffer->drv_mapped != NULL, NV_ERROR_CODE_EXTERNAL);

  buffer->drv_mapped_size   = size;
  buffer->drv_mapped_offset = offset;

  return NV_SUCCESS;
}

nv_errorc
nv_gpu_dynamic_buffer_unmap_memory(nv_gpu_dynamic_buffer_t* buffer)
{
  nv_return_if_fail(buffer != NULL, NV_ERROR_CODE_INVALID_ARG);
  nv_return_if_fail(buffer->drv_mapped_size != 0, NV_ERROR_CODE_INVALID_ARG);
  nv_return_if_fail(buffer->drv_mapped_offset < buffer->size, NV_ERROR_CODE_INVALID_ARG);
  nv_return_if_fail(has_flag(buffer->flags, NV_GPU_DYNAMIC_BUFFER_PERSISTENT_MAPPED) == false, NV_ERROR_CODE_INVALID_ARG);

  VkDevice device = buffer->driver->ctx->device;

  /* because we never really have host_coherent_bit in vk flags, we must always flush the memory */
  VkMappedMemoryRange range = {
    .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
    .memory = buffer->memory,
    .offset = buffer->drv_mapped_offset,
    .size   = buffer->drv_mapped_size,
  };
  vkFlushMappedMemoryRanges(device, 1, &range);

  vkUnmapMemory(device, buffer->memory);

  buffer->drv_mapped        = NULL;
  buffer->drv_mapped_size   = 0;
  buffer->drv_mapped_offset = 0;

  return NV_SUCCESS;
}

static inline nv_errorc
_readback_buffer_staged(nv_gpu_dynamic_buffer_t* buffer, vk_size_t size, vk_size_t offset, void* dst)
{
  if (!(_nv_to_vk_buffer_usage(buffer->flags) & VK_BUFFER_USAGE_TRANSFER_SRC_BIT))
  {
    /* TODO: work around? */
    nv_log_error("Cannot readback from buffer that is not transfer source\n");
    return NV_ERROR_CODE_INVALID_ARG;
  }

  nvvk_ctx_t* nvvkctx = buffer->driver->ctx;

  nv_gpu_buffer_t staging;
  nv_gpu_memory_t staging_mem;
  nv_gpu_create_buffer(nvvkctx, buffer->size, NOVA_GPU_ALIGNMENT_UNNECESSARY, NOVA_GPU_BUFFER_USAGE_TRANSFER_DESTINATION, &staging);
  nv_gpu_allocate_memory(nvvkctx, buffer->size, NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE, &staging_mem);
  nv_gpu_bind_buffer_to_memory(nvvkctx, &staging_mem, 0, &staging);

  VkCommandBuffer cmd = nv_vk_begin_command_buffer(nvvkctx);

  VkBufferCopy copy = { .srcOffset = offset, .dstOffset = 0, .size = size };
  vkCmdCopyBuffer(cmd, buffer->buffer, staging.buffer, 1, &copy);

  nv_vk_end_command_buffer(nvvkctx, cmd, nvvkctx->transfer_queue, 1);

  void* mapped = NULL;
  nv_gpu_map_memory(nvvkctx, &staging_mem, buffer->size, 0, &mapped);
  nv_assert(mapped != NULL);
  nv_memcpy(dst, mapped, buffer->size);
  nv_gpu_unmap_memory(nvvkctx, &staging_mem);

  nv_gpu_destroy_buffer(nvvkctx, &staging);
  nv_gpu_free_memory(nvvkctx, &staging_mem);

  return NV_SUCCESS;
}

nv_errorc
nv_gpu_dynamic_buffer_readback(nv_gpu_dynamic_buffer_t* buffer, vk_size_t size, vk_size_t offset, void* dst)
{
  nv_return_if_fail(buffer != NULL, NV_ERROR_CODE_INVALID_ARG);
  nv_return_if_fail(size <= buffer->size, NV_ERROR_CODE_INVALID_ARG);
  nv_return_if_fail((size + offset) < buffer->size, NV_ERROR_CODE_INVALID_ARG);
  nv_return_if_fail(dst != NULL, NV_ERROR_CODE_INVALID_ARG);

  /* persistent mapping implies the whole buffer is mapped, just read it in */
  if (buffer->flags & NV_GPU_DYNAMIC_BUFFER_PERSISTENT_MAPPED)
  {
    void* mapping = NULL;

    nv_errorc code = nv_gpu_dynamic_buffer_map_memory(buffer, size, offset, &mapping);
    nv_return_if_fail(code == NV_SUCCESS, code);

    nv_memcpy(dst, mapping, size);
    return NV_SUCCESS;
  }
  else if (buffer->drv_mapped && buffer->drv_mapped_size >= size && buffer->drv_mapped_offset <= offset)
  {
    /* that weird shenanigans with the offset is to make it absolute. The offset should be absolute to the buffer. */
    const unsigned char* src = (unsigned char*)buffer->drv_mapped + (buffer->drv_mapped_offset - offset);
    nv_memcpy(dst, src, size);
    return NV_SUCCESS;
  }

  /* nothing else worked, slowly stage a transfer from the CPU to the GPU */
  return _readback_buffer_staged(buffer, size, offset, dst);
}
