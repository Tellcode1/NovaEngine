#include "../../include/iris/buffer.h"
#include "../../external/volk/volk.h"
#include "../../include/iris/driver.h"
#include "../../include/iris/memory.h"
#include "../../include/iris/pipeline.h"
#include "../../include/iris/sampler.h"
#include "../../include/iris/types.h"
#include "../../include/iris/utils.h"
#include "../../include/iris/vkstdafx.h"
#include "../../include/std/include/error.h"
#include "../../include/std/include/stdafx.h"
#include "../../include/std/include/string.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static inline iris_buffer_t*
get_buffer_for_transfer(iris_driver_t* driver, iris_size_t size)
{
  nv_assert_else_return(driver != NULL, NULL);
  nv_assert_else_return(size != 0, NULL);

  if (size <= IRIS_SMALL_TRANSFER_BUFFER_SIZE)
  {
    return &driver->small_transfer_buffer;
  }
  else if (size <= driver->large_transfer_buffer.size)
  {
    return &driver->large_transfer_buffer;
  }

  return NULL;
}

static inline VkBufferUsageFlags
nv_to_vk_buffer_usage(iris_buffer_flags flags)
{
  VkBufferUsageFlags usage = 0;

  // volatile accessed externally, must always be up-to-date
  // if (flags & IRIS_BUFFER_FLAGS_VOLATILE_BIT)
  // {
  //   // or any usage that implies frequent access
  //   usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  // }

  if ((flags & IRIS_BUFFER_FLAGS_VERTEX_BUFFER_BIT) != 0u)
  {
    usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  }
  if ((flags & IRIS_BUFFER_FLAGS_INDEX_BUFFER_BIT) != 0u)
  {
    usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  }
  if ((flags & IRIS_BUFFER_FLAGS_SS_BUFFER_BIT) != 0u)
  {
    usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  }
  if ((flags & IRIS_BUFFER_FLAGS_UNIFORM_BUFFER_BIT) != 0u)
  {
    usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  }

  if (usage == 0)
  {
    usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  }

  /* The driver needs this to perform many operations */
  usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

  return usage;
}

