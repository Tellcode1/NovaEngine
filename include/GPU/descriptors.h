#ifndef __NOVA_DESCRIPTORS_H__
#define __NOVA_DESCRIPTORS_H__

// implementation: vk.c

#include "../../external/volk/volk.h"
#include "../../std/math/math.h"
#include "../../std/stdafx.h"
#include "vkstdafx.h"

NOVA_HEADER_START;

// WARNING: Currently only supports the first 11 descriptor types.

typedef struct nv_descriptor_set_t     nv_descriptor_set_t;
typedef struct nv_descriptor_pool_size nv_descriptor_pool_size;
typedef struct nv_descriptor_pool_t    nv_descriptor_pool_t;
typedef struct nv_descriptor_set_t     nv_descriptor_set_t;

struct nv_descriptor_pool_size
{
  uint32_t type;
  int      capacity;
  int      numchilds; // how many are being used
};

struct nv_descriptor_pool_t
{
  VkDescriptorPool        pool;
  int                     max_child_sets;
  nv_descriptor_pool_size descriptors[11];
  nv_descriptor_set_t**   sets;
  int                     nsets;
};

struct nv_descriptor_set_t
{
  int                          canary;
  VkDescriptorSetLayout        layout;
  VkDescriptorSet              set;
  nv_descriptor_pool_t*        pool;
  struct VkWriteDescriptorSet* writes;
  int                          nwrites;
};

extern void nv_descriptor_set_submit_write(nv_descriptor_set_t* set, const VkWriteDescriptorSet* write);
extern void nv_descriptor_set_destroy(nv_descriptor_set_t* set);
extern void nv_descriptor_pool_destroy(nv_descriptor_pool_t* pool);
extern void _nv_descriptor_pool_allocate(nv_descriptor_pool_t* pool);
extern void nv_descriptor_pool_init(nv_descriptor_pool_t* dst);
extern void nv_allocate_descriptor_set(nv_descriptor_pool_t* pool, const VkDescriptorSetLayoutBinding* bindings, int nbindings, nv_descriptor_set_t** dst);

NOVA_HEADER_END;

#endif //__NOVA_DESCRIPTORS_H__