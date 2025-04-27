#include "GPU/driver.h"

// #include "GPU/allocator.h"
#include "GPU/allocator.h"
#include "GPU/buffer.h"
#include "GPU/newmemory.h"
#include "GPU/pipeline.h"
#include "GPU/types.h"
#include "GPU/vk.h"

#include "engine/camera.h"
#include "std/containers/list.h"
#include "std/errorcodes.h"
#include "std/stdafx.h"
#include "std/string.h"

#include "external/volk/volk.h"
#include <stdlib.h>

static inline bool
has_flag(u32 flags, u32 want)
{
  return (flags & want);
}

static inline vk_size_t
_max_elem(const vk_size_t* arr, vk_size_t count)
{
  /* some vulkan implementations have some values as 0, so we can't use 0 here */
  vk_size_t max = 1;
  for (vk_size_t i = 0; i < count; i++)
  {
    if (arr[i] > max)
    {
      max = arr[i];
    }
  }
  return max;
}

static inline vk_size_t
_get_preferred_alignment(const nvvk_ctx_t* vkctx)
{
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(vkctx->phys_device, &props);

  const vk_size_t alignment_list[] = {
    props.limits.nonCoherentAtomSize,             // for host-visible memory alignment (especially if not host coherent)
    props.limits.bufferImageGranularity,          // required if buffers and images are in same memory
    props.limits.minUniformBufferOffsetAlignment, // for dynamic UBOs
    props.limits.minStorageBufferOffsetAlignment, // for dynamic SSBOs

    // not necessarily needed, just optimal
    // props.limits.optimalBufferCopyOffsetAlignment,   // for copy src/dst offsets
    // props.limits.optimalBufferCopyRowPitchAlignment, // for row pitch in image copies
  };

  return _max_elem(alignment_list, nv_arrlen(alignment_list));
}

nv_error
nvvk_driver_init(nvvk_ctx_t* ctx, nvvk_driver_t* dst)
{
  nv_assert_else_return(nvvk_ctx_is_valid(ctx) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);

  nv_bzero(dst, sizeof(nvvk_driver_t));

  dst->canary = 0xDEADBEEF;

  dst->ctx = ctx;

  nv_error code = NV_SUCCESS;

  /* NOTE: POINTERS. We're storing POINTERS */
  code = nv_list_init(sizeof(nv_gpu_buffer_t*), 16, nv_allocator_c, NULL, &dst->buffers);
  nv_assert_else_return(code == NV_SUCCESS, code);

  code = nv_list_init(sizeof(nv_gpu_sampler_t), 16, nv_allocator_c, NULL, &dst->samplers);
  nv_assert_else_return(code == NV_SUCCESS, code);

  nv_gpu_memory_pool_create_info_t pool_ci = (nv_gpu_memory_pool_create_info_t){
    .size              = 1000000,
    .minimum_alignment = 1,
    .memory_flags      = NV_GPU_MEMORY_GPU_LOCAL_BIT | NV_GPU_MEMORY_MAPPABLE_BIT,
    .type              = NV_GPU_ALLOCATOR_FREELIST,
    .policy            = NV_GPU_ALLOCATOR_POLICY_BEST_FIT,
    .flags             = 0,
  };
  code = nv_gpu_memory_pool_init(dst, &pool_ci, &dst->pool);
  nv_assert_else_return(code == NV_SUCCESS, code);

  code = nv_gpu_buffer_init(
      dst, NOVA_GPU_SMALL_TRANSFER_BUFFER_SIZE, NOVA_GPU_SMALL_TRANSFER_BUFFER_ALIGNMENT, NOVA_GPU_SMALL_TRANSFER_BUFFER_CREATE_FLAGS, &dst->small_transfer_buffer);
  nv_assert_else_return(code == NV_SUCCESS, code);

  code = nv_gpu_buffer_init(
      dst, NOVA_GPU_LARGE_TRANSFER_BUFFER_INITIAL_SIZE, NOVA_GPU_LARGE_TRANSFER_BUFFER_ALIGNMENT, NOVA_GPU_LARGE_TRANSFER_BUFFER_CREATE_FLAGS, &dst->large_transfer_buffer);
  nv_assert_else_return(code == NV_SUCCESS, code);

  /**
   * This should never fail and indicates an error in the implementation of nvvk_driver_is_valid.
   */
  nv_assert_else_return(nvvk_driver_is_valid(dst) == true, NV_ERROR_INVALID_RETVAL);

  return NV_SUCCESS;
}

