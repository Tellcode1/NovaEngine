#include "../../include/iris/descriptors.h"
#include "../../external/volk/volk.h"
#include "../../include/iris/pipeline.h"
#include "../../include/iris/types.h"
#include "../../include/std/include/error.h"
#include "../../include/std/include/stdafx.h"
#include "../../include/std/include/string.h"
#include "../../include/std/include/types.h"
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <stddef.h>
#include <stdint.h>
#include <vulkan/vk_platform.h>

int
nv_descriptor_set_submit_write(nvvk_ctx_t* vkctx, nv_descriptor_set_t* set, const VkWriteDescriptorSet* write)
{
  nv_assert_else_return(vkctx != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(write != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(set != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(set->writes != NULL, NV_ERROR_BROKEN_STATE);

  set->writes = (VkWriteDescriptorSet*)nv_realloc(set->writes, (set->nwrites + 1) * sizeof(VkWriteDescriptorSet));
  nv_assert_else_return(set->writes != NULL, NV_ERROR_MALLOC_FAILED);

  if (!nv_memcpy(&set->writes[set->nwrites], write, sizeof(VkWriteDescriptorSet)))
  {
    return NV_ERROR_EXTERNAL;
  }
  set->nwrites++;
  vkUpdateDescriptorSets(vkctx->device, 1, write, 0, 0);
  return 0;
}

void
nv_descriptor_set_destroy(nv_descriptor_set_t* set)
{
  nvvk_ctx_t* vkctx = set->driver->vkctx;
  vkDestroyDescriptorSetLayout(vkctx->device, set->layout, &vkctx->vkalloc);
  nv_free(set->writes);
  nv_free(set);
}

void
nv_descriptor_pool_destroy(nvvk_ctx_t* vkctx, nv_descriptor_pool_t* pool)
{
  for (size_t i = 0; i < pool->nsets; i++)
  {
    nv_descriptor_set_destroy(pool->sets[i]);
  }
  nv_free((void*)pool->sets);
  vkDestroyDescriptorPool(vkctx->device, pool->pool, &vkctx->vkalloc);
}

static inline int
nv_descriptor_pool_allocate(nvvk_ctx_t* vkctx, nv_descriptor_pool_t* pool)
{
  nv_assert_else_return(pool != NULL, NV_ERROR_INVALID_ARG);

  VkDescriptorPoolSize allocations[11]     = { 0 };
  size_t               descriptors_written = 0;

  for (size_t i = 0; i < 11; i++)
  {
    if (pool->descriptors[i].capacity == 0)
    {
      continue;
    }
    allocations[descriptors_written] = (VkDescriptorPoolSize){ (VkDescriptorType)pool->descriptors[i].type, (uint32_t)pool->descriptors[i].capacity };
    descriptors_written++;
  }

  if (descriptors_written == 0)
  {
    return 0;
  }

  bool const need_more_max_sets = (pool->nsets + 1) > pool->max_child_sets;
  if (need_more_max_sets)
  {
    pool->max_child_sets = NV_MAX(pool->max_child_sets * 2, 1);
  }

  VkDescriptorPoolCreateInfo const poolInfo = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = (u32)pool->max_child_sets, .poolSizeCount = (u32)descriptors_written, .pPoolSizes = allocations
  };

  VkDescriptorPool new_pool;
  nvvk_result_check(*vkctx, vkCreateDescriptorPool(vkctx->device, &poolInfo, &vkctx->vkalloc, &new_pool));
  if (new_pool == NULL)
  {
    return -1;
  }

  VkDescriptorSet* new_sets = (VkDescriptorSet*)nv_zmalloc(sizeof(VkDescriptorSet) * NV_MAX(pool->nsets, 1));
  nv_assert_else_return(new_sets != NULL, NV_ERROR_MALLOC_FAILED);

  if (pool->nsets > 0)
  {
    VkDescriptorSetLayout* layouts = (VkDescriptorSetLayout*)nv_zmalloc(sizeof(VkDescriptorSetLayout) * pool->nsets);
    nv_assert_else_return(layouts != NULL, NV_ERROR_MALLOC_FAILED);

    for (size_t i = 0; i < pool->nsets; i++)
    {
      if (pool->sets[i]->layout == VK_NULL_HANDLE)
      {
        i = NV_MAX(i - 1, 0);
        continue;
      }
      layouts[i] = pool->sets[i]->layout;
    }

    VkDescriptorSetAllocateInfo setAllocInfo = nv_zinit(VkDescriptorSetAllocateInfo);
    setAllocInfo.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocInfo.descriptorPool              = new_pool;
    setAllocInfo.descriptorSetCount          = pool->nsets;
    setAllocInfo.pSetLayouts                 = layouts;
    nvvk_result_check(*vkctx, vkAllocateDescriptorSets(vkctx->device, &setAllocInfo, new_sets));
    nv_assert_else_return(new_sets != NULL, NV_ERROR_INVALID_RETVAL);

    nv_free((void*)layouts);
  }

  size_t ncopies = 0;
  for (size_t i = 0; i < pool->nsets; i++)
  {
    ncopies += pool->sets[i]->nwrites;
  }
  VkCopyDescriptorSet* copies = (VkCopyDescriptorSet*)nv_zmalloc(sizeof(VkCopyDescriptorSet) * NV_MAX(ncopies, 1));
  nv_assert_else_return(copies != NULL, NV_ERROR_MALLOC_FAILED);

  nv_assert_else_return(pool->sets != NULL, -1);

  ncopies = 0;
  for (size_t i = 0; i < pool->nsets; i++)
  {
    nv_descriptor_set_t* old_set = pool->sets[i];

    for (size_t writei = 0; writei < old_set->nwrites; writei++)
    {
      if (new_sets[i] == VK_NULL_HANDLE)
      {
        continue;
      }

      VkWriteDescriptorSet* write = &old_set->writes[writei];
      copies[ncopies]             = (VkCopyDescriptorSet){
                    .sType           = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET,
                    .srcSet          = old_set->set,
                    .srcBinding      = write->dstBinding,
                    .srcArrayElement = write->dstArrayElement,
                    .dstSet          = new_sets[i],
                    .dstBinding      = write->dstBinding,
                    .dstArrayElement = write->dstArrayElement,
                    .descriptorCount = write->descriptorCount,
      };
      ncopies++;
    }
    old_set->set = new_sets[i];
  }
  vkUpdateDescriptorSets(vkctx->device, 0, NULL, ncopies, copies);

  if (pool->pool != NULL)
  {
    vkDestroyDescriptorPool(vkctx->device, pool->pool, &vkctx->vkalloc);
  }
  pool->pool = new_pool;

  if (new_sets != NULL)
  {
    nv_free((void*)new_sets);
  }
  if (copies != NULL)
  {
    nv_free(copies);
  }
  return 0;
}

int
nv_descriptor_pool_init(nvvk_ctx_t* vkctx, nv_descriptor_pool_t* dst)
{
  *dst                                 = nv_zinit(nv_descriptor_pool_t);
  nv_descriptor_pool_size pool_sizes[] = {
    { VK_DESCRIPTOR_TYPE_SAMPLER, 0, 0 },
    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, 0 },
    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 0, 0 },
    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0, 0 },
    { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 0, 0 },
    { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 0, 0 },
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, 0 },
    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 0, 0 },
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 0, 0 },
    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 0, 0 },
    { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 0, 0 },
  };
  nv_memcpy(dst->descriptors, pool_sizes, sizeof(pool_sizes));
  dst->sets           = (nv_descriptor_set_t**)nv_zmalloc(sizeof(nv_descriptor_set_t*));
  dst->max_child_sets = 1;
  dst->nsets          = 0;
  if (nv_descriptor_pool_allocate(vkctx, dst) != 0)
  {
    return -1;
  }
  return 0;
}

