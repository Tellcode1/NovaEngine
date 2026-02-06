#ifndef NOVA_VK_DRIVER_H
#define NOVA_VK_DRIVER_H

#include "../std/include/containers/list.h"
#include "../std/include/error.h"
#include "../std/include/types.h"
#include "buffer.h"
#include "descriptors.h"
#include "extent.h"
#include "memory.h"
#include "texture.h"
#include "types.h"

#ifdef __cplusplus
extern "C"
{
#endif

  /**
   * Nova uses two buffers for most transfers, a small one and a large one. The small one has a constant size but the large buffer will resize
   * to transfer larger amounts of data
   */
#ifndef IRIS_SMALL_TRANSFER_BUFFER_SIZE
#  define IRIS_SMALL_TRANSFER_BUFFER_SIZE 256
#endif

#ifndef IRIS_LARGE_TRANSFER_BUFFER_INITIAL_SIZE
#  define IRIS_LARGE_TRANSFER_BUFFER_INITIAL_SIZE 4096
#endif

#ifndef IRIS_SMALL_TRANSFER_BUFFER_CREATE_FLAGS
#  define IRIS_SMALL_TRANSFER_BUFFER_CREATE_FLAGS (IRIS_BUFFER_FLAGS_TRANSFER_ONLY_BIT)
#endif

#ifndef IRIS_LARGE_TRANSFER_BUFFER_CREATE_FLAGS
#  define IRIS_LARGE_TRANSFER_BUFFER_CREATE_FLAGS IRIS_SMALL_TRANSFER_BUFFER_CREATE_FLAGS
#endif

#ifndef IRIS_SMALL_TRANSFER_BUFFER_ALIGNMENT
#  define IRIS_SMALL_TRANSFER_BUFFER_ALIGNMENT 8
#endif

#ifndef IRIS_LARGE_TRANSFER_BUFFER_ALIGNMENT
#  define IRIS_LARGE_TRANSFER_BUFFER_ALIGNMENT 16
#endif

  /**
   * A driver for nearly all abstractions of the NVVK API.
   * This driver is responsible for everything to creation to destruction of resources and aims
   * to unify all the state to a single place. A context *may* havee more than one driver but that
   * typically should never be used.
   * The driver also serves to seperate the vulkan logic from the renderer to allow optimization
   * but still be seperated from the renderer
   */
  typedef struct iris_driver iris_driver_t;

  struct iris_driver
  {
    u32 canary; // = 0xDEADBEEF

    /**
     * TODO: Is there any way to know if ctx has been destroyed and to kys if needed?
     */
    nvvk_ctx_t* vkctx;

    /**
     * Pointers to all the samplers.
     * As samplers are very reusable, we don't create many.
     * Newly created samplers are stored here and creating ones which already exist
     * return an opaque pointer to an entry in this list.
     * WARNING: The type stored in this is iris_sampler_internal_t, not the one you'd expect!
     */
    nv_list_t samplers;

    /* constant size */
    iris_buffer_t small_transfer_buffer;

    /* resized when needed */
    iris_buffer_t large_transfer_buffer;

    iris_memory_pool_t cpu_mappable_pool;
    iris_memory_pool_t gpu_local_pool;

    VkCommandBuffer active_upload_cmd;
  };

  extern nv_error iris_driver_init(nvvk_ctx_t* ctx, iris_driver_t* dst);
  extern void     iris_driver_destroy(iris_driver_t* driver);

  /**
   * WARNING: Even if the underlying nvvk_ctx_t is invalid, this function will return false.
   */
  extern bool iris_driver_is_valid(const iris_driver_t* driver);

  /**
   * Begin an upload batch. A batch may have already been started, it will just continue.
   * After calling, All write_data() functions will record their commands
   * into a pool which is submit to the GPU at end_upload_batch() time.
   */
  extern void iris_begin_upload_batch(iris_driver_t* driver);
  extern void iris_end_upload_batch(iris_driver_t* driver);
  extern bool iris_is_upload_batch_active(const iris_driver_t* driver);

#ifdef __cplusplus
}
#endif

#endif // NOVA_VK_DRIVER_H