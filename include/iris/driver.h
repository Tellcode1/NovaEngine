#ifndef NOVA_VK_DRIVER_H
#define NOVA_VK_DRIVER_H

#include "../shadersystem/nvsm.h"
#include "../std/include/containers/list.h"
#include "../std/include/errorcodes.h"
#include "../std/include/types.h"
#include "buffer.h"
#include "descriptors.h"
#include "destructqueue.h"
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
     * Anything to be destroyed is appended to this queue.
     * It may be pooled and reused accordingly.
     */
    iris_destruct_queue_t destruct_queue;

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
   * Begin an upload batch. A batch must not have already been started.
   * After calling, All write_data() functions will record their commands
   * into a pool which is submit to the GPU at end_upload_batch() time.
   */
  extern void iris_begin_upload_batch(iris_driver_t* driver);
  extern void iris_end_upload_batch(iris_driver_t* driver);
  extern bool iris_is_upload_batch_active(const iris_driver_t* driver);

  /**
   * Queue a resource for destruction.
   * The resource may be pooled and given back to some other user.
   */
  extern void iris_queue_for_destruction(iris_driver_t* driver, iris_resource_t rsrc);

  /**
   * Queue a buffer for destruction.
   */
  static inline void
  iris_buffer_destroy(iris_buffer_t* buffer)
  {
    nv_assert_else_return(buffer != NULL, );
    nv_assert_else_return(buffer->handle != VK_NULL_HANDLE, );
    iris_queue_for_destruction(buffer->driver, (iris_resource_t){ .type = IRIS_RESOURCE_BUFFER, .handle.buffer = buffer->handle });
  }

  /**
   * Queue a texture for destruction.
   */
  static inline void
  iris_texture_destroy(iris_texture_t* texture)
  {
    nv_assert_else_return(texture != NULL, );
    nv_assert_else_return(texture->handle != VK_NULL_HANDLE, );
    iris_queue_for_destruction(texture->driver, (iris_resource_t){ .type = IRIS_RESOURCE_TEXTURE, .handle.texture = texture->handle });
  }

  /**
   * Queue a shader for destruction.
   */
  static inline void
  iris_shader_destroy(nvsm_shader_t* shader)
  {
    nv_assert_else_return(shader != NULL, );
    nv_assert_else_return(shader->handle != VK_NULL_HANDLE, );
    // iris_queue_for_destruction(shader->driver, (iris_resource_t){ .type = IRIS_RESOURCE_SHADER, .handle.shader = shader });
  }

  /**
   * Queue memory for freeing.
   */
  static inline void
  iris_memory_free(iris_memory_t* memory)
  {
    nv_assert_else_return(memory != NULL, );
    if (memory->memory_flags & IRIS_MEMORY_FLAGS_DEDICATED_BIT)
    {
      iris_queue_for_destruction(memory->driver, (iris_resource_t){ .type = IRIS_RESOURCE_MEMORY, .handle.memory = memory->dedicated_allocation });
    }
    else
    {
      iris_memory_free_immediate(memory);
    }
  }

  /**
   * Destroy everything in the queue.
   */
  extern void iris_queue_flush(iris_driver_t* driver);

  /**
   * Destroy an resource in the queue.
   */
  extern void iris_queue_destroy_entry(iris_driver_t* driver, size_t i);

#ifdef __cplusplus
}
#endif

#endif // NOVA_VK_DRIVER_H