void
nvvk_driver_destroy(nvvk_driver_t* driver)
{
  if (!driver || nvvk_driver_is_valid(driver) == false)
  {
    return;
  }

  if (nv_list_size(&driver->buffers) != 0)
  {
    nv_log_error("Driver still held %zu buffers at time of destruction.\n", nv_list_size(&driver->buffers));
  }

  nv_gpu_memory_pool_destroy(&driver->pool);

  for (size_t i = 0; i < nv_list_size(&driver->samplers); i++)
  {
    nv_gpu_sampler_t* sampler = (nv_gpu_sampler_t*)nv_list_get(&driver->samplers, i);
    if (sampler && sampler->vksampler)
    {
      vkDestroySampler(driver->ctx->device, sampler->vksampler, &driver->ctx->allocator);
    }
  }

  for (size_t i = 0; i < NOVA_GPU_COMMAND_BUFFER_CACHE_COUNT; i++)
  {
    vkDestroyFence(driver->ctx->device, driver->ctx->cmd_buffer_fences[i], &driver->ctx->allocator);
  }

  nv_list_destroy(&driver->buffers);
  nv_list_destroy(&driver->samplers);

  nv_gpu_buffer_destroy(&driver->small_transfer_buffer);
  nv_gpu_buffer_destroy(&driver->large_transfer_buffer);

  nv_bzero(driver, sizeof(nvvk_driver_t));
}

bool
nvvk_driver_is_valid(const nvvk_driver_t* driver)
{
  if (!driver)
  {
    return false;
  }

  if (driver->canary != 0xDEADBEEF)
  {
    return false;
  }

  if (nv_list_is_valid(&driver->samplers) == false || nv_list_is_valid(&driver->buffers) == false)
  {
    return false;
  }

  if (driver->ctx == NULL || nvvk_ctx_is_valid(driver->ctx) == false)
  {
    return false;
  }

  /* nvvk_driver_is_valid is called by the functions to initialize these, we cannot check these */
  // if (driver->small_transfer_buffer.buffer == VK_NULL_HANDLE || driver->large_transfer_buffer.buffer == VK_NULL_HANDLE)
  // {
  //   return false;
  // }

  return true;
}

static void
_nv_gpu_allocator_freelist_insert_block(nv_gpu_allocator_freelist_t* list, nv_gpu_memory_block_t* block)
{
  nv_assert_else_return(block != NULL, );
  nv_assert_else_return(block->size != 0, );

  block->size = ALIGN_UP(block->size, block->alignment);

  if (!list->root || block->offset < list->root->offset)
  {
    block->pload.freelist.next = list->root;
    list->root                 = block;
    return;
  }

  nv_gpu_memory_block_t* cur = list->root;
  while (cur->pload.freelist.next && cur->pload.freelist.next->offset <= block->offset)
  {
    cur = cur->pload.freelist.next;
  }
  block->pload.freelist.next = cur->pload.freelist.next;
  cur->pload.freelist.next   = block;
}

static void
_nv_gpu_allocator_freelist_coalesce_blocks(nv_gpu_allocator_freelist_t* list)
{
  bool merged_any;
  do
  {
    merged_any                 = false;
    nv_gpu_memory_block_t* cur = list->root;
    while (cur && cur->pload.freelist.next)
    {
      nv_gpu_memory_block_t* next = cur->pload.freelist.next;
      // adjacent?
      if (cur->offset + cur->size == next->offset)
      {
        cur->size                = cur->size + next->size;
        cur->pload.freelist.next = next->pload.freelist.next;
        nv_free(next);
        merged_any = true;
        break; // restart from list->root
      }
      cur = next;
    }
  } while (merged_any);
}

bool
nv_gpu_allocator_freelist_allocate(nv_gpu_allocator_freelist_t* list, vk_size_t alignment, vk_size_t request_size, vk_size_t* out_offset)
{
  nv_gpu_memory_block_t* prev = NULL;
  nv_gpu_memory_block_t* cur  = list->root;

  while (cur)
  {
    vk_size_t aligned_off = ALIGN_UP(cur->offset, alignment);
    vk_size_t padding     = aligned_off - cur->offset;
    vk_size_t total_need  = padding + request_size;

    if (cur->size >= total_need)
    {
      *out_offset = aligned_off;

      vk_size_t tail_off  = aligned_off + request_size;
      vk_size_t tail_size = cur->size - total_need;

      if (padding > 0)
      {
        // keep the head fragment
        cur->size = padding;

        if (tail_size > 0)
        {
          nv_gpu_memory_block_t* tail = nv_malloc(sizeof(nv_gpu_memory_block_t));
          tail->offset                = tail_off;
          tail->size                  = tail_size;
          tail->alignment             = alignment;
          tail->pload.freelist.next   = cur->pload.freelist.next;
          cur->pload.freelist.next    = tail;
        }
      }
      else
      {
        // no head fragment: reuse or remove cur
        if (tail_size > 0)
        {
          cur->offset = tail_off;
          cur->size   = tail_size;
        }
        else
        {
          // exact fit — pull cur out
          if (prev)
          {
            prev->pload.freelist.next = cur->pload.freelist.next;
          }
          else
          {
            list->root = cur->pload.freelist.next;
          }
          nv_free(cur);
        }
      }
      return true;
    }

    prev = cur;
    cur  = cur->pload.freelist.next;
  }
  return false;
}

void
nv_gpu_allocator_freelist_free(nv_gpu_allocator_freelist_t* list, vk_size_t offset, vk_size_t size, vk_size_t alignment)
{
  nv_gpu_memory_block_t* block = nv_malloc(sizeof(nv_gpu_memory_block_t));
  nv_assert_else_return(block != NULL, );

  block->offset              = offset;
  block->size                = size;
  block->pload.freelist.next = NULL;
  block->alignment           = alignment;

  _nv_gpu_allocator_freelist_insert_block(list, block);
  _nv_gpu_allocator_freelist_coalesce_blocks(list);
}