int
nv_allocate_descriptor_set(iris_driver_t* driver, nv_descriptor_pool_t* pool, const VkDescriptorSetLayoutBinding* bindings, int nbindings, nv_descriptor_set_t** dst)
{
  nv_assert_else_return(pool != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(bindings != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nbindings > 0, NV_ERROR_INVALID_ARG);

  nvvk_ctx_t* vkctx = driver->vkctx;

  bool need_realloc = false;
  for (size_t i = 0; i < 11; i++)
  {
    for (int j = 0; j < nbindings; j++)
    {
      nv_descriptor_pool_size* descriptor = &pool->descriptors[i];
      if (descriptor->type == bindings[j].descriptorType)
      {
        descriptor->capacity = NV_MAX(descriptor->capacity * 2, (int)bindings[j].descriptorCount + descriptor->capacity);
        need_realloc         = true;
      }
    }
  }

  if (need_realloc || ((pool->nsets + 1) > pool->max_child_sets))
  {
    if ((pool->nsets + 1) > pool->max_child_sets)
    {
      pool->max_child_sets = NV_MAX(pool->max_child_sets * 2, 1);
      pool->sets           = (nv_descriptor_set_t**)nv_realloc((void*)pool->sets, pool->max_child_sets * sizeof(nv_descriptor_set_t));
      nv_assert_else_return(pool->sets != NULL, NV_ERROR_MALLOC_FAILED);
    }

    nv_descriptor_pool_allocate(driver->vkctx, pool);
  }

  nv_descriptor_set_t* set = (nv_descriptor_set_t*)nv_zmalloc(sizeof(nv_descriptor_set_t));
  nv_assert_else_return(set != NULL, NV_ERROR_MALLOC_FAILED);

  set->driver             = driver;
  pool->sets[pool->nsets] = set;
  pool->nsets++;

  (*dst)         = set;
  (*dst)->driver = driver;

  set->pool   = pool;
  set->writes = (VkWriteDescriptorSet*)nv_zmalloc(sizeof(VkWriteDescriptorSet));
  nv_assert_else_return(set->writes != NULL, NV_ERROR_MALLOC_FAILED);

  VkDescriptorSetLayoutCreateInfo layoutinfo = nv_zinit(VkDescriptorSetLayoutCreateInfo);
  layoutinfo.sType                           = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutinfo.pBindings                       = bindings;
  layoutinfo.bindingCount                    = nbindings;
  nvvk_result_check(*vkctx, vkCreateDescriptorSetLayout(vkctx->device, &layoutinfo, &vkctx->vkalloc, &set->layout));
  nv_assert_else_return(set->layout != VK_NULL_HANDLE, NV_ERROR_EXTERNAL);

  VkDescriptorSetAllocateInfo setAllocInfo = nv_zinit(VkDescriptorSetAllocateInfo);
  setAllocInfo.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  setAllocInfo.descriptorPool              = pool->pool;
  setAllocInfo.descriptorSetCount          = 1;
  setAllocInfo.pSetLayouts                 = &set->layout;
  nvvk_result_check(*vkctx, vkAllocateDescriptorSets(vkctx->device, &setAllocInfo, &set->set));
  if (set->set == VK_NULL_HANDLE)
  {
    return -1;
  }

  return 0;
}