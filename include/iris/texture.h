
#ifndef NOVA_TEXTURE_H
#define NOVA_TEXTURE_H

#include "../../external/volk/volk.h"
#include "../engine/format.h"
#include "../std/include/math/math.h"
#include "../std/include/math/vec3.h"
#include "buffer.h"
#include "memory.h"
#include "types.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  struct iris_driver;

  typedef struct iris_texture     iris_texture_t;
  typedef struct iris_image_range iris_image_range_t;
  struct nv_renderer;

#define IRIS_TEXTURE_NO_ARRAY_LAYERS (1)

  typedef u32 iris_texture_flags;
  typedef enum iris_texture_flags_bits
  {
    /* shader sampled */
    IRIS_TEXTURE_SAMPLED_BIT = 1 << 0,

    /* usable as color attachment */
    IRIS_TEXTURE_COLOR_ATTACHMENT_BIT = 1 << 1,

    /* usable as multisampling resolve attachment */
    IRIS_TEXTURE_RESOLVE_ATTACHMENT_BIT = 1 << 2,

    /* usable as depth/stencil */
    IRIS_TEXTURE_DEPTH_ATTACHMENT_BIT = 1 << 3,

    /* storage image for compute or whatever */
    IRIS_TEXTURE_STORAGE_BIT = 1 << 5,

    /* cube/array */
    IRIS_TEXTURE_CUBEMAP_BIT = 1 << 6,

    /* request to generate mips */
    IRIS_TEXTURE_GENERATE_MIPMAPS_BIT = 1 << 7,
  } iris_texture_flags_bits;

  typedef struct iris_texture_region
  {
    vec3i      offset;
    nv_extent3 extent;
    u32        mip_level;
    u32        array_level;
  } iris_texture_region_t;

  typedef struct iris_texture_extra_create_info
  {
    iris_memory_flags   custom_memory_flags;
    iris_memory_pool_t* custom_memory_pool;

    VkImageUsageFlags passthrough_usage_flags; /* passthrough if you need custom usage bits. OR'd with calculated image usage bits */
    VkImageLayout     initial_layout;

    bool immutable; /* image created once, NO RESIZES TOO */

    VkImage render_texture_image;
  } iris_texture_extra_create_info_t;

  typedef struct iris_texture_create_info
  {
    /**
     * If extent.height = 0, extent.depth = 0, Image is considered 1D
     * If extent.depth = 0, Image is considered 2D
     * Else, image is considered 3D
     */
    nv_extent3 extent; // < width, height, depth > depth = 1 for 2D image
    size_t     alignment;

    /**
     * The size of the array of images. Each image will have the same width and height, but can have different data
     * use IRIS_TEXTURE_NO_ARRAY_LAYERS if image is not an array
     */
    u32 array_layers;

    nv_format       format;
    nv_sample_count samples;

    iris_texture_flags flags;

    // Image is stored linearly, i.e row by row, As you and the CPU would expect
    // But otherwise, for optimal tiling, the image is stored how the GPU likes it to be
    // which is MUCH faster but the CPU doesn't know how to interpret that
    bool linear_tiling;

    // can be NULL
    iris_texture_extra_create_info_t* extra;
  } iris_texture_create_info_t;

  struct iris_texture
  {
    struct iris_driver* driver;
    u64                 user_data;

    iris_texture_flags flags;

    /* description */
    nv_extent3 extent;
    u32        mip_levels;
    u32        array_layers;

    nv_format     format;
    VkImageLayout current_layout;

    VkImage     handle;
    VkImageView handle_view;

    iris_memory_t memory;

    /* driver fields */
    bool drv_destroyed;
    bool drv_in_use;
  };

  /**
   * @brief Create a texture to use for rendering on the GPU
   * Data must be written to the texture first.
   */
  extern nv_error iris_texture_init(struct iris_driver* driver, const iris_texture_create_info_t* info, iris_texture_t* out_texture);

  extern void iris_texture_destroy(iris_texture_t* tex);

  /**
   * @brief Resize an image to new dimensions.
   * @param copy_old If true, all data from old texture is copied
   */
  extern nv_error iris_texture_resize(iris_texture_t* tex, nv_extent3 new_extent, bool copy_old);

  /**
   * Write CPU data into image (single-subresource or whole image). Internally may use staging.
   * pixels format must match image format.
   * @todo Make a system to change formats when needed.
   */
  extern nv_error iris_texture_write_data(iris_texture_t* tex, const iris_texture_region_t* dst_region, const void* pixels, size_t data_size) NOVA_ATTR_NONNULL(1, 2, 3);

  extern nv_error iris_texture_copy(iris_texture_t* src, iris_texture_t* dst);

  extern nv_error iris_texture_copy_from_buffer(iris_texture_t* dst_texture, const iris_texture_region_t* dst_region, const iris_buffer_t* src_buffer, u32 src_buffer_offset);

  /**
   * Readback pixels to CPU buffer (will block by default).
   * @todo Figure out ways for async this?
   */
  extern nv_error iris_texture_readback(
      iris_texture_t* tex, void* dst, size_t dst_size, u32 mip_level, u32 array_layer, VkImageLayout current_layout); /* use to know if layout transition is necessary */

  extern nv_error iris_texture_generate_mipmaps(iris_texture_t* tex, u32 levels);

  extern VkImageView iris_texture_get_image_view(const iris_texture_t* tex);
  extern VkImage     iris_texture_get_image(const iris_texture_t* tex);
  extern nv_format   iris_texture_get_format(const iris_texture_t* tex);

  /* Layout barrier helper: insert an image memory barrier into command buffer for layout transition */
  extern void nv_vk_transition_image_auto(VkCommandBuffer cmd, iris_texture_t* tex, VkImageLayout newLayout, uint32_t mip_level, uint32_t array_level);

  /**
   * @brief Get our guess as to what VK Image flags we need
   */
  extern VkImageUsageFlags iris_texture_flags_to_vk_flags(iris_texture_flags flags);

#ifdef __cplusplus
}
#endif

#endif // NOVA_TEXTURE_H