static inline nv_gpu_buffer_t*
_get_buffer_for_transfer(nvvk_driver_t* driver, vk_size_t size)
{
  nv_assert_else_return(driver != NULL, NULL);
  nv_assert_else_return(size != 0, NULL);

  if (size <= NOVA_GPU_SMALL_TRANSFER_BUFFER_SIZE && !driver->small_transfer_buffer.drv_in_use)
  {
    return &driver->small_transfer_buffer;
  }
  else if (size <= driver->large_transfer_buffer.size && !driver->large_transfer_buffer.drv_in_use)
  {
    return &driver->large_transfer_buffer;
  }

  return NULL;
}

static inline VkBufferUsageFlags
_nv_to_vk_buffer_usage(nv_gpu_buffer_flags flags)
{
  VkBufferUsageFlags usage = 0;

  // volatile accessed externally, must always be up-to-date
  // if (flags & NV_GPU_BUFFER_VOLATILE_BIT)
  // {
  //   // or any usage that implies frequent access
  //   usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  // }

  if (flags & NV_GPU_BUFFER_TRANSIENT_BIT)
  {
    usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  }

  // needs to support GPU -> CPU transfers
  if (flags & NV_GPU_BUFFER_READBACK_OPTIMAL_BIT)
  {
    usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  }

  if (flags & NV_GPU_BUFFER_VERTEX_BUFFER_BIT)
  {
    usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  }
  if (flags & NV_GPU_BUFFER_INDEX_BUFFER_BIT)
  {
    usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  }
  if (flags & NV_GPU_BUFFER_SS_BUFFER_BIT)
  {
    usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  }
  if (flags & NV_GPU_BUFFER_UNIFORM_BUFFER_BIT)
  {
    usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  }

  if (usage == 0)
  {
    usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  }

  /* The driver needs this to perform many operations */
  usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

  return usage;
}

