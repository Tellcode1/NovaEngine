#ifndef __NOVA_DESCRIPTORS_H__
#define __NOVA_DESCRIPTORS_H__

// implementation: vk.c

#include "../std/stdafx.h"
#include "vk.hpp"

// WARNING: Currently only supports the first 11 descriptor types.

typedef struct nv_descriptor_set_t     nv_descriptor_set_t;
typedef struct nv_descriptor_pool_size nv_descriptor_pool_size;
typedef struct nv_descriptor_pool_t    nv_descriptor_pool_t;

struct nv_descriptor_pool_size
{
  uint32_t type;
  size_t   capacity;
  size_t   numchilds; // how many are being used
};

struct nv_descriptor_pool_t
{
  VkDescriptorPool        pool;
  size_t                  max_child_sets;
  nv_descriptor_pool_size descriptors[11];
  nv_descriptor_set_t**   sets;
  size_t                  nsets;
};

struct nv_descriptor_set_t
{
  u32                          canary;
  VkDescriptorSetLayout        layout;
  VkDescriptorSet              set;
  nv_descriptor_pool_t*        pool;
  struct VkWriteDescriptorSet* writes;
  size_t                       nwrites;
};

extern int  nv_descriptor_set_submit_write(nvvk_ctx_t* vkctx, nv_descriptor_set_t* set, const VkWriteDescriptorSet* write);
extern void nv_descriptor_set_destroy(nvvk_ctx_t* vkctx, nv_descriptor_set_t* set);
extern void nv_descriptor_pool_destroy(nvvk_ctx_t* vkctx, nv_descriptor_pool_t* pool);
extern int  _nv_descriptor_pool_allocate(nvvk_ctx_t* vkctx, nv_descriptor_pool_t* pool);
extern int  nv_descriptor_pool_init(nvvk_ctx_t* vkctx, nv_descriptor_pool_t* dst);
extern int  nv_allocate_descriptor_set(nvvk_ctx_t* vkctx, nv_descriptor_pool_t* pool, const VkDescriptorSetLayoutBinding* bindings, int nbindings, nv_descriptor_set_t** dst);

#endif //__NOVA_DESCRIPTORS_H__
