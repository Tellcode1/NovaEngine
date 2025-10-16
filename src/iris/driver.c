#include "../../include/iris/driver.h"
#include "../../external/volk/volk.h"
#include "../../include/iris/buffer.h"
#include "../../include/iris/memory.h"
#include "../../include/iris/sampler.h"
#include "../../include/iris/types.h"
#include "../../include/iris/utils.h"
#include "../../include/std/include/alloc.h"
#include "../../include/std/include/containers/list.h"
#include "../../include/std/include/errorcodes.h"
#include "../../include/std/include/stdafx.h"
#include "../../include/std/include/string.h"
#include "../../include/std/include/types.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef IRIS_DISABLE_OPTIMIZATIONS
#  define IRIS_DISABLE_OPTIMIZATIONS (true)
#endif

#define DOES_ALIAS(ptr, array, size) ((void*)(ptr) >= (void*)(array) && (void*)(ptr) <= (void*)((uchar*)(array) + (size)))

static inline bool
has_flag(u32 flags, u32 want)
{
  return (flags & want) != 0u;
}

static inline nv_error
generate_and_insert_buffer_copy(VkCommandBuffer cmd, VkBuffer dst, VkBuffer src, size_t num_bytes, size_t dst_offset, size_t src_offset)
{
  VkBufferCopy const copy = (VkBufferCopy){
    .srcOffset = src_offset,
    .dstOffset = dst_offset,
    .size      = num_bytes,
  };
  vkCmdCopyBuffer(cmd, src, dst, 1, &copy);

  return NV_SUCCESS;
}

nv_error
iris_driver_init(nvvk_ctx_t* ctx, iris_driver_t* dst)
{
  nv_assert_else_return(nvvk_ctx_is_valid(ctx) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);

  nv_bzero(dst, sizeof(iris_driver_t));

  dst->canary = 0xDEADBEEF;

  dst->vkctx = ctx;

  nv_error code = NV_SUCCESS;

  code = nv_list_init(sizeof(iris_sampler_internal_t), 16, nv_allocator_c, NULL, &dst->samplers);
  nv_assert_else_return(code == NV_SUCCESS, code);

  iris_memory_pool_create_info_t pool_ci = (iris_memory_pool_create_info_t){
    .size              = 10000000,
    .minimum_alignment = 1,
    .memory_flags      = IRIS_MEMORY_FLAGS_MAPPABLE_BIT,
    .type              = IRIS_ALLOCATOR_FREELIST,
    .policy            = IRIS_ALLOCATOR_POLICY_BEST_FIT,
    .flags             = 0,
  };
  code = iris_memory_pool_init(dst, &pool_ci, &dst->cpu_mappable_pool);
  nv_assert_else_return(code == NV_SUCCESS, code);

  pool_ci = (iris_memory_pool_create_info_t){
    .size              = 10000000,
    .minimum_alignment = 1,
    .memory_flags      = 0,
    .type              = IRIS_ALLOCATOR_FREELIST,
    .policy            = IRIS_ALLOCATOR_POLICY_WORST_FIT,
    .flags             = 0,
  };
  code = iris_memory_pool_init(dst, &pool_ci, &dst->gpu_local_pool);
  nv_assert_else_return(code == NV_SUCCESS, code);

  iris_buffer_extra_create_info_t extra_info = nv_zero_init(iris_buffer_extra_create_info_t);
  extra_info.custom_memory_flags             = dst->cpu_mappable_pool.memory_flags | IRIS_MEMORY_FLAGS_PERSISTENT_MAPPED_BIT;
  extra_info.custom_memory_pool              = &dst->cpu_mappable_pool;

  code = iris_buffer_init(
      dst, IRIS_SMALL_TRANSFER_BUFFER_SIZE, IRIS_SMALL_TRANSFER_BUFFER_ALIGNMENT, &extra_info, IRIS_SMALL_TRANSFER_BUFFER_CREATE_FLAGS, &dst->small_transfer_buffer);
  nv_assert_else_return(code == NV_SUCCESS, code);

  code = iris_buffer_init(
      dst, IRIS_LARGE_TRANSFER_BUFFER_INITIAL_SIZE, IRIS_LARGE_TRANSFER_BUFFER_ALIGNMENT, &extra_info, IRIS_LARGE_TRANSFER_BUFFER_CREATE_FLAGS, &dst->large_transfer_buffer);
  nv_assert_else_return(code == NV_SUCCESS, code);

  /**
   * This should never fail and indicates an error in the implementation of iris_driver_is_valid.
   */
  nv_assert_else_return(iris_driver_is_valid(dst) == true, NV_ERROR_INVALID_RETVAL);

  return NV_SUCCESS;
}

void
iris_driver_destroy(iris_driver_t* driver)
{
  if ((driver == NULL) || !iris_driver_is_valid(driver))
  {
    return;
  }

  for (size_t i = 0; i < nv_list_size(&driver->samplers); i++)
  {
    iris_sampler_internal_t* sampler = (iris_sampler_internal_t*)nv_list_get(&driver->samplers, i);
    if ((sampler != NULL) && (sampler->handle != NULL))
    {
      vkDestroySampler(driver->vkctx->device, sampler->handle, &driver->vkctx->vkalloc);
    }
  }

  for (size_t i = 0; i < IRIS_COMMAND_BUFFER_CACHE_COUNT; i++)
  {
    vkDestroyFence(driver->vkctx->device, driver->vkctx->cmd_buffer_fences[i], &driver->vkctx->vkalloc);
  }

  nv_list_destroy(&driver->samplers);

  iris_buffer_destroy(&driver->small_transfer_buffer);
  iris_buffer_destroy(&driver->large_transfer_buffer);

  /**
   * Always destroy child resources (buffers) before parent resources (their pools)
   */
  iris_memory_pool_destroy(&driver->cpu_mappable_pool);
  iris_memory_pool_destroy(&driver->gpu_local_pool);

  nv_bzero(driver, sizeof(iris_driver_t));
}

bool
iris_driver_is_valid(const iris_driver_t* driver)
{
  if (driver == NULL)
  {
    return false;
  }

  if (driver->canary != 0xDEADBEEF)
  {
    return false;
  }

  if (driver->vkctx == NULL || !nvvk_ctx_is_valid(driver->vkctx))
  {
    return false;
  }

  /* iris_driver_is_valid is called by the functions to initialize these, we cannot check these */
  // if (driver->small_transfer_buffer.buffer == VK_NULL_HANDLE || driver->large_transfer_buffer.buffer == VK_NULL_HANDLE)
  // {
  //   return false;
  // }

  return true;
}

void
iris_begin_upload_batch(iris_driver_t* driver)
{
  if (driver->active_upload_cmd)
  {
    return;
  }
  driver->active_upload_cmd = nv_vk_begin_command_buffer(driver);
}

void
iris_end_upload_batch(iris_driver_t* driver)
{
  if (!driver->active_upload_cmd)
  {
    nv_log_error("An upload batch has not been started\n");
    return;
  }
  nv_vk_end_command_buffer(driver, driver->active_upload_cmd, driver->vkctx->graphics_queue, true);
  driver->active_upload_cmd = VK_NULL_HANDLE;
}

bool
iris_is_upload_batch_active(const iris_driver_t* driver)
{
  return driver->active_upload_cmd != VK_NULL_HANDLE;
}