static inline VkMemoryPropertyFlags
_nv_to_vk_memory_properties(nv_gpu_buffer_flags flags)
{
  VkMemoryPropertyFlags props = 0;

  // readback requires CPU visible memory
  if (flags & NV_GPU_BUFFER_READBACK_OPTIMAL_BIT)
  {
    props |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  }

  if (flags & NV_GPU_BUFFER_PERSISTENT_MAPPED)
  {
    props |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
  }

  // transient/volatile buffers are often best in device-local memory
  /* Vertex, index and SS buffers need to be in device local memory. */
  if ((flags & NV_GPU_BUFFER_TRANSIENT_BIT) || (flags & NV_GPU_BUFFER_VOLATILE_BIT) || (flags & NV_GPU_BUFFER_VERTEX_BUFFER_BIT) || (flags & NV_GPU_BUFFER_INDEX_BUFFER_BIT)
      || (flags & NV_GPU_BUFFER_SS_BUFFER_BIT))
  {
    props |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  }

  if (flags & NV_GPU_BUFFER_MAPPABLE)
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

nv_error
nv_gpu_buffer_init(nvvk_driver_t* driver, vk_size_t size, size_t alignment, nv_gpu_buffer_flags flags, nv_gpu_buffer_t* dst)
{
  nv_assert_else_return(nvvk_driver_is_valid(driver) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(size != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);

  alignment = NV_MAX(alignment, NOVA_GPU_BUFFER_MINIMUM_ALIGNMENT);
  alignment = NV_MAX(alignment, _get_preferred_alignment(driver->ctx));

  nv_bzero(dst, sizeof(nv_gpu_buffer_t));

  const vk_size_t aligned_size = ALIGN_UP(size, alignment);

  VkMemoryAllocateFlags vk_property_flags = _nv_to_vk_memory_properties(flags);
  VkBufferUsageFlags    vk_buffer_flags   = _nv_to_vk_buffer_usage(flags);

  VkBufferCreateInfo buffer_info = nv_zero_init(VkBufferCreateInfo);
  buffer_info.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size               = size;
  buffer_info.usage              = vk_buffer_flags;
  nvvk_result_check(*driver->ctx, vkCreateBuffer(driver->ctx->device, &buffer_info, &driver->ctx->allocator, &dst->buffer));
  nv_assert_else_return(dst->buffer != VK_NULL_HANDLE, NV_ERROR_EXTERNAL);

  VkMemoryRequirements memory_requirements;
  vkGetBufferMemoryRequirements(driver->ctx->device, dst->buffer, &memory_requirements);
  nv_assert_else_return(memory_requirements.size != 0, NV_ERROR_EXTERNAL);
  nv_assert_else_return(memory_requirements.alignment != 0, NV_ERROR_EXTERNAL);

  // VkMemoryAllocateInfo allocInfo = nv_zero_init(VkMemoryAllocateInfo);
  // allocInfo.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  // allocInfo.allocationSize       = memory_requirements.size;
  // allocInfo.memoryTypeIndex      = nv_vk_get_mem_type(driver->ctx, memory_requirements.memoryTypeBits, vk_property_flags);
  // nvvk_result_check(*driver->ctx, vkAllocateMemory(driver->ctx->device, &allocInfo, &nvvkctx->allocator, &dst->memory));
  // nv_assert_else_return(dst->memory != VK_NULL_HANDLE, NV_ERROR_EXTERNAL);
  nv_error code = nv_gpu_memory_pool_allocate(&driver->pool, memory_requirements.size, memory_requirements.alignment, &dst->block);
  if (code != NV_SUCCESS)
  {
    return code;
  }

  dst->size      = dst->block.size;
  dst->alignment = dst->block.alignment;
  dst->flags     = flags;
  dst->driver    = driver;

  nvvk_result_check(*driver->ctx, vkBindBufferMemory(driver->ctx->device, dst->buffer, driver->pool.memory, dst->block.offset));

  if (flags & NV_GPU_BUFFER_PERSISTENT_MAPPED)
  {
    dst->drv_mapped = (uchar*)dst->block.pool->drv_mapped + dst->block.offset;
    nv_assert_else_return(dst->drv_mapped != NULL, NV_ERROR_EXTERNAL);

    dst->drv_mapped_size   = size;
    dst->drv_mapped_offset = 0;
  }

  return NV_SUCCESS;
}

void
nv_gpu_buffer_destroy(nv_gpu_buffer_t* buffer)
{
  if (!buffer)
  {
    return;
  }

  nvvk_ctx_t* ctx = buffer->driver->ctx;

  vkDeviceWaitIdle(ctx->device);

  vkDestroyBuffer(ctx->device, buffer->buffer, &ctx->allocator);
  nv_gpu_memory_pool_free(buffer->block.pool, &buffer->block);
  // vkFreeMemory(ctx->device, buffer->driver->pool.memory, NULL);
}

static inline nv_error
_nv_transfer_to_cpu_visible_buffer(nv_gpu_buffer_t* buffer, const void* data, vk_size_t data_size, vk_size_t offset)
{
  nv_assert_else_return(nvvk_driver_is_valid(buffer->driver) == true, NV_ERROR_INVALID_ARG);

  void*    mapping = NULL;
  nv_error code    = NV_SUCCESS;

  code = nv_gpu_buffer_map_memory(buffer, data_size, offset, &mapping);
  if (!code)
  {
    return code;
  }

  nv_memcpy(mapping, data, data_size);

  if (!(buffer->flags & NV_GPU_BUFFER_PERSISTENT_MAPPED))
  {
    code = nv_gpu_buffer_unmap_memory(buffer);
    if (!code)
    {
      return code;
    }
  }

  return NV_SUCCESS;
}

static inline nv_error
_nv_stage_transfer_to_buffer(nv_gpu_buffer_t* buffer, const void* data, vk_size_t data_size, vk_size_t offset)
{
  nv_assert_else_return(nvvk_driver_is_valid(buffer->driver) == true, NV_ERROR_INVALID_ARG);

  nvvk_driver_t* const driver = buffer->driver;
  nv_error             code   = NV_SUCCESS;

  if ((buffer->size + offset) < NOVA_GPU_SMALL_TRANSFER_BUFFER_SIZE)
  {
    code = nv_gpu_buffer_write_data(&driver->small_transfer_buffer, data, data_size, 0);
    if (!code)
    {
      return code;
    }

    code = nv_gpu_buffer_copy(&driver->small_transfer_buffer, buffer, data_size, offset, 0);
    if (!code)
    {
      return code;
    }
  }
  else if ((buffer->size + offset) < NOVA_GPU_LARGE_TRANSFER_BUFFER_INITIAL_SIZE)
  {
    code = nv_gpu_buffer_write_data(&driver->large_transfer_buffer, data, data_size, 0);
    if (!code)
    {
      return code;
    }

    code = nv_gpu_buffer_copy(&driver->large_transfer_buffer, buffer, data_size, offset, 0);
    if (!code)
    {
      return code;
    }
  }

  return NV_SUCCESS;
}

nv_error
nv_gpu_buffer_write_data(nv_gpu_buffer_t* buffer, const void* data, vk_size_t data_size, vk_size_t offset)
{
  nv_assert_else_return(nvvk_driver_is_valid(buffer->driver) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(buffer != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(data != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(data_size != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(offset <= buffer->size, NV_ERROR_INVALID_ARG);

  // if (buffer->flags & NV_GPU_BUFFER_CPU_VISIBLE)
  // {
  //   return _nv_transfer_to_cpu_visible_buffer(buffer, data, data_size, offset);
  // }

  return _nv_stage_transfer_to_buffer(buffer, data, data_size, offset);
}

nv_error
nv_gpu_buffer_map_memory(nv_gpu_buffer_t* buffer, vk_size_t size, vk_size_t offset, void** mapping)
{
  nv_assert_else_return(buffer != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_driver_is_valid(buffer->driver) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(size != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(offset < buffer->size, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(has_flag(buffer->flags, NV_GPU_BUFFER_MAPPABLE) == true, NV_ERROR_INVALID_ARG);

  VkDevice device = buffer->driver->ctx->device;

  /**
   * The buffer is transient, we won't be reading from it
   * We just create a backing cache and let the user write to that and flush that
   * to the GPU memory when needed.
   */
  if (buffer->flags & NV_GPU_BUFFER_TRANSIENT_BIT)
  {
    /**
     * The second or will never be reached because we expect that the write cache isn't initialized
     */
    if (!buffer->drv_write_cache || buffer->drv_write_cache == 0 /* || (buffer->drv_write_cache_size + buffer->drv_write_cache_offset) >= buffer->size */)
    {
      buffer->drv_write_cache        = nv_calloc(size);
      buffer->drv_write_cache_size   = size;
      buffer->drv_write_cache_offset = offset;

      nv_assert_else_return(buffer->drv_write_cache != NULL, NV_ERROR_MALLOC_FAILED);

      *mapping = buffer->drv_write_cache;

      return NV_SUCCESS;
    }
    else
    {
      /* trying to map the buffer twice */
      return NV_ERROR_INVALID_OPERATION;
    }
  }
  /* If we're already mapped and the current mapping isn't out of bounds of the request */
  else if ((buffer->flags & NV_GPU_BUFFER_PERSISTENT_MAPPED) && buffer->drv_mapped != NULL)
  {
    if (buffer->drv_mapped_size >= size && buffer->drv_mapped_offset <= offset)
    {
      *mapping = (unsigned char*)buffer->drv_mapped + offset;
      return NV_SUCCESS;
    }
    else
    {
      /* The current size is out of bounds, unmap it and map it again */
      // vkUnmapMemory(device, buffer->driver->pool.memory);

      buffer->drv_mapped        = NULL;
      buffer->drv_mapped_size   = size;
      buffer->drv_mapped_offset = offset;
    }
  }

  /* being CPU visible is an assertion */
  // VkResult result = vkMapMemory(device, buffer->driver->pool.memory, buffer->block.offset + offset, size, 0, &buffer->drv_mapped);
  // nv_assert_else_return(result == VK_SUCCESS, NV_ERROR_EXTERNAL);
  // nv_assert_else_return(buffer->drv_mapped != NULL, NV_ERROR_EXTERNAL);
  buffer->drv_mapped = (uchar*)buffer->block.pool->drv_mapped + buffer->block.offset + offset;

  buffer->drv_mapped_size   = size;
  buffer->drv_mapped_offset = offset;

  *mapping = buffer->drv_mapped;

  return NV_SUCCESS;
}

nv_error
nv_gpu_buffer_flush_mapped_memory(nv_gpu_buffer_t* buffer)
{
  nv_assert_else_return(buffer != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_driver_is_valid(buffer->driver) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(buffer->drv_mapped_size != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(buffer->drv_mapped_offset < buffer->size, NV_ERROR_INVALID_ARG);

  if (buffer->flags & NV_GPU_BUFFER_TRANSIENT_BIT && buffer->drv_write_cache != NULL && buffer->drv_write_cache_size > 0)
  {
    nv_error code = _nv_stage_transfer_to_buffer(buffer, buffer->drv_write_cache, buffer->drv_write_cache_size, buffer->drv_write_cache_offset);
    return code;
  }

  vk_size_t alignment = buffer->block.alignment;

  /* because we never really have host_coherent_bit in vk flags, we must always flush the memory */
  VkMappedMemoryRange range = {
    .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
    .memory = buffer->driver->pool.memory,
    .offset = buffer->drv_mapped_offset + buffer->block.offset,
    .size   = ALIGN_UP(buffer->drv_mapped_size, alignment),
  };
  vkFlushMappedMemoryRanges(buffer->driver->ctx->device, 1, &range);

  return NV_SUCCESS;
}

nv_error
nv_gpu_buffer_unmap_memory(nv_gpu_buffer_t* buffer)
{
  nv_assert_else_return(buffer != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_driver_is_valid(buffer->driver) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(buffer->drv_mapped_size != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(buffer->drv_mapped_offset < buffer->size, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(has_flag(buffer->flags, NV_GPU_BUFFER_PERSISTENT_MAPPED) == false, NV_ERROR_INVALID_ARG);

  VkDevice device = buffer->driver->ctx->device;

  if (buffer->flags & NV_GPU_BUFFER_TRANSIENT_BIT)
  {
    nv_error code = nv_gpu_buffer_flush_mapped_memory(buffer);

    buffer->drv_write_cache        = NULL;
    buffer->drv_write_cache_size   = 0;
    buffer->drv_write_cache_offset = 0;

    return code;
  }

  nv_error code = nv_gpu_buffer_flush_mapped_memory(buffer);
  if (code != NV_SUCCESS)
  {
    return code;
  }

  vkUnmapMemory(device, buffer->driver->pool.memory);

  buffer->drv_mapped        = NULL;
  buffer->drv_mapped_size   = 0;
  buffer->drv_mapped_offset = 0;

  return NV_SUCCESS;
}

static inline nv_error
_readback_buffer_staged(nv_gpu_buffer_t* buffer, vk_size_t size, vk_size_t offset, void* dst)
{
  nv_assert_else_return(buffer != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_driver_is_valid(buffer->driver) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(size != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return((size + offset) < buffer->size, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);

  nvvk_driver_t* driver = buffer->driver;

  nv_gpu_buffer_t staging;
  nv_gpu_buffer_init(buffer->driver, size, 8, NV_GPU_BUFFER_READBACK_OPTIMAL_BIT | NV_GPU_BUFFER_MAPPABLE, &staging);

  VkCommandBuffer cmd = nv_vk_begin_command_buffer(driver);

  VkBufferCopy copy = { .srcOffset = offset, .dstOffset = 0, .size = size };
  vkCmdCopyBuffer(cmd, nv_gpu_buffer_get_backing(buffer), nv_gpu_buffer_get_backing(&staging), 1, &copy);

  nv_vk_end_command_buffer(driver, cmd, driver->ctx->transfer_queue, 1);

  void* mapping = NULL;
  nv_gpu_buffer_map_memory(&staging, size, 0, &mapping);
  nv_memcpy(mapping, dst, size);
  nv_gpu_buffer_unmap_memory(&staging);

  nv_gpu_buffer_destroy(&staging);

  return NV_SUCCESS;
}

nv_error
nv_gpu_buffer_readback(nv_gpu_buffer_t* buffer, vk_size_t size, vk_size_t offset, void* dst)
{
  nv_assert_else_return(buffer != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_driver_is_valid(buffer->driver) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(size <= buffer->size, NV_ERROR_INVALID_ARG);
  nv_assert_else_return((size + offset) < buffer->size, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(has_flag(buffer->flags, NV_GPU_BUFFER_TRANSIENT_BIT) == false, NV_ERROR_INVALID_ARG);

  /* persistent mapping implies the whole buffer is mapped, just read it in */
  if (buffer->flags & NV_GPU_BUFFER_PERSISTENT_MAPPED)
  {
    void* mapping = NULL;

    nv_error code = nv_gpu_buffer_map_memory(buffer, size, offset, &mapping);
    nv_assert_else_return(code == NV_SUCCESS, code);

    nv_memcpy(dst, mapping, size);
    return NV_SUCCESS;
  }
  else if (buffer->drv_mapped && buffer->drv_mapped_size >= size && buffer->drv_mapped_offset <= offset)
  {
    /* that weird shenanigans with the offset is to make it absolute. The offset should be absolute to the buffer. */
    const unsigned char* src = (unsigned char*)buffer->drv_mapped + (offset - buffer->drv_mapped_offset);
    nv_memcpy(dst, src, size);
    return NV_SUCCESS;
  }

  /* nothing else worked, slowly stage a transfer from the GPU to the CPU */
  return _readback_buffer_staged(buffer, size, offset, dst);
}

nv_error
nv_gpu_buffer_copy(nv_gpu_buffer_t* dst, nv_gpu_buffer_t* src, vk_size_t num_bytes, vk_size_t dst_offset, vk_size_t src_offset)
{
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(src != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(src->driver == dst->driver, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_driver_is_valid(src->driver) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_driver_is_valid(dst->driver) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(num_bytes != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return((num_bytes + dst_offset) < dst->size != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return((num_bytes + src_offset) < src->size != 0, NV_ERROR_INVALID_ARG);

  VkBufferCopy copy = (VkBufferCopy){
    .srcOffset = src_offset,
    .dstOffset = dst_offset,
    .size      = num_bytes,
  };
  VkCommandBuffer cmd = nv_vk_begin_command_buffer(dst->driver);

  vkCmdCopyBuffer(cmd, src->buffer, dst->buffer, 1, &copy);

  nv_vk_end_command_buffer(dst->driver, cmd, dst->driver->ctx->transfer_queue, true);

  return NV_SUCCESS;
}

void
_nv_gpu_buffer_flush_writes_if_any(nv_gpu_buffer_t* buffer)
{
  (void)buffer;
}

nv_gpu_memory_new_flags
nv_gpu_vk_memory_flags_to_nv_flags(VkMemoryPropertyFlags flags)
{
  nv_gpu_memory_new_flags ret = 0;

  if (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
  {
    ret |= NV_GPU_MEMORY_GPU_LOCAL_BIT;
  }

  if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
  {
    ret |= NV_GPU_MEMORY_MAPPABLE_BIT;
  }

  if (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
  {
    ret |= NV_GPU_MEMORY_CPU_CACHED_BIT;
  }

  return ret;
}

VkMemoryPropertyFlags
nv_gpu_nv_memory_flags_to_vk_flags(nv_gpu_memory_new_flags flags)
{
  nv_gpu_memory_new_flags ret = 0;

  if (flags & NV_GPU_MEMORY_GPU_LOCAL_BIT)
  {
    ret |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  }

  if (flags & NV_GPU_MEMORY_MAPPABLE_BIT)
  {
    ret |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
  }

  if (flags & NV_GPU_MEMORY_CPU_CACHED_BIT)
  {
    ret |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
  }

  return ret;
}

VkBuffer
nv_gpu_buffer_get_backing(const nv_gpu_buffer_t* buffer)
{
  nv_assert_else_return(buffer != NULL, VK_NULL_HANDLE);
  nv_assert_else_return(nvvk_driver_is_valid(buffer->driver) == true, VK_NULL_HANDLE);
  return buffer->buffer;
}

nv_error
nv_gpu_memory_pool_init(nvvk_driver_t* driver, const nv_gpu_memory_pool_create_info_t* info, nv_gpu_memory_pool_t* dst)
{
  nv_assert_else_return(driver != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_driver_is_valid(driver), NV_ERROR_INVALID_ARG);
  nv_assert_else_return(info != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(info->memory_flags != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(info->minimum_alignment != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(info->size != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);

  nv_bzero(dst, sizeof(nv_gpu_memory_pool_t));

  dst->canary = 0xDEADBEEF;

  const vk_size_t preffered_alignment = _get_preferred_alignment(driver->ctx);
  const vk_size_t alignment           = NV_MAX(preffered_alignment, info->minimum_alignment);

  const vk_size_t aligned_size = ALIGN_UP(info->size, alignment);

  VkMemoryPropertyFlags vk_property_flags = nv_gpu_nv_memory_flags_to_vk_flags(info->memory_flags);

  VkMemoryAllocateInfo allocInfo = nv_zero_init(VkMemoryAllocateInfo);
  allocInfo.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize       = aligned_size;

  /**
   * We pass in uint32_max here to tell vulkan that
   * we aren't allocating for a resource and that all
   * memory types are valid.
   */
  allocInfo.memoryTypeIndex = nv_vk_get_mem_type(driver->ctx, UINT32_MAX, vk_property_flags);

  nvvk_result_check(*driver->ctx, vkAllocateMemory(driver->ctx->device, &allocInfo, &driver->ctx->allocator, &dst->memory));
  nv_assert_else_return(dst->memory != VK_NULL_HANDLE, NV_ERROR_MALLOC_FAILED);

  dst->allocated_size = aligned_size;
  dst->alignment      = alignment;
  dst->driver         = driver;

  dst->memory_flags = info->memory_flags;
  dst->flags        = info->flags;

  dst->type   = info->type;
  dst->policy = info->policy;

  switch (info->type)
  {
    case NV_GPU_ALLOCATOR_STACK: nv_gpu_memory_allocator_stack_init(dst->allocated_size, &dst->backing_allocator.stack); break;
    case NV_GPU_ALLOCATOR_FREELIST: nv_gpu_memory_allocator_freelist_init(dst->allocated_size, &dst->backing_allocator.freelist); break;
    default: break;
  }

  /**
   * TODO: We don't want to always map the *entire* memory, do we?
   */
  if (info->memory_flags & NV_GPU_MEMORY_MAPPABLE_BIT)
  {
    vkMapMemory(driver->ctx->device, dst->memory, 0, aligned_size, 0, &dst->drv_mapped);
  }

  return NV_SUCCESS;
}

void
nv_gpu_memory_pool_destroy(nv_gpu_memory_pool_t* pool)
{
  if (!pool)
  {
    return;
  }

  nv_assert_else_return(nvvk_driver_is_valid(pool->driver), );
  nv_assert_else_return(pool->canary == 0xDEADBEEF, );

  VkDevice device = pool->driver->ctx->device;

  if (pool->memory != VK_NULL_HANDLE)
  {
    vkDeviceWaitIdle(device);
    vkFreeMemory(device, pool->memory, &pool->driver->ctx->allocator);
  }
}

nv_error
nv_gpu_memory_pool_allocate(nv_gpu_memory_pool_t* pool, vk_size_t size, vk_size_t alignment, nv_gpu_memory_block_t* dst_block)
{
  nv_assert_else_return(pool != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(pool->canary == 0xDEADBEEF, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(dst_block != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(size != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(alignment != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(size <= pool->allocated_size, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(ALIGN_UP(size, alignment) <= pool->allocated_size, NV_ERROR_INVALID_ARG);

  const vk_size_t preffered_alignment = NV_MAX(pool->alignment, alignment);
  const vk_size_t aligned_size        = ALIGN_UP(size, preffered_alignment);

  // pool->user_data += aligned_size;
  // nv_log_info("VKMEMUSG::(%llu|+%llu)\n", pool->user_data, aligned_size);

  if (pool->type == NV_GPU_ALLOCATOR_STACK)
  {
    nv_gpu_allocator_stack_t* stack = &pool->backing_allocator.stack;

    *dst_block = (nv_gpu_memory_block_t){
      .pool      = pool,
      .size      = aligned_size,
      .offset    = stack->bumper,
      .alignment = preffered_alignment,
      .pload     = (nv_gpu_allocator_payload_t){},
    };

    stack->last_allocation_bumper = stack->bumper;

    stack->bumper += aligned_size;
    stack->bumper = ALIGN_UP(stack->bumper, preffered_alignment);

    nv_assert_else_return(stack->bumper <= pool->allocated_size, NV_ERROR_MALLOC_FAILED);
  }
  else if (pool->type == NV_GPU_ALLOCATOR_FREELIST)
  {
    nv_gpu_allocator_freelist_t* freelist = &pool->backing_allocator.freelist;

    vk_size_t offset = SIZE_MAX;
    nv_gpu_allocator_freelist_allocate(freelist, aligned_size, preffered_alignment, &offset);

    nv_assert_else_return(offset != SIZE_MAX, NV_ERROR_INVALID_RETVAL);
    nv_assert_else_return((offset % preffered_alignment) == 0, NV_ERROR_INVALID_RETVAL);

    *dst_block = (nv_gpu_memory_block_t){
      .pool      = pool,
      .size      = aligned_size,
      .offset    = offset,
      .alignment = preffered_alignment,
      .pload     = (nv_gpu_allocator_payload_t){},
    };
  }
  else
  {
    nv_log_error("Unimplemented type for allocator.\n");
    return NV_ERROR_INVALID_INPUT;
  }

  /* assert that block is aligned */
  nv_assert_else_return((dst_block->size % _get_preferred_alignment(pool->driver->ctx)) == 0, NV_ERROR_UNKNOWN);

  return NV_SUCCESS;
}

nv_error
nv_gpu_memory_pool_free(nv_gpu_memory_pool_t* pool, nv_gpu_memory_block_t* block)
{
  nv_assert_else_return(pool != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(pool->canary == 0xDEADBEEF, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(pool->memory != VK_NULL_HANDLE, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(pool->allocated_size > 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(block != NULL, NV_ERROR_INVALID_ARG);

  // pool->user_data -= block->size;
  // nv_log_info("VKMEMUSG::(%llu|-%llu)\n", pool->user_data, block->size);

  if (pool->type == NV_GPU_ALLOCATOR_STACK)
  {
    nv_gpu_allocator_stack_t* stack = &pool->backing_allocator.stack;

    if (block->offset == stack->last_allocation_bumper)
    {
      stack->bumper -= block->size;
      stack->bumper = ALIGN_UP(stack->bumper, pool->alignment);
    }
  }
  else if (pool->type == NV_GPU_ALLOCATOR_FREELIST)
  {
    nv_gpu_allocator_freelist_t* freelist = &pool->backing_allocator.freelist;

    nv_gpu_allocator_freelist_free(freelist, block->offset, block->size, block->alignment);
  }
  else
  {
    nv_log_error("Unimplemented type for allocator.\n");
    return NV_ERROR_INVALID_INPUT;
  }

  return NV_SUCCESS;
}

nv_error
nv_gpu_memory_allocator_freelist_init(vk_size_t aligned_size, nv_gpu_allocator_freelist_t* list)
{
  nv_assert_else_return(list != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(aligned_size != 0, NV_ERROR_INVALID_ARG);

  nv_bzero(list, sizeof(nv_gpu_allocator_freelist_t));

  list->root = nv_calloc(sizeof(nv_gpu_memory_block_t));
  if (!list->root)
  {
    return NV_ERROR_MALLOC_FAILED;
  }

  list->root->offset              = 0;
  list->root->size                = aligned_size;
  list->root->pload.freelist.next = NULL;

  return NV_SUCCESS;
}

void
nv_gpu_memory_allocator_freelist_destroy(nv_gpu_allocator_freelist_t* list)
{
  nv_assert_else_return(list != NULL, );

  nv_gpu_memory_block_t* cur = list->root;
  while (cur)
  {
    nv_gpu_memory_block_t* next = cur->pload.freelist.next;
    nv_free(cur);
    cur = next;
  }
  list->root = NULL;

  nv_bzero(list, sizeof(nv_gpu_allocator_freelist_t));
}

nv_error
nv_gpu_memory_allocator_stack_init(vk_size_t aligned_size, nv_gpu_allocator_stack_t* stack)
{
  nv_assert_else_return(stack != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(aligned_size != 0, NV_ERROR_INVALID_ARG);

  nv_bzero(stack, sizeof(nv_gpu_allocator_stack_t));

  stack->bumper                 = 0;
  stack->last_allocation_bumper = 0;

  return NV_SUCCESS;
}

void
nv_gpu_memory_allocator_stack_destroy(nv_gpu_allocator_stack_t* stack)
{
  nv_assert_else_return(stack != NULL, );

  nv_bzero(stack, sizeof(nv_gpu_allocator_stack_t));
}

void*
nvvk_alloc(void* pUserData, size_t size, size_t alignment, VkSystemAllocationScope allocationScope)
{
  (void)pUserData;
  (void)allocationScope;

  return nv_aligned_alloc(size, alignment);
}

void*
nvvk_realloc(void* pUserData, void* pOriginal, size_t new_size, size_t alignment, VkSystemAllocationScope allocationScope)
{
  (void)pUserData;
  (void)allocationScope;

  return nv_aligned_realloc(pOriginal, new_size, alignment);
}

void
nvvk_free(void* pUserData, void* pMemory)
{
  (void)pUserData;
  nv_aligned_free(pMemory);
}

void
nvvk_internal_allocation(void* pUserData, size_t size, VkInternalAllocationType allocationType, VkSystemAllocationScope allocationScope)
{
}

void
nvvk_internal_free(void* pUserData, size_t size, VkInternalAllocationType allocationType, VkSystemAllocationScope allocationScope)
{
}