static inline VkMemoryPropertyFlags
nv_to_vk_memory_properties(iris_memory_flags memory_flags, iris_buffer_flags buffer_flags)
{
  VkMemoryPropertyFlags props = 0;

  // readback requires CPU visible memory
  if ((memory_flags & IRIS_MEMORY_FLAGS_READBACK_OPTIMAL_BIT) != 0u)
  {
    props |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  }

  if ((memory_flags & IRIS_MEMORY_FLAGS_PERSISTENT_MAPPED_BIT) != 0u)
  {
    props |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
  }

  // transient/volatile buffers are often best in device-local memory
  /* Vertex, index and SS buffers need to be in device local memory. */
  if (((memory_flags & IRIS_MEMORY_FLAGS_TRANSIENT_BIT) != 0u) || ((memory_flags & IRIS_MEMORY_FLAGS_VOLATILE_BIT) != 0u)
      || ((buffer_flags & IRIS_BUFFER_FLAGS_VERTEX_BUFFER_BIT) != 0u) || ((buffer_flags & IRIS_BUFFER_FLAGS_SS_BUFFER_BIT) != 0u))
  {
    props |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  }

  if ((memory_flags & IRIS_MEMORY_FLAGS_MAPPABLE_BIT) != 0u)
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

static inline nv_error
create_buffer(iris_driver_t* driver, iris_size_t size, iris_buffer_flags flags, VkBuffer* dst)
{
  const VkBufferUsageFlags vk_buffer_flags = nv_to_vk_buffer_usage(flags);

  VkBufferCreateInfo buffer_info = nv_zinit(VkBufferCreateInfo);
  buffer_info.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size               = size;
  buffer_info.usage              = vk_buffer_flags;
  nvvk_result_check(*driver->vkctx, vkCreateBuffer(driver->vkctx->device, &buffer_info, &driver->vkctx->vkalloc, dst));
  nv_assert_else_return(*dst != VK_NULL_HANDLE, NV_ERROR_EXTERNAL);

  return NV_SUCCESS;
}

nv_error
iris_buffer_init(iris_driver_t* driver, iris_size_t size, size_t alignment, iris_buffer_extra_create_info_t* extra_info, iris_buffer_flags flags, iris_buffer_t* dst)
{
  nv_assert_else_return(iris_driver_is_valid(driver) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(size != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);

  nv_bzero(dst, sizeof(iris_buffer_t));

  nv_error code = NV_SUCCESS;

  alignment = NV_MAX(alignment, IRIS_BUFFER_MINIMUM_ALIGNMENT);
  alignment = NV_MAX(alignment, get_preferred_alignment(driver->vkctx));

  iris_memory_flags memory_flags = 0;
  if (extra_info != NULL && extra_info->custom_memory_flags != 0)
  {
    memory_flags = extra_info->custom_memory_flags;
  }

  const iris_size_t buffer_size = align_up_size(size, alignment);
  if ((code = create_buffer(driver, buffer_size, flags, &dst->handle)) != NV_SUCCESS)
  {
    return code;
  }

  VkMemoryRequirements memory_requirements;
  vkGetBufferMemoryRequirements(driver->vkctx->device, dst->handle, &memory_requirements);

  if (extra_info != NULL && extra_info->custom_memory_pool != NULL)
  {
    iris_memory_allocate(extra_info->custom_memory_pool, memory_requirements.size, memory_requirements.alignment, &dst->memory);
  }
  else
  {
    iris_memory_allocate_dedicated(driver, memory_requirements.memoryTypeBits, memory_flags, memory_requirements.size, memory_requirements.alignment, &dst->memory);
  }

  dst->size      = align_up_size(size, alignment);
  dst->alignment = alignment;
  dst->flags     = flags;
  dst->driver    = driver;

  nvvk_result_check(*driver->vkctx, vkBindBufferMemory(driver->vkctx->device, dst->handle, iris_memory_get_backing(&dst->memory), dst->memory.pool_offset));

  return NV_SUCCESS;
}

void
iris_buffer_destroy(iris_buffer_t* buffer)
{
  nv_assert_else_return(buffer != NULL, );
  nv_assert_else_return(iris_driver_is_valid(buffer->driver), );

  nvvk_ctx_t* ctx = buffer->driver->vkctx;

  iris_memory_free(&buffer->memory);

  vkDeviceWaitIdle(ctx->device);
  vkDestroyBuffer(ctx->device, buffer->handle, &ctx->vkalloc);
  // vkFreeMemory(ctx->device, buffer->driver->pool.memory, NULL);
}

size_t
iris_buffer_size(const iris_buffer_t* buffer)
{
  return buffer->size;
}

static inline nv_error
nv_transfer_to_cpu_visible_buffer(iris_buffer_t* buffer, const void* data, iris_size_t data_size, iris_size_t offset)
{
  nv_assert_else_return(buffer != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(iris_driver_is_valid(buffer->driver) == true, NV_ERROR_INVALID_ARG);

  void*    mapping = NULL;
  nv_error code    = NV_SUCCESS;

  size_t write_offset = offset;

  code = iris_memory_map(&buffer->memory, write_offset, data_size, &mapping);
  if (code != NV_SUCCESS)
  {
    return code;
  }

  nv_memcpy(mapping, data, data_size);

  if (!(buffer->memory.memory_flags & IRIS_MEMORY_FLAGS_PERSISTENT_MAPPED_BIT))
  {
    iris_memory_unmap(&buffer->memory);
  }

  return NV_SUCCESS;
}

static inline nv_error
nv_stage_transfer_to_buffer(iris_buffer_t* buffer, const void* data, iris_size_t data_size, iris_size_t offset)
{
  nv_assert_else_return(iris_driver_is_valid(buffer->driver) == true, NV_ERROR_INVALID_ARG);

  iris_driver_t* const driver = buffer->driver;
  nv_error             code   = NV_SUCCESS;

  if ((buffer->size + offset) < IRIS_SMALL_TRANSFER_BUFFER_SIZE)
  {
    void* mapping = NULL;
    code          = iris_memory_map(&driver->small_transfer_buffer.memory, 0, data_size, &mapping);
    if (code != NV_SUCCESS)
    {
      return code;
    }

    nv_memmove(mapping, data, data_size);

    code = iris_buffer_copy(buffer, &driver->small_transfer_buffer, data_size, offset, 0);
    if (code != NV_SUCCESS)
    {
      return code;
    }

    iris_memory_flush(&buffer->memory);
  }
  else if ((buffer->size + offset) < IRIS_LARGE_TRANSFER_BUFFER_INITIAL_SIZE)
  {
    void* mapping = NULL;
    code          = iris_memory_map(&driver->large_transfer_buffer.memory, 0, data_size, &mapping);
    if (code != NV_SUCCESS)
    {
      return code;
    }

    nv_memmove(mapping, data, data_size);

    code = iris_buffer_copy(buffer, &driver->large_transfer_buffer, data_size, offset, 0);
    if (code != NV_SUCCESS)
    {
      return code;
    }

    iris_memory_flush(&buffer->memory);
  }
  else
  {
    return NV_ERROR_INVALID_RETVAL;
  }

  return NV_SUCCESS;
}

nv_error
iris_buffer_write_data(iris_buffer_t* buffer, const void* data, iris_size_t data_size, iris_size_t offset)
{
  nv_assert_else_return(iris_driver_is_valid(buffer->driver) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(buffer != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(data != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(data_size != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(offset <= buffer->size, NV_ERROR_INVALID_ARG);

  // we stop writing to CPU visible memory for writes up to 64 KiB
  if (((buffer->memory.memory_flags & IRIS_MEMORY_FLAGS_MAPPABLE_BIT) != 0u) && data_size < (64 * 1000ULL))
  {
    return nv_transfer_to_cpu_visible_buffer(buffer, data, data_size, offset);
  }

  return nv_stage_transfer_to_buffer(buffer, data, data_size, offset);
}

nv_error
iris_memory_flush(iris_memory_t* memory)
{
  nv_assert_else_return(memory != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(iris_driver_is_valid(memory->driver) == true, NV_ERROR_INVALID_ARG);

  if (memory->drv_mapped_size <= 0 || memory->drv_mapped == NULL)
  {
    return NV_SUCCESS;
  }

  /* because we never really have host_coherent_bit in vk flags, we must always flush the memory */
  VkMappedMemoryRange const range = {
    .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
    .memory = iris_memory_get_backing(memory),
    .offset = memory->pool_offset + memory->drv_mapped_offset,
    .size   = memory->drv_mapped_size,
  };
  vkFlushMappedMemoryRanges(memory->driver->vkctx->device, 1, &range);

  return NV_SUCCESS;
}

static inline nv_error
readback_buffer_staged(iris_buffer_t* buffer, iris_size_t offset, iris_size_t size, void* dst)
{
  nv_assert_else_return(buffer != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(iris_driver_is_valid(buffer->driver) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(size != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return((size + offset) < buffer->size, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);

  iris_driver_t* driver = buffer->driver;

  iris_buffer_extra_create_info_t extra_info = nv_zinit(iris_buffer_extra_create_info_t);
  extra_info.custom_memory_flags             = IRIS_MEMORY_FLAGS_READBACK_OPTIMAL_BIT | IRIS_MEMORY_FLAGS_PERSISTENT_MAPPED_BIT;
  extra_info.custom_memory_pool              = &driver->cpu_mappable_pool;

  iris_buffer_t staging;
  iris_buffer_init(buffer->driver, size, 8, &extra_info, IRIS_BUFFER_FLAGS_READBACK_ONLY_BIT, &staging);

  VkCommandBuffer cmd = nv_vk_begin_command_buffer(driver);

  size_t read_offset  = offset;
  size_t write_offset = 0;

  VkBufferCopy const copy = { .srcOffset = read_offset, .dstOffset = write_offset, .size = size };
  vkCmdCopyBuffer(cmd, iris_buffer_get_backing(buffer), iris_buffer_get_backing(&staging), 1, &copy);

  nv_vk_end_command_buffer(driver, cmd, driver->vkctx->transfer_queue, true);

  void* mapping = NULL;
  iris_memory_map(&staging.memory, write_offset, size, &mapping);
  nv_memcpy(mapping, dst, size);
  iris_memory_unmap(&staging.memory);

  iris_buffer_destroy(&staging);

  return NV_SUCCESS;
}

nv_error
iris_buffer_readback(iris_buffer_t* buffer, iris_size_t size, iris_size_t offset, void* dst)
{
  nv_assert_else_return(buffer != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(iris_driver_is_valid(buffer->driver) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(size <= buffer->size, NV_ERROR_INVALID_ARG);
  nv_assert_else_return((size + offset) < buffer->size, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return((buffer->memory.memory_flags & IRIS_MEMORY_FLAGS_TRANSIENT_BIT) == false, NV_ERROR_INVALID_ARG);

  /* persistent mapping implies the whole buffer is mapped, just read it in */
  if ((buffer->memory.memory_flags & IRIS_MEMORY_FLAGS_PERSISTENT_MAPPED_BIT) != 0u)
  {
    void* mapping = NULL;

    size_t read_offset = offset;

    nv_error const code = iris_memory_map(&buffer->memory, read_offset, size, &mapping);
    nv_assert_else_return(code == NV_SUCCESS, code);

    nv_memcpy(dst, mapping, size);
    return NV_SUCCESS;
  }
  else if ((buffer->memory.drv_mapped != NULL) && buffer->memory.drv_mapped_size >= size && buffer->memory.drv_mapped_offset <= offset)
  {
    /* that weird shenanigans with the offset is to make it absolute. The offset should be absolute to the buffer. */
    const unsigned char* src = (unsigned char*)buffer->memory.drv_mapped + (offset - buffer->memory.drv_mapped_offset);
    nv_memcpy(dst, src, size);
    return NV_SUCCESS;
  }

  /* nothing else worked, slowly stage a transfer from the GPU to the CPU */
  return readback_buffer_staged(buffer, offset, size, dst);
}

nv_error
iris_buffer_copy(iris_buffer_t* dst, iris_buffer_t* src, iris_size_t num_bytes, iris_size_t dst_offset, iris_size_t src_offset)
{
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(src != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(src->driver == dst->driver, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(iris_driver_is_valid(src->driver), NV_ERROR_INVALID_ARG);
  nv_assert_else_return(iris_driver_is_valid(dst->driver), NV_ERROR_INVALID_ARG);
  nv_assert_else_return(num_bytes != 0, NV_ERROR_INVALID_ARG);

  size_t read_offset  = src_offset;
  size_t write_offset = dst_offset;

  nv_assert_else_return((num_bytes + write_offset) <= dst->size, NV_ERROR_INVALID_ARG);
  nv_assert_else_return((num_bytes + read_offset) <= src->size, NV_ERROR_INVALID_ARG);

  if (src->memory.pool == dst->memory.pool && src->memory.pool != NULL)
  {
    // prevent overlap inside same pool
    nv_assert_else_return((num_bytes + read_offset) <= write_offset || (num_bytes + write_offset) <= read_offset, NV_ERROR_INVALID_ARG);
  }

  // Flush writes from src so we can safely copy the data
  iris_memory_flush(&src->memory);

  iris_driver_t* driver = dst->driver;

  VkCommandBuffer cmd = VK_NULL_HANDLE;
  if (iris_is_upload_batch_active(driver))
  {
    cmd = driver->active_upload_cmd;
  }
  else
  {
    cmd = nv_vk_begin_command_buffer(driver);
  }

  VkBufferCopy copy = {
    .srcOffset = read_offset,
    .dstOffset = write_offset,
    .size      = num_bytes,
  };
  vkCmdCopyBuffer(cmd, src->handle, dst->handle, 1, &copy);

  // If we weren't in an upload batch, immediately end and submit the transfer
  if (!iris_is_upload_batch_active(driver))
  {
    nv_vk_end_command_buffer(dst->driver, cmd, dst->driver->vkctx->transfer_queue, true);
  }

  return NV_SUCCESS;
}

VkMemoryPropertyFlags
iris_nv_memory_flags_to_vk_flags(iris_memory_flags flags)
{
  if (flags == IRIS_MEMORY_FLAGS_DEFAULT_BIT)
  {
    return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  }

  iris_memory_flags ret = 0;

  ret |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

  if ((flags & IRIS_MEMORY_FLAGS_MAPPABLE_BIT) != 0u)
  {
    ret |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
  }

  if ((flags & IRIS_MEMORY_FLAGS_CPU_CACHED_BIT) != 0u)
  {
    ret |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
  }

  return ret;
}

bool
iris_memory_does_overlap(const iris_memory_t* mem_1, const iris_memory_t* mem_2)
{
  if (NV_UNLIKELY(mem_1 == mem_2))
  {
    return true;
  }

  if (((mem_1->memory_flags & IRIS_MEMORY_FLAGS_DEDICATED_BIT) != 0u) || ((mem_2->memory_flags & IRIS_MEMORY_FLAGS_DEDICATED_BIT) != 0u))
  {
    return false;
  }

  const bool regions_overlap = ((mem_1->size + mem_1->pool_offset) < mem_2->pool_offset) || ((mem_2->size + mem_2->pool_offset) < mem_1->pool_offset);
  return (mem_1->pool == mem_2->pool) && regions_overlap;
}

VkBuffer
iris_buffer_get_backing(const iris_buffer_t* buffer)
{
  nv_assert_else_return(buffer != NULL, VK_NULL_HANDLE);
  nv_assert_else_return(iris_driver_is_valid(buffer->driver) == true, VK_NULL_HANDLE);
  return buffer->handle;
}

nv_error
iris_buffer_resize(iris_buffer_t* buffer, size_t new_size, size_t new_alignment, bool copy_old_data)
{
  nv_assert_else_return(buffer != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(iris_driver_is_valid(buffer->driver), NV_ERROR_INVALID_ARG);
  nv_assert_else_return(new_size > 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return((new_alignment & (new_alignment - 1)) == 0, NV_ERROR_INVALID_ARG);

  nvvk_ctx_t* vkctx = buffer->driver->vkctx;

  VkBuffer new_buffer = VK_NULL_HANDLE;

  // Have enough space for atleast 'frames_in_flight' copies of the buffer
  size_t const new_aligned_size = align_up_size(new_size, new_alignment);

  // Create a vulkan buffer with that size
  create_buffer(buffer->driver, new_aligned_size, buffer->flags, &new_buffer);

  VkMemoryRequirements memory_requirements;
  vkGetBufferMemoryRequirements(vkctx->device, new_buffer, &memory_requirements);

  iris_memory_t       new_block = nv_zinit(iris_memory_t);
  iris_memory_pool_t* pool      = buffer->memory.pool;

  if (copy_old_data)
  {
    iris_memory_allocate_dedicated(
        buffer->driver, memory_requirements.memoryTypeBits, buffer->memory.memory_flags, memory_requirements.size, memory_requirements.alignment, &new_block);
    vkBindBufferMemory(vkctx->device, new_buffer, iris_memory_get_backing(&new_block), new_block.pool_offset);

    VkCommandBuffer cmd  = nv_vk_begin_command_buffer(buffer->driver);
    VkBufferCopy    copy = {
         .srcOffset = 0,
         .dstOffset = 0,
         .size      = NV_MIN(new_size, buffer->size),
    };
    vkCmdCopyBuffer(cmd, buffer->handle, new_buffer, 1, &copy);
    nv_vk_end_command_buffer(buffer->driver, cmd, vkctx->transfer_queue, true);

    vkDeviceWaitIdle(buffer->driver->vkctx->device);
    vkDestroyBuffer(vkctx->device, iris_buffer_get_backing(buffer), &vkctx->vkalloc);

    // free old memory AFTER copy
    iris_memory_free(&buffer->memory);
  }
  else
  {
    vkDeviceWaitIdle(buffer->driver->vkctx->device);

    // free old memory BEFORE copy
    iris_memory_flags old_flags = buffer->memory.memory_flags;
    vkDestroyBuffer(vkctx->device, iris_buffer_get_backing(buffer), &vkctx->vkalloc);
    iris_memory_free(&buffer->memory);

    if (pool != NULL)
    {
      iris_memory_allocate(pool, memory_requirements.size, memory_requirements.alignment, &new_block);
    }
    else
    {
      iris_memory_allocate_dedicated(buffer->driver, memory_requirements.memoryTypeBits, old_flags, memory_requirements.size, memory_requirements.alignment, &new_block);
    }
    vkBindBufferMemory(vkctx->device, new_buffer, iris_memory_get_backing(&new_block), new_block.pool_offset);
  }

  // update buffer struct
  buffer->handle = new_buffer;
  buffer->memory = new_block;
  buffer->size   = new_aligned_size;

  return NV_SUCCESS;
}