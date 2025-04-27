#ifndef __NOVA_VK_DRIVER_H__
#define __NOVA_VK_DRIVER_H__

#include "../std/containers/list.h"
#include "../std/stdafx.h"
#include "allocator.h"
#include "buffer.h"
#include "types.h"

NOVA_HEADER_START

/**
 * A driver for nearly all abstractions of the NVVK API.
 * This driver is responsible for everything to creation to destruction of resources and aims
 * to unify all the state to a single place. A context *may* havee more than one driver but that
 * typically should never be used.
 * The driver also serves to seperate the vulkan logic from the renderer to allow optimization
 * but still be seperated from the renderer
 */
typedef struct nvvk_driver_t nvvk_driver_t;

struct nvvk_driver_t
{
  u32 canary; // = 0xDEADBEEF

  /**
   * TODO: Is there any way to know if ctx has been destroyed and to kys if needed?
   */
  nvvk_ctx_t* ctx;

  /**
   * Pointers to the all the dynamic buffers.
   * This is used for sampling the read and write times of the buffer, and much more stuff.
   * Nearly all optimization of buffers is handled by this here driver.
   */
  nv_list_t buffers;

  /**
   * Pointers to all the samplers.
   * As samplers are very reusable, we don't create many.
   * Newly created samplers are stored here and creating ones which already exist
   * return an opaque pointer to an entry in this list.
   */
  nv_list_t samplers;

  /* Both buffers below are transient */

  /* constant size */
  nv_gpu_buffer_t small_transfer_buffer;

  /* resized when needed */
  nv_gpu_buffer_t large_transfer_buffer;

  /* A list of temporary buffers, used for transfers (and possibly other stuff ) */
  nv_list_t tmp_buffers;

  nv_gpu_memory_pool_t pool;
};

extern nv_error nvvk_driver_init(nvvk_ctx_t* ctx, nvvk_driver_t* dst);
extern void     nvvk_driver_destroy(nvvk_driver_t* driver);

/**
 * WARNING: Even if the underlying nvvk_ctx_t is invalid, this function will return false.
 */
extern bool nvvk_driver_is_valid(const nvvk_driver_t* driver);

NOVA_HEADER_END

#endif //__NOVA_VK_DRIVER_H__