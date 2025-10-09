
#ifndef IRIS_SAMPLER_H
#define IRIS_SAMPLER_H

#include "../../external/volk/volk.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

  struct iris_driver;
  typedef struct iris_sampler_internal iris_sampler_internal_t;
  typedef struct iris_sampler          iris_sampler_t;

  typedef enum iris_filter
  {
    NV_FILTER_NEAREST = 0,
    NV_FILTER_LINEAR  = 1,
  } iris_filter;

  /* What to do when the texture coordinates read past the texture (> 1.0F) */
  typedef enum iris_sampler_wrap_mode
  {
    /* Modulus the texture coordinates by 1 so it endlessly repeats */
    IRIS_SAMPLER_WRAP_MODE_REPEAT,

    /* Repeat, but the out of bounds reads are mirrored (laterally inverted) */
    IRIS_SAMPLER_MODE_MIRRORED_REPEAT,

    /* Take the colors of the pixels on the EDGES of the texture */
    IRIS_SAMPLER_MODE_CLAMP_TO_EDGE,
    IRIS_SAMPLER_MODE_CLAMP_TO_BORDER,
  } iris_sampler_wrap_mode;

  typedef enum iris_sampler_compare_mode
  {
    IRIS_SAMPLER_COMPARE_MODE_NONE     = 1,
    IRIS_SAMPLER_COMPARE_MODE_LESS     = 2,
    IRIS_SAMPLER_COMPARE_MODE_LEQUAL   = 3,
    IRIS_SAMPLER_COMPARE_MODE_EQUAL    = 4,
    IRIS_SAMPLER_COMPARE_MODE_GEQUAL   = 5,
    IRIS_SAMPLER_COMPARE_MODE_GREATER  = 6,
    IRIS_SAMPLER_COMPARE_MODE_NOTEQUAL = 7,
    IRIS_SAMPLER_COMPARE_MODE_ALWAYS   = 8,
    IRIS_SAMPLER_COMPARE_MODE_NEVER    = 9
  } iris_sampler_compare_mode;

  typedef struct iris_sampler_create_info
  {
    iris_filter               min_filter;
    iris_filter               mag_filter;
    iris_sampler_wrap_mode    wrapu;
    iris_sampler_wrap_mode    wrapv;
    iris_sampler_wrap_mode    wrapw;
    double                    anisotropy;
    iris_sampler_compare_mode compare_mode;
    double                    min_lod;
    double                    max_lod;
  } iris_sampler_create_info;

  struct iris_sampler_internal
  {
    iris_filter               min_filter;
    iris_filter               mag_filter;
    iris_sampler_wrap_mode    wrapu;
    iris_sampler_wrap_mode    wrapv;
    iris_sampler_wrap_mode    wrapw;
    double                    anisotropy;
    iris_sampler_compare_mode compare_mode;

    double min_lod;
    double max_lod;

    VkSampler handle;

    /**
     * The reference count
     * This shouldn't be used by the user
     * It's tracked by the driver and when it hits 0
     * , the sampler (the vulkan one) is destroyed.
     */
    size_t rcount;
  };

  struct iris_sampler
  {
    iris_sampler_internal_t* ptr;
  };

  extern void iris_create_sampler(struct iris_driver* driver, const iris_sampler_create_info* pInfo, iris_sampler_t* dst);
  extern void iris_destroy_sampler_immediate(struct iris_driver* driver, iris_sampler_t* sampler);

  extern VkSampler iris_sampler_get(const iris_sampler_t* sampler);

  extern VkCompareOp iris_sampler_compare_mode_to_vk_op(iris_sampler_compare_mode mode);

#ifdef __cplusplus
}
#endif

#endif // IRIS_SAMPLER_H
