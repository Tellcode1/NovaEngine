
#ifndef NOVA_DESCRIPTORS_H
#define NOVA_DESCRIPTORS_H

#include "../../external/volk/volk.h"
#include "../std/include/types.h"
#include "types.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif
  struct iris_driver;
  // TODO: WHERET EH FOOF IS DOCUMENTATION

  // WARNING: Currently only supports the first 11 descriptor types.

  typedef struct nv_descriptor_set       nv_descriptor_set_t;
  typedef struct nv_descriptor_pool_size nv_descriptor_pool_size;
  typedef struct nv_descriptor_pool      nv_descriptor_pool_t;

  struct nv_descriptor_pool_size
  {
    uint32_t type;
    size_t   capacity;
    size_t   numchilds; // how many are being used
  };

  struct nv_descriptor_pool
  {
    VkDescriptorPool        pool;
    size_t                  max_child_sets;
    nv_descriptor_pool_size descriptors[11];
    nv_descriptor_set_t**   sets;
    size_t                  nsets;
  };

  struct nv_descriptor_set
  {
    u32                          canary;
    struct iris_driver*          driver;
    VkDescriptorSetLayout        layout;
    VkDescriptorSet              set;
    nv_descriptor_pool_t*        pool;
    struct VkWriteDescriptorSet* writes;
    size_t                       nwrites;
  };

  extern int
  nv_allocate_descriptor_set(struct iris_driver* driver, nv_descriptor_pool_t* pool, const VkDescriptorSetLayoutBinding* bindings, int nbindings, nv_descriptor_set_t** dst);
  extern void nv_descriptor_set_destroy(nv_descriptor_set_t* set);

  extern int nv_descriptor_set_submit_write(nvvk_ctx_t* vkctx, nv_descriptor_set_t* set, const VkWriteDescriptorSet* write);

  extern void nv_descriptor_pool_destroy(nvvk_ctx_t* vkctx, nv_descriptor_pool_t* pool);
  extern int  _nv_descriptor_pool_allocate(nvvk_ctx_t* vkctx, nv_descriptor_pool_t* pool);
  extern int  nv_descriptor_pool_init(nvvk_ctx_t* vkctx, nv_descriptor_pool_t* dst);

#ifdef __cplusplus
}
#endif

#endif // NOVA_DESCRIPTORS_H
