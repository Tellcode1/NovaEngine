#include <stdlib.h>

#include "GPU/buffer.hpp"
#include "GPU/driver.hpp"
#include "GPU/gpumemory.hpp"
#include "GPU/pipeline.hpp"
#include "GPU/prep.hpp"
#include "GPU/types.hpp"
#include "GPU/vk.hpp"
#include "GPU/vkstdafx.hpp"

#include "std/bit.h"
#include "std/containers/list.h"
#include "std/errorcodes.h"
#include "std/stdafx.h"
#include "std/string.h"

#include "external/volk/volk.h"

#ifndef NV_GPU_DISABLE_OPTIMIZATIONS
#  define NV_GPU_DISABLE_OPTIMIZATIONS (true)
#endif

#define DOES_ALIAS(ptr, array, size) ((void*)(ptr) >= (void*)(array) && (void*)(ptr) <= (void*)((uchar*)(array) + size))

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
    // these raise the required alignment on my pc to 128 from 64
    // so we don't use them
    // props.limits.optimalBufferCopyOffsetAlignment,   // for copy src/dst offsets
    // props.limits.optimalBufferCopyRowPitchAlignment, // for row pitch in image copies
  };

  return _max_elem(alignment_list, nv_arrlen(alignment_list));
}

static inline nv_error
_generate_and_insert_buffer_copy(VkCommandBuffer cmd, nv_gpu_buffer_t* dst, nv_gpu_buffer_t* src, size_t num_bytes, size_t dst_offset, size_t src_offset)
{
  VkBufferCopy copy = (VkBufferCopy){
    .srcOffset = src_offset,
    .dstOffset = dst_offset,
    .size      = num_bytes,
  };
  vkCmdCopyBuffer(cmd, src->buffer, dst->buffer, 1, &copy);

  return NV_SUCCESS;
}

nv_error
nvvk_driver_init(nvvk_ctx_t* ctx, nvvk_driver* dst)
{
  nv_assert_else_return(nvvk_ctx_is_valid(ctx) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);

  nv_bzero(dst, sizeof(nvvk_driver));

  dst->canary = 0xDEADBEEF;

  dst->vkctx = ctx;

  nv_error code = NV_SUCCESS;

  /* NOTE: POINTERS. We're storing POINTERS */
  code = nv_list_init(sizeof(nv_gpu_buffer_t*), 16, nv_allocator_c, NULL, &dst->buffers);
  nv_assert_else_return(code == NV_SUCCESS, code);

  code = nv_list_init(sizeof(nv_gpu_sampler_t), 16, nv_allocator_c, NULL, &dst->samplers);
  nv_assert_else_return(code == NV_SUCCESS, code);

  nv_gpu_memory_pool_create_info_t pool_ci = (nv_gpu_memory_pool_create_info_t){
    .size              = 1000000,
    .minimum_alignment = 1,
    .memory_flags      = NV_GPU_MEMORY_MAPPABLE_BIT,
    .type              = NV_GPU_ALLOCATOR_FREELIST,
    .policy            = NV_GPU_ALLOCATOR_POLICY_BEST_FIT,
    .flags             = 0,
  };
  code = nv_gpu_memory_pool_init(dst, &pool_ci, &dst->cpu_mappable_pool);
  nv_assert_else_return(code == NV_SUCCESS, code);

  pool_ci = (nv_gpu_memory_pool_create_info_t){
    .size              = 1000000,
    .minimum_alignment = 1,
    .memory_flags      = 0,
    .type              = NV_GPU_ALLOCATOR_FREELIST,
    .policy            = NV_GPU_ALLOCATOR_POLICY_WORST_FIT,
    .flags             = 0,
  };
  code = nv_gpu_memory_pool_init(dst, &pool_ci, &dst->gpu_local_pool);
  nv_assert_else_return(code == NV_SUCCESS, code);

  code =
      nv_gpu_memory_pool_allocate(&dst->cpu_mappable_pool, NOVA_GPU_SMALL_TRANSFER_BUFFER_SIZE, NOVA_GPU_SMALL_TRANSFER_BUFFER_ALIGNMENT, &dst->small_transfer_buffer_memory);
  code = nv_gpu_memory_pool_allocate(
      &dst->cpu_mappable_pool, NOVA_GPU_LARGE_TRANSFER_BUFFER_INITIAL_SIZE, NOVA_GPU_LARGE_TRANSFER_BUFFER_ALIGNMENT, &dst->large_transfer_buffer_memory);

  code = nv_gpu_buffer_init(
      dst,
      &dst->small_transfer_buffer_memory,
      NOVA_GPU_SMALL_TRANSFER_BUFFER_SIZE,
      NOVA_GPU_SMALL_TRANSFER_BUFFER_ALIGNMENT,
      NOVA_GPU_SMALL_TRANSFER_BUFFER_CREATE_FLAGS,
      &dst->small_transfer_buffer);
  nv_assert_else_return(code == NV_SUCCESS, code);

  code = nv_gpu_buffer_init(
      dst,
      &dst->large_transfer_buffer_memory,
      NOVA_GPU_LARGE_TRANSFER_BUFFER_INITIAL_SIZE,
      NOVA_GPU_LARGE_TRANSFER_BUFFER_ALIGNMENT,
      NOVA_GPU_LARGE_TRANSFER_BUFFER_CREATE_FLAGS,
      &dst->large_transfer_buffer);
  nv_assert_else_return(code == NV_SUCCESS, code);

  /**
   * This should never fail and indicates an error in the implementation of nvvk_driver_is_valid.
   */
  nv_assert_else_return(nvvk_driver_is_valid(dst) == true, NV_ERROR_INVALID_RETVAL);

  return NV_SUCCESS;
}

void
nvvk_driver_destroy(nvvk_driver* driver)
{
  if (!driver || nvvk_driver_is_valid(driver) == false)
  {
    return;
  }

  if (nv_list_size(&driver->buffers) != 0)
  {
    nv_log_error("Driver still held %zu buffers at time of destruction.\n", nv_list_size(&driver->buffers));
  }

  nv_gpu_memory_pool_destroy(&driver->cpu_mappable_pool);
  nv_gpu_memory_pool_destroy(&driver->gpu_local_pool);

  for (size_t i = 0; i < nv_list_size(&driver->samplers); i++)
  {
    nv_gpu_sampler_t* sampler = (nv_gpu_sampler_t*)nv_list_get(&driver->samplers, i);
    if (sampler && sampler->vksampler)
    {
      vkDestroySampler(driver->vkctx->device, sampler->vksampler, &driver->vkctx->vkalloc);
    }
  }

  for (size_t i = 0; i < NOVA_GPU_COMMAND_BUFFER_CACHE_COUNT; i++)
  {
    vkDestroyFence(driver->vkctx->device, driver->vkctx->cmd_buffer_fences[i], &driver->vkctx->vkalloc);
  }

  nv_list_destroy(&driver->buffers);
  nv_list_destroy(&driver->samplers);

  nv_gpu_buffer_destroy(&driver->small_transfer_buffer);
  nv_gpu_buffer_destroy(&driver->large_transfer_buffer);

  nv_bzero(driver, sizeof(nvvk_driver));
}

