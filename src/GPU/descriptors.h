#ifndef __NOVA_DESCRIPTORS_H__
#define __NOVA_DESCRIPTORS_H__

// implementation: vk.c

#include "../../external/volk/volk.h"
#include "../std/math/math.h"
#include "../std/stdafx.h"
#include "vkstdafx.h"

NOVA_HEADER_START

// WARNING: Currently only supports the first 11 descriptor types.

typedef struct nv_descriptor_set_t     nv_descriptor_set_t;
typedef struct nv_descriptor_pool_size nv_descriptor_pool_size;
typedef struct nv_descriptor_pool_t    nv_descriptor_pool_t;

struct nv_descriptor_pool_size
{
  uint32_t m_type;
  int      m_capacity;
  int      m_numchilds; // how many are being used
};

struct nv_descriptor_pool_t
{
  VkDescriptorPool        m_pool;
  int                     m_max_child_sets;
  nv_descriptor_pool_size m_descriptors[11];
  nv_descriptor_set_t**   m_sets;
  int                     m_nsets;
};

struct nv_descriptor_set_t
{
  int                          m_canary;
  VkDescriptorSetLayout        m_layout;
  VkDescriptorSet              m_set;
  nv_descriptor_pool_t*        m_pool;
  struct VkWriteDescriptorSet* m_writes;
  int                          m_nwrites;
};

extern int  nv_descriptor_set_submit_write(nv_descriptor_set_t* set, const VkWriteDescriptorSet* write);
extern void nv_descriptor_set_destroy(nv_descriptor_set_t* set);
extern void nv_descriptor_pool_destroy(nv_descriptor_pool_t* pool);
extern int  _nv_descriptor_pool_allocate(nv_descriptor_pool_t* pool);
extern int  nv_descriptor_pool_init(nv_descriptor_pool_t* dst);
extern int  nv_allocate_descriptor_set(nv_descriptor_pool_t* pool, const VkDescriptorSetLayoutBinding* bindings, int nbindings, nv_descriptor_set_t** dst);

NOVA_HEADER_END

#endif //__NOVA_DESCRIPTORS_H__