bool
nvvk_driver_is_valid(const nvvk_driver* driver)
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

  if (driver->vkctx == NULL || nvvk_ctx_is_valid(driver->vkctx) == false)
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

static inline nv_gpu_buffer_t*
_get_buffer_for_transfer(nvvk_driver* driver, vk_size_t size)
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
  usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

  return usage;
}

static inline VkMemoryPropertyFlags
_nv_to_vk_memory_properties(nv_gpu_memory_new_flags memory_flags, nv_gpu_buffer_flags buffer_flags)
{
  VkMemoryPropertyFlags props = 0;

  // readback requires CPU visible memory
  if (memory_flags & NV_GPU_MEMORY_READBACK_OPTIMAL_BIT)
  {
    props |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  }

  if (memory_flags & NV_GPU_MEMORY_PERSISTENT_MAPPED_BIT)
  {
    props |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
  }

  // transient/volatile buffers are often best in device-local memory
  /* Vertex, index and SS buffers need to be in device local memory. */
  if ((memory_flags & NV_GPU_MEMORY_TRANSIENT_BIT) || (memory_flags & NV_GPU_MEMORY_VOLATILE_BIT) || (buffer_flags & NV_GPU_BUFFER_VERTEX_BUFFER_BIT)
      || (buffer_flags & NV_GPU_BUFFER_SS_BUFFER_BIT))
  {
    props |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  }

  if (memory_flags & NV_GPU_MEMORY_MAPPABLE_BIT)
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
_create_buffer(nvvk_driver* driver, vk_size_t size, nv_gpu_buffer_flags flags, VkBuffer* dst)
{
  const VkBufferUsageFlags vk_buffer_flags = _nv_to_vk_buffer_usage(flags);

  VkBufferCreateInfo buffer_info = nv_zero_init(VkBufferCreateInfo);
  buffer_info.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size               = size;
  buffer_info.usage              = vk_buffer_flags;
  nvvk_result_check(*driver->vkctx, vkCreateBuffer(driver->vkctx->device, &buffer_info, &driver->vkctx->vkalloc, dst));
  nv_assert_else_return(*dst != VK_NULL_HANDLE, NV_ERROR_EXTERNAL);

  return NV_SUCCESS;
}

nv_error
nv_gpu_buffer_init(nvvk_driver* driver, nv_gpu_memory_block_t* memory, vk_size_t size, size_t alignment, nv_gpu_buffer_flags flags, nv_gpu_buffer_t* dst)
{
  nv_assert_else_return(nvvk_driver_is_valid(driver) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(size != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);

  nv_bzero(dst, sizeof(nv_gpu_buffer_t));

  nv_error code = NV_SUCCESS;

  alignment = NV_MAX(alignment, NOVA_GPU_BUFFER_MINIMUM_ALIGNMENT);
  alignment = NV_MAX(alignment, _get_preferred_alignment(driver->vkctx));

  const vk_size_t buffer_size = _align_up_size(size, alignment);
  if ((code = _create_buffer(driver, buffer_size, flags, &dst->buffer)) != NV_SUCCESS)
  {
    return code;
  }

  dst->block = memory;

  VkMemoryRequirements memory_requirements;
  vkGetBufferMemoryRequirements(driver->vkctx->device, dst->buffer, &memory_requirements);
  nv_assert_else_return(memory_requirements.size != 0, NV_ERROR_EXTERNAL);
  nv_assert_else_return(memory_requirements.alignment != 0, NV_ERROR_EXTERNAL);

  dst->size      = dst->block->size;
  dst->alignment = dst->block->alignment;
  dst->flags     = flags;
  dst->driver    = driver;

  nvvk_result_check(*driver->vkctx, vkBindBufferMemory(driver->vkctx->device, dst->buffer, nv_gpu_memory_get_backing(dst->block), dst->block->offset));

  return NV_SUCCESS;
}

void
nv_gpu_buffer_destroy(nv_gpu_buffer_t* buffer)
{
  if (!buffer)
  {
    return;
  }

  nv_assert_else_return(nvvk_driver_is_valid(buffer->driver), );
  nv_assert_else_return(nvvk_ctx_is_valid(buffer->driver->vkctx), );

  nvvk_ctx_t* ctx = buffer->driver->vkctx;
  nv_assert_else_return(nvvk_driver_is_valid(buffer->driver) == true, );
  nv_assert_else_return(nvvk_ctx_is_valid(buffer->driver->vkctx) == true, );

  vkDeviceWaitIdle(ctx->device);

  vkDestroyBuffer(ctx->device, buffer->buffer, &ctx->vkalloc);
  // vkFreeMemory(ctx->device, buffer->driver->pool.memory, NULL);
}

size_t
nv_gpu_buffer_size(const nv_gpu_buffer_t* buffer)
{
  return buffer->size;
}

static inline nv_error
_nv_transfer_to_cpu_visible_buffer(nv_gpu_buffer_t* buffer, const void* data, vk_size_t data_size, vk_size_t offset)
{
  nv_assert_else_return(nvvk_driver_is_valid(buffer->driver) == true, NV_ERROR_INVALID_ARG);

  void*    mapping = NULL;
  nv_error code    = NV_SUCCESS;

  code = nv_gpu_memory_map(buffer->block, offset, data_size, &mapping);
  if (code != NV_SUCCESS)
  {
    return code;
  }

  nv_memcpy(mapping, data, data_size);

  if (!(buffer->block->flags & NV_GPU_MEMORY_PERSISTENT_MAPPED_BIT))
  {
    nv_gpu_memory_unmap(buffer->block);
  }

  return NV_SUCCESS;
}

static inline nv_error
_nv_stage_transfer_to_buffer(nv_gpu_buffer_t* buffer, const void* data, vk_size_t data_size, vk_size_t offset)
{
  nv_assert_else_return(nvvk_driver_is_valid(buffer->driver) == true, NV_ERROR_INVALID_ARG);

  nvvk_driver* const driver = buffer->driver;
  nv_error           code   = NV_SUCCESS;

  if ((buffer->size + offset) < NOVA_GPU_SMALL_TRANSFER_BUFFER_SIZE)
  {
    void* mapping = NULL;
    code          = nv_gpu_memory_map(driver->small_transfer_buffer.block, 0, data_size, &mapping);
    if (!code)
    {
      return code;
    }

    nv_memmove(mapping, data, data_size);

    code = nv_gpu_buffer_copy(&driver->small_transfer_buffer, buffer, data_size, offset, 0);
    if (!code)
    {
      return code;
    }

    nv_gpu_memory_flush(buffer->block);
  }
  else if ((buffer->size + offset) < NOVA_GPU_LARGE_TRANSFER_BUFFER_INITIAL_SIZE)
  {
    void* mapping = NULL;
    code          = nv_gpu_memory_map(driver->large_transfer_buffer.block, 0, data_size, &mapping);
    if (!code)
    {
      return code;
    }

    nv_memmove(mapping, data, data_size);

    code = nv_gpu_buffer_copy(&driver->large_transfer_buffer, buffer, data_size, offset, 0);
    if (!code)
    {
      return code;
    }

    nv_gpu_memory_flush(buffer->block);
  }
  else
  {
    return NV_ERROR_INVALID_RETVAL;
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

  // we stop writing to CPU visible memory for writes up to 64 KiB
  if (buffer->block->flags & NV_GPU_MEMORY_MAPPABLE_BIT && data_size < nv_bytes_to_kib(64))
  {
    return _nv_transfer_to_cpu_visible_buffer(buffer, data, data_size, offset);
  }

  return _nv_stage_transfer_to_buffer(buffer, data, data_size, offset);
}

nv_error
nv_gpu_memory_flush(nv_gpu_memory_block_t* memory)
{
  nv_assert_else_return(memory != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_driver_is_valid(memory->driver) == true, NV_ERROR_INVALID_ARG);

  if (memory->drv_mapped_size <= 0 || memory->drv_mapped == NULL)
  {
    return NV_SUCCESS;
  }

  /* because we never really have host_coherent_bit in vk flags, we must always flush the memory */
  VkMappedMemoryRange range = {
    .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
    .memory = nv_gpu_memory_get_backing(memory),
    .offset = memory->drv_mapped_offset + memory->offset,
    .size   = memory->drv_mapped_size,
  };
  vkFlushMappedMemoryRanges(memory->driver->vkctx->device, 1, &range);

  return NV_SUCCESS;
}

static inline nv_error
_readback_buffer_staged(nv_gpu_buffer_t* buffer, vk_size_t offset, vk_size_t size, void* dst)
{
  nv_assert_else_return(buffer != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_driver_is_valid(buffer->driver) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(size != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return((size + offset) < buffer->size, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);

  nvvk_driver* driver = buffer->driver;

  nv_gpu_memory_block_t staging_memory;
  nv_gpu_memory_allocate_dedicated(driver, NV_GPU_MEMORY_READBACK_OPTIMAL_BIT | NV_GPU_MEMORY_PERSISTENT_MAPPED_BIT, size, 8, &staging_memory);

  nv_gpu_buffer_t staging;
  nv_gpu_buffer_init(buffer->driver, &staging_memory, size, 8, NV_GPU_BUFFER_READBACK_ONLY_BIT, &staging);

  VkCommandBuffer cmd = lr::nv_vk_begin_command_buffer(driver);

  VkBufferCopy copy = { .srcOffset = offset, .dstOffset = 0, .size = size };
  vkCmdCopyBuffer(cmd, nv_gpu_buffer_get_backing(buffer), nv_gpu_buffer_get_backing(&staging), 1, &copy);

  lr::nv_vk_end_command_buffer(driver, cmd, driver->vkctx->transfer_queue, true);

  void* mapping = NULL;
  nv_gpu_memory_map(&staging_memory, 0, size, &mapping);
  nv_memcpy(mapping, dst, size);
  nv_gpu_memory_unmap(&staging_memory);

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
  nv_assert_else_return(has_flag(buffer->block->flags, NV_GPU_MEMORY_TRANSIENT_BIT) == false, NV_ERROR_INVALID_ARG);

  /* persistent mapping implies the whole buffer is mapped, just read it in */
  if (buffer->block->flags & NV_GPU_MEMORY_PERSISTENT_MAPPED_BIT)
  {
    void* mapping = NULL;

    nv_error code = nv_gpu_memory_map(buffer->block, offset, size, &mapping);
    nv_assert_else_return(code == NV_SUCCESS, code);

    nv_memcpy(dst, mapping, size);
    return NV_SUCCESS;
  }
  else if (buffer->block->drv_mapped && buffer->block->drv_mapped_size >= size && buffer->block->drv_mapped_offset <= offset)
  {
    /* that weird shenanigans with the offset is to make it absolute. The offset should be absolute to the buffer. */
    const unsigned char* src = (unsigned char*)buffer->block->drv_mapped + (offset - buffer->block->drv_mapped_offset);
    nv_memcpy(dst, src, size);
    return NV_SUCCESS;
  }

  /* nothing else worked, slowly stage a transfer from the GPU to the CPU */
  return _readback_buffer_staged(buffer, offset, size, dst);
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
  nv_assert_else_return((num_bytes + dst_offset) <= dst->size, NV_ERROR_INVALID_ARG);
  nv_assert_else_return((num_bytes + src_offset) <= src->size, NV_ERROR_INVALID_ARG);

  if (src->block->pool == dst->block->pool)
  {
    nv_assert_else_return((num_bytes + src_offset) < dst_offset, NV_ERROR_INVALID_ARG);
  }

  VkCommandBuffer cmd = lr::nv_vk_begin_command_buffer(dst->driver);

  _generate_and_insert_buffer_copy(cmd, dst, src, num_bytes, dst_offset, src_offset);
  _nv_gpu_buffer_insert_read_barrier(dst, cmd);

  lr::nv_vk_end_command_buffer(dst->driver, cmd, dst->driver->vkctx->transfer_queue, true);

  return NV_SUCCESS;
}

nv_gpu_memory_new_flags
nv_gpu_vk_memory_flags_to_nv_flags(VkMemoryPropertyFlags flags)
{
  nv_gpu_memory_new_flags ret = 0;

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

  ret |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

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

bool
nv_gpu_memory_does_overlap(const nv_gpu_memory_block_t* mem_1, const nv_gpu_memory_block_t* mem_2)
{
  if (NV_UNLIKELY(mem_1 == mem_2))
  {
    return true;
  }

  if ((mem_1->flags & NV_GPU_MEMORY_DEDICATED_BIT) || (mem_2->flags & NV_GPU_MEMORY_DEDICATED_BIT))
  {
    return false;
  }

  const bool regions_overlap = ((mem_1->size + mem_1->offset) < mem_2->offset) || ((mem_2->size + mem_2->offset) < mem_1->offset);
  if ((mem_1->pool == mem_2->pool) && regions_overlap)
  {
    return true;
  }

  return false;
}

VkBuffer
nv_gpu_buffer_get_backing(const nv_gpu_buffer_t* buffer)
{
  nv_assert_else_return(buffer != NULL, VK_NULL_HANDLE);
  nv_assert_else_return(nvvk_driver_is_valid(buffer->driver) == true, VK_NULL_HANDLE);
  return buffer->buffer;
}

VkDeviceMemory
nv_gpu_memory_get_backing(const nv_gpu_memory_block_t* block)
{
  if (block->flags & NV_GPU_MEMORY_DEDICATED_BIT)
  {
    return block->dedicated_allocation;
  }
  return block->pool->memory;
}

nv_error
nv_gpu_memory_pool_init(nvvk_driver* driver, const nv_gpu_memory_pool_create_info_t* info, nv_gpu_memory_pool_t* dst)
{
  nv_assert_else_return(driver != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_driver_is_valid(driver), NV_ERROR_INVALID_ARG);
  nv_assert_else_return(info != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(info->minimum_alignment != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(info->size != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);

  nv_bzero(dst, sizeof(nv_gpu_memory_pool_t));

  dst->canary = 0xDEADBEEF;

  const vk_size_t preffered_alignment = _get_preferred_alignment(driver->vkctx);
  const vk_size_t alignment           = NV_MAX(preffered_alignment, info->minimum_alignment);

  const vk_size_t aligned_size = _align_up_size(info->size, alignment);

  VkMemoryPropertyFlags vk_property_flags = nv_gpu_nv_memory_flags_to_vk_flags(info->memory_flags);

  VkMemoryAllocateInfo allocInfo = nv_zero_init(VkMemoryAllocateInfo);
  allocInfo.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize       = aligned_size;

  /**
   * We pass in uint32_max here to tell vulkan that
   * we aren't allocating for a resource and that all
   * memory types are valid.
   */
  allocInfo.memoryTypeIndex = lr::nv_vk_get_mem_type(driver->vkctx, UINT32_MAX, vk_property_flags);

  nvvk_result_check(*driver->vkctx, vkAllocateMemory(driver->vkctx->device, &allocInfo, &driver->vkctx->vkalloc, &dst->memory));
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
    case NV_GPU_ALLOCATOR_STACK: nv_gpu_memory_allocator_stack_init(aligned_size, &dst->backing_allocator.stack); break;
    case NV_GPU_ALLOCATOR_FREELIST: nv_gpu_freelist_init(aligned_size, &dst->backing_allocator.flist); break;
    default: break;
  }

  /**
   * TODO: We don't want to always map the *entire* memory, do we?
   */
  if (info->memory_flags & NV_GPU_MEMORY_MAPPABLE_BIT)
  {
    nvvk_result_check(*driver->vkctx, vkMapMemory(driver->vkctx->device, dst->memory, 0, aligned_size, 0, &dst->drv_mapped));
    nv_assert_else_return(dst->drv_mapped != NULL, NV_ERROR_INVALID_RETVAL);
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

  VkDevice device = pool->driver->vkctx->device;

  if (pool->memory != VK_NULL_HANDLE)
  {
    vkDeviceWaitIdle(device);
    vkFreeMemory(device, pool->memory, &pool->driver->vkctx->vkalloc);
  }

  switch (pool->type)
  {
    case NV_GPU_ALLOCATOR_STACK: nv_gpu_memory_allocator_stack_destroy(&pool->backing_allocator.stack); break;
    case NV_GPU_ALLOCATOR_FREELIST: nv_gpu_freelist_destroy(&pool->backing_allocator.flist); break;
    default: break;
  }

  nv_zero_structp(pool);
}

nv_error
nv_gpu_memory_map(nv_gpu_memory_block_t* memory, size_t offset, size_t size, void** mapping)
{
  /* If we're already mapped and the current mapping isn't out of bounds of the request */
  if ((memory->flags & NV_GPU_MEMORY_PERSISTENT_MAPPED_BIT) && memory->drv_mapped != NULL)
  {
    if (memory->drv_mapped_size >= size && memory->drv_mapped_offset <= offset)
    {
      *mapping = (unsigned char*)memory->drv_mapped + offset;
      return NV_SUCCESS;
    }
    /* The current size is out of bounds, unmap it and map it again */
    // vkUnmapMemory(device, memory->driver->pool.memory);

    memory->drv_mapped = NULL;
  }

  if (memory->flags & NV_GPU_MEMORY_DEDICATED_BIT)
  {
    void*    tmp = NULL;
    VkResult res = vkMapMemory(memory->driver->vkctx->device, memory->dedicated_allocation, offset, size, 0, &tmp);
    nv_assert_else_return(res == VK_SUCCESS, NV_ERROR_EXTERNAL);

    memory->drv_mapped        = tmp;
    memory->drv_mapped_size   = size;
    memory->drv_mapped_offset = offset;

    *mapping = memory->drv_mapped;

    return NV_SUCCESS;
  }

  /* being CPU visible is an assertion */
  // VkResult result = vkMapMemory(device, memory->driver->pool.memory, memory->block->offset + offset, size, 0, &memory->drv_mapped);
  // nv_assert_else_return(result == VK_SUCCESS, NV_ERROR_EXTERNAL);
  if (memory->pool)
  {
    nv_assert_else_return(memory->pool->drv_mapped != NULL, NV_ERROR_EXTERNAL);
  }
  memory->drv_mapped = (uchar*)memory->pool->drv_mapped + memory->offset + offset;
  nv_assert_else_return(memory->drv_mapped != NULL, NV_ERROR_EXTERNAL);

  memory->drv_mapped_size   = size;
  memory->drv_mapped_offset = offset;

  *mapping = memory->drv_mapped;

  nv_assert_else_return(*mapping != NULL, NV_ERROR_BROKEN_STATE);

  return NV_SUCCESS;
}

void
nv_gpu_memory_unmap(nv_gpu_memory_block_t* memory)
{
  nv_assert_else_return(memory != NULL, );
  nv_assert_else_return(memory->drv_mapped_size != 0, );
  nv_assert_else_return(memory->drv_mapped_offset < memory->size, );
  nv_assert_else_return(has_flag(memory->flags, NV_GPU_MEMORY_PERSISTENT_MAPPED_BIT) == false, );
  if (has_flag(memory->flags, NV_GPU_MEMORY_PERSISTENT_MAPPED_BIT) == false)
  {
    nv_log_error("pee is stored in the balls\n");
    return;
  }

  VkDevice device = memory->driver->vkctx->device;

  nv_error code = nv_gpu_memory_flush(memory);
  if (code != NV_SUCCESS)
  {
    return;
  }

  if (memory->flags & NV_GPU_MEMORY_DEDICATED_BIT)
  {
    vkUnmapMemory(device, nv_gpu_memory_get_backing(memory));
  }

  memory->drv_mapped        = NULL;
  memory->drv_mapped_size   = 0;
  memory->drv_mapped_offset = 0;
}

nv_error
nv_gpu_memory_allocate_dedicated(nvvk_driver* driver, nv_gpu_memory_new_flags flags, vk_size_t size, vk_size_t alignment, nv_gpu_memory_block_t* dst_block)
{
  nv_assert_else_return(nvvk_driver_is_valid(driver) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst_block != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(size != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(alignment != 0, NV_ERROR_INVALID_ARG);

  nv_zero_structp(dst_block);

  const vk_size_t preffered_alignment = _get_preferred_alignment(driver->vkctx);
  alignment                           = NV_MAX(preffered_alignment, alignment);

  const vk_size_t aligned_size = _align_up_size(size, alignment);

  flags |= NV_GPU_MEMORY_DEDICATED_BIT;
  VkMemoryPropertyFlags vk_property_flags = nv_gpu_nv_memory_flags_to_vk_flags(flags);

  VkMemoryAllocateInfo allocInfo = nv_zero_init(VkMemoryAllocateInfo);
  allocInfo.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize       = aligned_size;

  /**
   * We pass in uint32_max here to tell vulkan that
   * we aren't allocating for a resource and that all
   * memory types are valid.
   */
  allocInfo.memoryTypeIndex = lr::nv_vk_get_mem_type(driver->vkctx, UINT32_MAX, vk_property_flags);

  nvvk_result_check(*driver->vkctx, vkAllocateMemory(driver->vkctx->device, &allocInfo, &driver->vkctx->vkalloc, &dst_block->dedicated_allocation));
  nv_assert_else_return(dst_block->dedicated_allocation != VK_NULL_HANDLE, NV_ERROR_MALLOC_FAILED);

  dst_block->driver    = driver;
  dst_block->size      = aligned_size;
  dst_block->alignment = alignment;
  dst_block->flags     = flags;
  dst_block->offset    = 0;

  return NV_SUCCESS;
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
  nv_assert_else_return(_align_up_size(size, alignment) <= pool->allocated_size, NV_ERROR_INVALID_ARG);

  const vk_size_t preffered_alignment = NV_MAX(pool->alignment, alignment);
  const vk_size_t aligned_size        = _align_up_size(size, preffered_alignment);

  // pool->user_data += aligned_size;
  // nv_log_info("VKMEMUSG::(%llu|+%llu)\n", pool->user_data, aligned_size);

  if (pool->type == NV_GPU_ALLOCATOR_STACK)
  {
    nv_gpu_allocator_stack_t* stack = &pool->backing_allocator.stack;

    *dst_block = (nv_gpu_memory_block_t){
      .driver    = pool->driver,
      .pool      = pool,
      .flags     = pool->memory_flags,
      .size      = aligned_size,
      .offset    = stack->bumper,
      .alignment = preffered_alignment,
    };

    stack->last_allocation_bumper = stack->bumper;

    stack->bumper += aligned_size;
    stack->bumper = _align_up_size(stack->bumper, preffered_alignment);

    nv_assert_else_return(stack->bumper <= pool->allocated_size, NV_ERROR_MALLOC_FAILED);
  }
  else if (pool->type == NV_GPU_ALLOCATOR_FREELIST)
  {
    nv_gpu_freelist_t* freelist = &pool->backing_allocator.flist;

    vk_size_t offset = SIZE_MAX;
    nv_gpu_freelist_alloc(freelist, aligned_size, preffered_alignment, pool->policy, &offset);

    nv_assert_else_return(offset != SIZE_MAX, NV_ERROR_INVALID_RETVAL);
    nv_assert_else_return((offset % preffered_alignment) == 0, NV_ERROR_INVALID_RETVAL);

    *dst_block = (nv_gpu_memory_block_t){
      .driver    = pool->driver,
      .pool      = pool,
      .flags     = pool->memory_flags,
      .size      = aligned_size,
      .offset    = offset,
      .alignment = preffered_alignment,
    };
  }
  else
  {
    nv_log_error("Unimplemented type for allocator.\n");
    return NV_ERROR_INVALID_INPUT;
  }

  /* assert that block is aligned */
  nv_assert_else_return((dst_block->size % _get_preferred_alignment(pool->driver->vkctx)) == 0, NV_ERROR_UNKNOWN);

  return NV_SUCCESS;
}

nv_error
nv_gpu_memory_pool_free(nv_gpu_memory_block_t* block)
{
  nv_assert_else_return(block != NULL, NV_ERROR_INVALID_ARG);

  if ((block->flags & NV_GPU_MEMORY_DEDICATED_BIT) != 0 && block->dedicated_allocation != VK_NULL_HANDLE)
  {
    vkFreeMemory(block->driver->vkctx->device, block->dedicated_allocation, &block->driver->vkctx->vkalloc);
    return NV_SUCCESS;
  }

  nv_gpu_memory_pool_t* pool = block->pool;

  nv_assert_else_return(pool != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(pool->canary == 0xDEADBEEF, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(pool->memory != VK_NULL_HANDLE, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(pool->allocated_size > 0, NV_ERROR_INVALID_ARG);

  // pool->user_data -= block->size;
  // nv_log_info("VKMEMUSG::(%llu|-%llu)\n", pool->user_data, block->size);

  if (pool->type == NV_GPU_ALLOCATOR_STACK)
  {
    nv_gpu_allocator_stack_t* stack = &pool->backing_allocator.stack;

    if (block->offset == stack->last_allocation_bumper)
    {
      stack->bumper -= block->size;
      stack->bumper = _align_up_size(stack->bumper, pool->alignment);
    }
  }
  else if (pool->type == NV_GPU_ALLOCATOR_FREELIST)
  {
    nv_gpu_freelist_t* freelist = &pool->backing_allocator.flist;
    nv_gpu_freelist_free(freelist, block->size, block->offset);
  }
  else
  {
    nv_log_error("Unimplemented type for allocator.\n");
    return NV_ERROR_INVALID_INPUT;
  }

  return NV_SUCCESS;
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

nv_error
nvvk_allocator_init(nvvk_allocator_t* dst)
{
  return NV_SUCCESS;
}

void
nvvk_allocator_destroy(nvvk_allocator_t* alloc)
{
}

void*
nvvk_alloc(void* user_data, size_t size, size_t alignment, VkSystemAllocationScope scope)
{
  return nv_aligned_alloc(size, alignment);
}

void*
nvvk_realloc(void* user_data, void* orig, size_t new_size, size_t alignment, VkSystemAllocationScope scope)
{
  return nv_aligned_realloc(orig, new_size, alignment);
}

void
nvvk_free(void* pUserData, void* ptr)
{
  nv_aligned_free(ptr);
}

void
nvvk_internal_allocation(void* pUserData, size_t size, VkInternalAllocationType allocationType, VkSystemAllocationScope allocationScope)
{
  (void)pUserData;
  (void)size;
  (void)allocationType;
  (void)allocationScope;
}

void
nvvk_internal_free(void* pUserData, size_t size, VkInternalAllocationType allocationType, VkSystemAllocationScope allocationScope)
{
  (void)pUserData;
  (void)size;
  (void)allocationType;
  (void)allocationScope;
}

nv_error
nv_gpu_buffer_resize(nv_gpu_buffer_t* buffer, size_t new_size, size_t new_alignment, bool copy_old_data)
{
  nv_assert_else_return(buffer != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_driver_is_valid(buffer->driver) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(buffer->block->flags & NV_GPU_MEMORY_RESIZABLE_BIT, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(new_size != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(new_alignment != 0, NV_ERROR_INVALID_ARG);

  (void)(copy_old_data);

  VkBuffer              new_buffer = VK_NULL_HANDLE;
  nv_gpu_memory_block_t new_block  = nv_zero_init(nv_gpu_memory_block_t);

  nv_assert_else_return(0, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(new_buffer != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(new_block.size != 0, NV_ERROR_BROKEN_STATE);

  return NV_SUCCESS;
}

static inline nv_gpu_freelist_block_t*
_alloc_node(nv_gpu_freelist_t* flist)
{
  if (flist->free_nodes)
  {
    nv_gpu_freelist_block_t* node = flist->free_nodes;
    flist->free_nodes             = node->next;
    return node;
  }
  return nv_alloc_struct(nv_gpu_freelist_block_t);
}

static inline void
_free_node(nv_gpu_freelist_t* flist, nv_gpu_freelist_block_t* node)
{
  node->next        = flist->free_nodes;
  flist->free_nodes = node;
}

nv_error
nv_gpu_freelist_init(size_t initial_capacity, nv_gpu_freelist_t* dst)
{
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);

  nv_zero_structp(dst);

  dst->root         = _alloc_node(dst);
  dst->root->offset = 0;
  dst->root->size   = initial_capacity;
  dst->root->next   = NULL;

  return NV_SUCCESS;
}

void
nv_gpu_freelist_destroy(nv_gpu_freelist_t* flist)
{
  if (!flist)
  {
    return;
  }

  nv_gpu_freelist_block_t* node = flist->root;
  while (node)
  {
    nv_gpu_freelist_block_t* next = node->next;
    nv_free(node);
    node = next;
  }

  node = flist->free_nodes;
  while (node)
  {
    nv_gpu_freelist_block_t* next = node->next;
    nv_free(node);
    node = next;
  }
}

static inline nv_error
_nv_gpu_freelist_insert_node_last(nv_gpu_freelist_t* flist, nv_gpu_freelist_block_t** ret)
{
  nv_assert_else_return(flist != NULL, NV_ERROR_INVALID_ARG);

  flist->num_nodes++;
  // nv_log_info("NNODES:%zu\n", flist->num_nodes);

  if (!flist->root)
  {
    flist->root = _alloc_node(flist);
    nv_assert_else_return(flist->root != NULL, NV_ERROR_MALLOC_FAILED);

    *ret = flist->root;
    return NV_SUCCESS;
  }

  nv_gpu_freelist_block_t* node = flist->root;
  while (node->next)
  {
    node = node->next;
  }

  node->next = _alloc_node(flist);
  nv_assert_else_return(node->next != NULL, NV_ERROR_MALLOC_FAILED);

  *ret = node->next;

  return NV_SUCCESS;
}

static inline nv_gpu_freelist_block_t*
_fl_alloc(nv_gpu_freelist_t* flist, size_t aligned_size, nv_gpu_allocator_policy policy, nv_gpu_freelist_block_t** prev)
{
  nv_gpu_freelist_block_t* best_fit_node  = NULL;
  nv_gpu_freelist_block_t* worst_fit_node = NULL;
  nv_gpu_freelist_block_t* best_fit_prev  = NULL;
  nv_gpu_freelist_block_t* worst_fit_prev = NULL;

  nv_gpu_freelist_block_t* node = flist->root;

  size_t min_fit_size    = SIZE_MAX;
  size_t curr_worst_size = 0;

  nv_gpu_freelist_block_t* prev_node = NULL;
  *prev                              = NULL;

  while (node)
  {
    if (node->size < aligned_size)
    {
      prev_node = node;
      node      = node->next;
      continue;
    }

    if (policy == NV_GPU_ALLOCATOR_POLICY_FIRST_FIT)
    {
      *prev = prev_node;
      return node;
    }

    if (node->size > curr_worst_size)
    {
      worst_fit_node  = node;
      curr_worst_size = node->size;
      worst_fit_prev  = prev_node;
    }

    if (node->size < min_fit_size)
    {
      best_fit_node = node;
      min_fit_size  = node->size;
      best_fit_prev = prev_node;
    }

    prev_node = node;
    node      = node->next;
  }

  if (policy == NV_GPU_ALLOCATOR_POLICY_WORST_FIT && worst_fit_node != NULL)
  {
    *prev = worst_fit_prev;
    return worst_fit_node;
  }
  else if (best_fit_node != NULL)
  {
    *prev = best_fit_prev;
    return best_fit_node;
  }

  return NULL;
}

bool
nv_gpu_freelist_alloc(nv_gpu_freelist_t* flist, size_t size, size_t alignment, nv_gpu_allocator_policy policy, size_t* offset_out)
{
  nv_assert_else_return(flist, false);
  nv_assert_else_return(flist->root != NULL, false);
  nv_assert_else_return(offset_out, false);

  nv_error code = nv_gpu_freelist_defrag(flist);
  if (code != NV_SUCCESS)
  {
    return false;
  }

  const size_t aligned_size = _align_up_size(size, alignment);

  nv_gpu_freelist_block_t* prev          = NULL;
  nv_gpu_freelist_block_t* best_fit_node = _fl_alloc(flist, aligned_size, policy, &prev);

  if (!best_fit_node)
  {
    return false;
  }

  if (prev)
  {
    prev->next = best_fit_node->next;
  }
  else
  {
    flist->root = best_fit_node->next;
  }

  size_t aligned_offset    = _align_up_size(best_fit_node->offset, alignment);
  size_t alignment_padding = aligned_offset - best_fit_node->offset;

  if (best_fit_node->size < alignment_padding + size)
  {
    return NV_ERROR_MALLOC_FAILED;
  }

  best_fit_node->offset = aligned_offset;
  best_fit_node->size -= alignment_padding + size;

  size_t remaining = best_fit_node->size;

  bool next_node_can_recieve_remaining = best_fit_node->next && best_fit_node->next->offset == (best_fit_node->offset + size);
  if (remaining > 0 && next_node_can_recieve_remaining)
  {
    best_fit_node->next->offset = best_fit_node->offset + size;
    best_fit_node->next->size += remaining;
    best_fit_node->size = size;
  }
  else if (remaining > 0)
  {
    nv_gpu_freelist_block_t* suffix = NULL;
    nv_error                 code   = _nv_gpu_freelist_insert_node_last(flist, &suffix);
    nv_assert_else_return(code == NV_SUCCESS, code);

    suffix->offset      = best_fit_node->offset + size;
    suffix->size        = remaining;
    best_fit_node->size = size;
  }

  *offset_out = best_fit_node->offset;

  _free_node(flist, best_fit_node);

  return true;
}

nv_error
nv_gpu_freelist_free(nv_gpu_freelist_t* flist, size_t offset, size_t size)
{
  nv_assert_else_return(flist != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(flist->root != NULL, NV_ERROR_INVALID_ARG);
  if (size == 0)
  {
    return NV_SUCCESS;
  }

  nv_gpu_freelist_block_t* new_node = NULL;

  nv_error code = _nv_gpu_freelist_insert_node_last(flist, &new_node);
  nv_assert_else_return(new_node != NULL && code == NV_SUCCESS, code);

  new_node->offset = offset;
  new_node->size   = size;

  code = nv_gpu_freelist_defrag(flist);
  if (code != NV_SUCCESS)
  {
    return code;
  }

  return NV_SUCCESS;
}

static inline nv_gpu_freelist_block_t*
_sorted_merge(nv_gpu_freelist_block_t* a, nv_gpu_freelist_block_t* b)
{
  if (!a)
  {
    return b;
  }
  if (!b)
  {
    return a;
  }

  if (a->offset <= b->offset)
  {
    a->next = _sorted_merge(a->next, b);
    return a;
  }
  else
  {
    b->next = _sorted_merge(a, b->next);
    return b;
  }
}

static inline void
_front_back_split(nv_gpu_freelist_block_t* source, nv_gpu_freelist_block_t** front, nv_gpu_freelist_block_t** back)
{
  nv_gpu_freelist_block_t* slow = source;
  nv_gpu_freelist_block_t* fast = source->next;

  while (fast)
  {
    fast = fast->next;
    if (fast)
    {
      slow = slow->next;
      fast = fast->next;
    }
  }

  *front     = source;
  *back      = slow->next;
  slow->next = NULL;
}

static inline nv_gpu_freelist_block_t*
_merge_sort(nv_gpu_freelist_block_t* root)
{
  if (!root || !root->next)
  {
    return root;
  }

  nv_gpu_freelist_block_t* a = NULL;
  nv_gpu_freelist_block_t* b = NULL;

  _front_back_split(root, &a, &b);

  a = _merge_sort(a);
  b = _merge_sort(b);

  return _sorted_merge(a, b);
}

static inline void
nv_gpu_freelist_sort(nv_gpu_freelist_t* flist)
{
  flist->root = _merge_sort(flist->root);
}

nv_error
nv_gpu_freelist_defrag(nv_gpu_freelist_t* flist)
{
  nv_gpu_freelist_block_t* cur = flist->root;

  while (cur && cur->next)
  {
    nv_gpu_freelist_block_t* next = cur->next;

    if (cur->offset + cur->size == next->offset)
    {
      cur->size += next->size;
      cur->next = next->next;

      _free_node(flist, next);
    }
    else
    {
      cur = next;
    }
  }

  return NV_SUCCESS;
}

void
_nv_gpu_buffer_insert_read_barrier(const nv_gpu_buffer_t* buffer, VkCommandBuffer cmd)
{
  VkBufferMemoryBarrier barrier = {
    .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
    .pNext               = NULL,
    .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
    .dstAccessMask       = VK_ACCESS_MEMORY_READ_BIT,
    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .buffer              = buffer->buffer,
    .offset              = 0,
    .size                = VK_WHOLE_SIZE, // or specific size
  };

  vkCmdPipelineBarrier(
      cmd,
      VK_PIPELINE_STAGE_TRANSFER_BIT,     // what stage just ran
      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, // what stage is coming next
      0,
      0,
      NULL,
      1,
      &barrier,
      0,
      NULL);
}

#define _PATHMAX 4096

static inline nv_error
readfile(const char* fname, char** data, size_t* data_size)
{
  nv_assert_else_return(fname != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(data != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(data_size != NULL, NV_ERROR_INVALID_ARG);

  FILE* f = fopen(fname, "rb");
  if (!f)
  {
    return NV_ERROR_FILE_NOT_FOUND;
  }

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  if (size < 0)
  {
    fclose(f);
    return NV_ERROR_IO_ERROR;
  }
  rewind(f);

  *data = (char*)nv_calloc(size + 1);
  if (!*data)
  {
    fclose(f);
    return NV_ERROR_MALLOC_FAILED;
  }

  size_t read_size = fread(*data, 1, size, f);
  fclose(f);

  if (read_size != (size_t)size)
  {
    nv_free(*data);
    *data = NULL;
    return NV_ERROR_IO_ERROR;
  }

  *data_size          = read_size;
  (*data)[*data_size] = '\0';
  return NV_SUCCESS;
}

nv_error
nv_prep_flatten_file_to_file(const char* file, FILE* out)
{
  nv_assert_else_return(file != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(out != NULL, NV_ERROR_INVALID_ARG);

  char*  contents      = NULL;
  size_t contents_size = 0;

  nv_error code = readfile(file, &contents, &contents_size);
  if (code != NV_SUCCESS)
  {
    return code;
  }

  char* ctx  = NULL;
  char* line = nv_strtok(contents, "\n", &ctx);

  while (line != NULL)
  {
    char* p = line;
    while (*p == ' ' || *p == '\t')
    {
      p++;
    }

    if (*p != '#')
    {
      fputs(line, out);
      fputc('\n', out);
      line = nv_strtok(NULL, "\n", &ctx);
      continue;
    }

    p++;

    while (*p == ' ' || *p == '\t')
    {
      p++;
    }

    if (nv_strncmp(p, "include", 7) == 0 && (p[7] == ' ' || p[7] == '\t' || p[7] == '\"'))
    {
      p += sizeof("include") - 1;

      while (*p == ' ' || *p == '\t')
      {
        p++;
      }

      if (*p != '\"')
      {
        fputs(line, out);
        fputc('\n', out);
        line = nv_strtok(NULL, "\n", &ctx);
        continue;
      }

      const char* start = p + 1;
      const char* end   = nv_strchr(start, '\"');

      if (!end || end <= start)
      {
        fputs(line, out);
        fputc('\n', out);
        line = nv_strtok(NULL, "\n", &ctx);
        continue;
      }

      size_t name_len = end - start;

      char tmp[1] = { 0 };

      /* 1) figure out directory of `file` */
      char*       base = NULL;
      const char* s1   = nv_strrchr(file, '/');
      const char* s2   = nv_strrchr(file, '\\');
      const char* sep  = s1 > s2 ? s1 : s2;
      if (sep)
      {
        size_t dir_len = sep - file + 1; /* include the slash */
        base           = (char*)nv_calloc(dir_len + 1);
        nv_memcpy(base, file, dir_len);
        base[dir_len] = '\0';
      }
      else
      {
        /**
         * Point base to a 0 byte.
         * This effectively sets the string to 0 length
         */
        base = tmp;
      }

      size_t full_len = nv_strlen(base) + name_len;

      char* fullpath = (char*)nv_calloc(full_len + 1);
      nv_snprintf(fullpath, full_len + 1, "%s%.*s", base, (int)name_len, start);
      fullpath[full_len] = 0;

      nv_error sub = nv_prep_flatten_file_to_file(fullpath, out);
      if (sub != NV_SUCCESS)
      {
        return sub;
      }

      fputc('\n', out);
      line = nv_strtok(NULL, "\n", &ctx);

      /**
       * We may point base to a stack
       * buffer. We dont' want to free that.
       */
      if (base != tmp)
      {
        nv_free(base);
      }
      nv_free(fullpath);
      continue;
    }

    fputs(line, out);
    fputc('\n', out);
    line = nv_strtok(NULL, "\n", &ctx);
  }

  nv_free(contents);
  return NV_SUCCESS;
}

nv_error
nv_prep_flatten_file_to_buffer(const char* file, char** buffer, size_t* buffer_size)
{
  nv_assert_else_return(file != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(buffer != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(buffer_size != NULL, NV_ERROR_INVALID_ARG);

  char*  contents      = NULL;
  size_t contents_size = 0;

  nv_error code = readfile(file, &contents, &contents_size);
  if (code != NV_SUCCESS)
  {
    return code;
  }

  if (!*buffer || *buffer_size == 0)
  {
    const size_t buffer_start_size = 4096;

    *buffer      = (char*)nv_calloc(buffer_start_size);
    *buffer_size = buffer_start_size;
  }

  const size_t buflen = nv_strlen(*buffer);
  if ((*buffer_size - buflen) <= contents_size)
  {
    const size_t new_buffer_size = NV_MAX(*buffer_size * 2, buflen + contents_size);
    *buffer                      = (char*)nv_realloc(*buffer, new_buffer_size);
    *buffer_size                 = new_buffer_size;
  }

  char* ctx  = NULL;
  char* line = nv_strtok(contents, "\n", &ctx);

  while (line != NULL)
  {
    char* p = line;
    while (*p == ' ' || *p == '\t')
    {
      p++;
    }

    if (*p != '#')
    {
      nv_strlcat(*buffer, line, *buffer_size);
      nv_strlcat(*buffer, "\n", *buffer_size);
      line = nv_strtok(NULL, "\n", &ctx);
      continue;
    }

    p++;

    while (*p == ' ' || *p == '\t')
    {
      p++;
    }

    if (nv_strncmp(p, "include", 7) == 0 && (p[7] == ' ' || p[7] == '\t' || p[7] == '\"'))
    {
      p += sizeof("include") - 1;

      while (*p == ' ' || *p == '\t')
      {
        p++;
      }

      if (*p != '\"')
      {
        nv_strlcat(*buffer, line, *buffer_size);
        nv_strlcat(*buffer, "\n", *buffer_size);
        line = nv_strtok(NULL, "\n", &ctx);
        continue;
      }

      const char* start = p + 1;
      const char* end   = nv_strchr(start, '\"');

      if (!end || end <= start)
      {
        nv_strlcat(*buffer, line, *buffer_size);
        nv_strlcat(*buffer, "\n", *buffer_size);
        line = nv_strtok(NULL, "\n", &ctx);
        continue;
      }

      size_t name_len = end - start;

      char tmp[1] = { 0 };

      /* 1) figure out directory of `file` */
      char*       base = NULL;
      const char* s1   = nv_strrchr(file, '/');
      const char* s2   = nv_strrchr(file, '\\');
      const char* sep  = s1 > s2 ? s1 : s2;
      if (sep)
      {
        size_t dir_len = sep - file + 1; /* include the slash */
        base           = (char*)nv_calloc(dir_len + 1);
        nv_memcpy(base, file, dir_len);
        base[dir_len] = '\0';
      }
      else
      {
        /**
         * Point base to a 0 byte.
         * This effectively sets the string to 0 length
         */
        base = tmp;
      }

      size_t full_len = nv_strlen(base) + name_len;

      char* fullpath = (char*)nv_calloc(full_len + 1);
      nv_snprintf(fullpath, full_len + 1, "%s%.*s", base, (int)name_len, start);
      fullpath[full_len] = 0;

      nv_strlcat(*buffer, "\n", *buffer_size);

      nv_error sub = nv_prep_flatten_file_to_buffer(fullpath, buffer, buffer_size);
      if (sub != NV_SUCCESS)
      {
        return sub;
      }

      nv_strlcat(*buffer, "\n", *buffer_size);

      line = nv_strtok(NULL, "\n", &ctx);

      /**
       * We may point base to a stack
       * buffer. We dont' want to free that.
       */
      if (base != tmp)
      {
        nv_free(base);
      }
      nv_free(fullpath);
      continue;
    }

    nv_strlcat(*buffer, line, *buffer_size);
    nv_strlcat(*buffer, "\n", *buffer_size);
    line = nv_strtok(NULL, "\n", &ctx);
  }

  nv_free(contents);
  return NV_SUCCESS;
}
