#include "../../include/iris/memory.h"
#include "../../external/volk/volk.h"
#include "../../include/iris/buffer.h"
#include "../../include/iris/driver.h"
#include "../../include/iris/pipeline.h"
#include "../../include/iris/sampler.h"
#include "../../include/iris/types.h"
#include "../../include/iris/utils.h"
#include "../../include/iris/vkstdafx.h"
#include "../../include/std/include/error.h"
#include "../../include/std/include/stdafx.h"
#include "../../include/std/include/string.h"
#include "../../include/std/include/types.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

VkDeviceMemory
iris_memory_get_backing(const iris_memory_t* memory)
{
  if ((memory->memory_flags & IRIS_MEMORY_FLAGS_DEDICATED_BIT) != 0u)
  {
    return memory->dedicated_allocation;
  }
  return memory->pool->memory_handle;
}

nv_error
iris_memory_pool_init(iris_driver_t* driver, const iris_memory_pool_create_info_t* info, iris_memory_pool_t* dst)
{
  nv_assert_else_return(driver != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(iris_driver_is_valid(driver), NV_ERROR_INVALID_ARG);
  nv_assert_else_return(info != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(info->minimum_alignment != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(info->size != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);

  nv_bzero(dst, sizeof(iris_memory_pool_t));

  dst->canary = 0xDEADBEEF;

  const iris_size_t preffered_alignment = get_preferred_alignment(driver->vkctx);
  const iris_size_t alignment           = NV_MAX(preffered_alignment, info->minimum_alignment);

  const iris_size_t aligned_size = align_up_size(info->size, alignment);

  VkMemoryPropertyFlags const vk_property_flags = iris_nv_memory_flags_to_vk_flags(info->memory_flags);

  VkMemoryAllocateInfo allocInfo = nv_zinit(VkMemoryAllocateInfo);
  allocInfo.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize       = aligned_size;

  /**
   * We pass in uint32_max here to tell vulkan that
   * we aren't allocating for a resource and that all
   * memory types are valid.
   */
  allocInfo.memoryTypeIndex = nv_vk_get_mem_type(driver->vkctx, UINT32_MAX, vk_property_flags);

  nvvk_result_check(*driver->vkctx, vkAllocateMemory(driver->vkctx->device, &allocInfo, &driver->vkctx->vkalloc, &dst->memory_handle));
  nv_assert_else_return(dst->memory_handle != VK_NULL_HANDLE, NV_ERROR_MALLOC_FAILED);

  dst->allocated_size = aligned_size;
  dst->alignment      = alignment;
  dst->driver         = driver;

  dst->memory_flags = info->memory_flags;
  dst->flags        = info->flags;

  dst->type   = info->type;
  dst->policy = info->policy;

  switch (info->type)
  {
    case IRIS_ALLOCATOR_STACK: iris_memory_allocator_stack_init(aligned_size, &dst->backing_allocator.stack); break;
    case IRIS_ALLOCATOR_FREELIST: iris_freelist_init(aligned_size, &dst->backing_allocator.flist); break;
    default: break;
  }

  /**
   * TODO: We don't want to always map the *entire* memory, do we?
   */
  if ((info->memory_flags & IRIS_MEMORY_FLAGS_MAPPABLE_BIT) != 0u)
  {
    nvvk_result_check(*driver->vkctx, vkMapMemory(driver->vkctx->device, dst->memory_handle, 0, aligned_size, 0, &dst->drv_mapped));
    nv_assert_else_return(dst->drv_mapped != NULL, NV_ERROR_INVALID_RETVAL);
  }

  return NV_SUCCESS;
}

void
iris_memory_pool_destroy(iris_memory_pool_t* pool)
{
  if (pool == NULL)
  {
    return;
  }

  nv_assert_else_return(iris_driver_is_valid(pool->driver), );
  nv_assert_else_return(pool->canary == 0xDEADBEEF, );

  VkDevice device = pool->driver->vkctx->device;

  if (pool->memory_handle != VK_NULL_HANDLE)
  {
    vkDeviceWaitIdle(device);
    vkFreeMemory(device, pool->memory_handle, &pool->driver->vkctx->vkalloc);
  }

  switch (pool->type)
  {
    case IRIS_ALLOCATOR_STACK: iris_memory_allocator_stack_destroy(&pool->backing_allocator.stack); break;
    case IRIS_ALLOCATOR_FREELIST: iris_freelist_destroy(&pool->backing_allocator.flist); break;
    default: break;
  }

  nv_zero_structp(pool);
}

nv_error
iris_memory_map(iris_memory_t* memory, size_t offset, size_t size, void** mapping)
{
  nv_assert_else_return(memory != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(mapping != NULL, NV_ERROR_INVALID_ARG);

  // If already mapped and request fits within that mapping
  if (memory->drv_mapped != NULL)
  {
    size_t mapped_begin = memory->drv_mapped_offset;
    size_t mapped_end   = mapped_begin + memory->drv_mapped_size;
    size_t req_begin    = offset;
    size_t req_end      = offset + size;

    if (req_begin >= mapped_begin && req_end <= mapped_end)
    {
      *mapping = (uchar*)memory->drv_mapped + (offset - mapped_begin);
      return NV_SUCCESS;
    }

    // Otherwise unmap and remap
    if ((bool)(memory->memory_flags & IRIS_MEMORY_FLAGS_DEDICATED_BIT))
    {
      vkUnmapMemory(memory->driver->vkctx->device, memory->dedicated_allocation);
    }
    memory->drv_mapped        = NULL;
    memory->drv_mapped_size   = 0;
    memory->drv_mapped_offset = 0;
  }

  if ((bool)(memory->memory_flags & IRIS_MEMORY_FLAGS_DEDICATED_BIT))
  {
    void*    tmp = NULL;
    VkResult res = vkMapMemory(memory->driver->vkctx->device, memory->dedicated_allocation, offset, size, 0, &tmp);
    nv_assert_else_return(res == VK_SUCCESS, NV_ERROR_EXTERNAL);

    memory->drv_mapped        = tmp;
    memory->drv_mapped_size   = size;
    memory->drv_mapped_offset = offset;

    *mapping = tmp;
    return NV_SUCCESS;
  }
  else
  {
    nv_assert_else_return(memory->pool != NULL, NV_ERROR_BROKEN_STATE);
    nv_assert_else_return(memory->pool->drv_mapped != NULL, NV_ERROR_EXTERNAL);

    memory->drv_mapped        = (uchar*)memory->pool->drv_mapped + memory->pool_offset;
    memory->drv_mapped_size   = memory->size; // whole block
    memory->drv_mapped_offset = 0;

    *mapping = (uchar*)memory->drv_mapped + offset;
    return NV_SUCCESS;
  }
}

void
iris_memory_unmap(iris_memory_t* memory)
{
  nv_assert_else_return(memory != NULL, );
  nv_assert_else_return(memory->drv_mapped_size != 0, );
  nv_assert_else_return(memory->drv_mapped_offset < memory->size, );
  nv_assert_else_return((memory->memory_flags & IRIS_MEMORY_FLAGS_PERSISTENT_MAPPED_BIT) == false, );
  if (!(memory->memory_flags & IRIS_MEMORY_FLAGS_PERSISTENT_MAPPED_BIT))
  {
    nv_log_error("pee is stored in the balls\n");
    return;
  }

  VkDevice device = memory->driver->vkctx->device;

  nv_error const code = iris_memory_flush(memory);
  if (code != NV_SUCCESS)
  {
    return;
  }

  if ((memory->memory_flags & IRIS_MEMORY_FLAGS_DEDICATED_BIT) != 0u)
  {
    vkUnmapMemory(device, iris_memory_get_backing(memory));
  }

  memory->drv_mapped        = NULL;
  memory->drv_mapped_size   = 0;
  memory->drv_mapped_offset = 0;
}

nv_error
iris_memory_allocate_dedicated(iris_driver_t* driver, u32 memory_type_bits, iris_memory_flags flags, iris_size_t size, iris_size_t alignment, iris_memory_t* dst_block)
{
  nv_assert_else_return(iris_driver_is_valid(driver) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst_block != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(size != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(alignment != 0, NV_ERROR_INVALID_ARG);

  nv_zero_structp(dst_block);

  const iris_size_t preffered_alignment = get_preferred_alignment(driver->vkctx);
  alignment                             = NV_MAX(preffered_alignment, alignment);

  const iris_size_t aligned_size = align_up_size(size, alignment);

  flags |= IRIS_MEMORY_FLAGS_DEDICATED_BIT;
  VkMemoryPropertyFlags const vk_property_flags = iris_nv_memory_flags_to_vk_flags(flags);

  VkMemoryAllocateInfo allocInfo = nv_zinit(VkMemoryAllocateInfo);
  allocInfo.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize       = aligned_size;

  /**
   * We pass in uint32_max here to tell vulkan that
   * we aren't allocating for a resource and that all
   * memory types are valid.
   */
  allocInfo.memoryTypeIndex = nv_vk_get_mem_type(driver->vkctx, memory_type_bits, vk_property_flags);

  nvvk_result_check(*driver->vkctx, vkAllocateMemory(driver->vkctx->device, &allocInfo, &driver->vkctx->vkalloc, &dst_block->dedicated_allocation));
  nv_assert_else_return(dst_block->dedicated_allocation != VK_NULL_HANDLE, NV_ERROR_MALLOC_FAILED);

  dst_block->driver       = driver;
  dst_block->size         = aligned_size;
  dst_block->alignment    = alignment;
  dst_block->memory_flags = flags | IRIS_MEMORY_FLAGS_DEDICATED_BIT;
  dst_block->pool_offset  = 0;

  return NV_SUCCESS;
}

nv_error
iris_memory_allocate(iris_memory_pool_t* pool, iris_size_t size, iris_size_t alignment, iris_memory_t* dst_block)
{
  nv_assert_else_return(pool != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(pool->canary == 0xDEADBEEF, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(dst_block != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(size != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(alignment != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(size <= pool->allocated_size, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(align_up_size(size, alignment) <= pool->allocated_size, NV_ERROR_INVALID_ARG);

  const iris_size_t preffered_alignment = NV_MAX(pool->alignment, alignment);
  const iris_size_t aligned_size        = align_up_size(size, preffered_alignment);

  // pool->user_data += aligned_size;
  // nv_log_info("VKMEMUSG::(%llu|+%llu)\n", pool->user_data, aligned_size);

  if (pool->type == IRIS_ALLOCATOR_STACK)
  {
    iris_allocator_stack_t* stack = &pool->backing_allocator.stack;

    *dst_block = (iris_memory_t){
      .driver       = pool->driver,
      .pool         = pool,
      .memory_flags = pool->memory_flags,
      .size         = aligned_size,
      .pool_offset  = stack->bumper,
      .alignment    = preffered_alignment,
    };

    stack->last_allocation_bumper = stack->bumper;

    stack->bumper += aligned_size;
    stack->bumper = align_up_size(stack->bumper, preffered_alignment);

    nv_assert_else_return(stack->bumper <= pool->allocated_size, NV_ERROR_MALLOC_FAILED);
  }
  else if (pool->type == IRIS_ALLOCATOR_FREELIST)
  {
    iris_freelist_t* freelist = &pool->backing_allocator.flist;

    iris_size_t offset = SIZE_MAX;
    iris_freelist_alloc(freelist, aligned_size, preffered_alignment, pool->policy, &offset);

    nv_assert_else_return(offset != SIZE_MAX, NV_ERROR_INVALID_RETVAL);
    nv_assert_else_return((offset % preffered_alignment) == 0, NV_ERROR_INVALID_RETVAL);

    *dst_block = (iris_memory_t){
      .driver       = pool->driver,
      .pool         = pool,
      .memory_flags = pool->memory_flags,
      .size         = aligned_size,
      .pool_offset  = offset,
      .alignment    = preffered_alignment,
    };
  }
  else
  {
    nv_log_error("Unimplemented type for allocator.\n");
    return NV_ERROR_INVALID_INPUT;
  }

  /* assert that memory is aligned */
  nv_assert_else_return((dst_block->size % get_preferred_alignment(pool->driver->vkctx)) == 0, NV_ERROR_UNKNOWN);

  return NV_SUCCESS;
}

nv_error
iris_memory_free(iris_memory_t* memory)
{
  nv_assert_else_return(memory != NULL, NV_ERROR_INVALID_ARG);

  if ((memory->memory_flags & IRIS_MEMORY_FLAGS_DEDICATED_BIT) != 0 && memory->dedicated_allocation != VK_NULL_HANDLE)
  {
    vkFreeMemory(memory->driver->vkctx->device, memory->dedicated_allocation, &memory->driver->vkctx->vkalloc);
    return NV_SUCCESS;
  }

  iris_memory_pool_t* pool = memory->pool;

  if (!pool)
  {
    pool = pool;
  }
  nv_assert_else_return(pool->canary == 0xDEADBEEF, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(pool->memory_handle != VK_NULL_HANDLE, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(pool->allocated_size > 0, NV_ERROR_INVALID_ARG);

  if (pool->type == IRIS_ALLOCATOR_STACK)
  {
    iris_allocator_stack_t* stack = &pool->backing_allocator.stack;

    if (memory->pool_offset == stack->last_allocation_bumper)
    {
      stack->bumper -= memory->size;
      stack->bumper = align_up_size(stack->bumper, pool->alignment);
    }
  }
  else if (pool->type == IRIS_ALLOCATOR_FREELIST)
  {
    iris_freelist_t* freelist = &pool->backing_allocator.flist;
    iris_freelist_free(freelist, memory->size, memory->pool_offset);
  }
  else
  {
    nv_log_error("Unimplemented type for allocator.\n");
    return NV_ERROR_INVALID_INPUT;
  }

  return NV_SUCCESS;
}

nv_error
iris_memory_allocator_stack_init(iris_size_t aligned_size, iris_allocator_stack_t* stack)
{
  nv_assert_else_return(stack != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(aligned_size != 0, NV_ERROR_INVALID_ARG);

  nv_bzero(stack, sizeof(iris_allocator_stack_t));

  stack->bumper                 = 0;
  stack->last_allocation_bumper = 0;

  return NV_SUCCESS;
}

void
iris_memory_allocator_stack_destroy(iris_allocator_stack_t* stack)
{
  nv_assert_else_return(stack != NULL, );

  nv_bzero(stack, sizeof(iris_allocator_stack_t));
}

nv_error
nvvk_allocator_init(nvvk_allocator_t* dst)
{
  (void)dst;
  dst->stop_crying = 0;
  return NV_SUCCESS;
}

void
nvvk_allocator_destroy(nvvk_allocator_t* alloc)
{
  nv_bzero(alloc, sizeof(*alloc));
}

void*
nvvk_alloc(void* user_data, size_t size, size_t alignment, VkSystemAllocationScope scope)
{
  nvvk_ctx_t* ctx = user_data;
  if (NV_UNLIKELY(size == 0))
    return &ctx->i_have_to_respond_to_0_size_allocations_for_some_reason_with_a_valid_pointer_why_vulkan_why_why_cant_you_just_be_normal;

  (void)scope;
  (void)user_data;
  void* p = nv_aligned_alloc(size, alignment);
#ifndef NDEBUG
  if (p == NULL)
  {
    nv_log_error("vkalloc for size=%zu align=%zu returned NULL\n", size, alignment);
  }
#endif
  return p;
}

void*
nvvk_realloc(void* user_data, void* orig, size_t new_size, size_t alignment, VkSystemAllocationScope scope)
{
  (void)scope;
  (void)user_data;
  return nv_aligned_realloc(orig, new_size, alignment);
}

void
nvvk_free(void* user_data, void* ptr)
{
  (void)user_data;
  nv_aligned_free(ptr);
}

void
nvvk_internal_allocation(void* user_data, size_t size, VkInternalAllocationType allocationType, VkSystemAllocationScope allocationScope)
{
  (void)user_data;
  (void)size;
  (void)allocationType;
  (void)allocationScope;
}

void
nvvk_internal_free(void* user_data, size_t size, VkInternalAllocationType allocationType, VkSystemAllocationScope allocationScope)
{
  (void)user_data;
  (void)size;
  (void)allocationType;
  (void)allocationScope;
}

static inline iris_freelist_block_t*
alloc_node(iris_freelist_t* flist)
{
  if (flist->free_nodes != NULL)
  {
    iris_freelist_block_t* node = flist->free_nodes;
    flist->free_nodes           = node->next;
    return node;
  }
  return nv_alloc_struct(iris_freelist_block_t);
}

static inline void
free_node(iris_freelist_t* flist, iris_freelist_block_t* node)
{
  node->next        = flist->free_nodes;
  flist->free_nodes = node;
}

nv_error
iris_freelist_init(size_t initial_capacity, iris_freelist_t* dst)
{
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);

  nv_zero_structp(dst);

  dst->root         = alloc_node(dst);
  dst->root->offset = 0;
  dst->root->size   = initial_capacity;
  dst->root->next   = NULL;

  return NV_SUCCESS;
}

void
iris_freelist_destroy(iris_freelist_t* flist)
{
  if (flist == NULL)
  {
    return;
  }

  iris_freelist_block_t* node = flist->root;
  while (node != NULL)
  {
    iris_freelist_block_t* next = node->next;
    nv_free(node);
    node = next;
  }

  node = flist->free_nodes;
  while (node != NULL)
  {
    iris_freelist_block_t* next = node->next;
    nv_free(node);
    node = next;
  }
}

static inline nv_error
iris_freelist_insert_node_last(iris_freelist_t* flist, iris_freelist_block_t** ret)
{
  nv_assert_else_return(flist != NULL, NV_ERROR_INVALID_ARG);

  flist->num_nodes++;
  // nv_log_info("NNODES:%zu\n", flist->num_nodes);

  if (flist->root == NULL)
  {
    flist->root = alloc_node(flist);
    nv_assert_else_return(flist->root != NULL, NV_ERROR_MALLOC_FAILED);

    *ret = flist->root;
    return NV_SUCCESS;
  }

  iris_freelist_block_t* node = flist->root;
  while (node->next != NULL)
  {
    node = node->next;
  }

  node->next = alloc_node(flist);
  nv_assert_else_return(node->next != NULL, NV_ERROR_MALLOC_FAILED);

  *ret = node->next;

  return NV_SUCCESS;
}

static inline iris_freelist_block_t*
fl_alloc(iris_freelist_t* flist, size_t aligned_size, iris_allocator_policy policy, iris_freelist_block_t** prev)
{
  iris_freelist_block_t* best_fit_node  = NULL;
  iris_freelist_block_t* worst_fit_node = NULL;
  iris_freelist_block_t* best_fit_prev  = NULL;
  iris_freelist_block_t* worst_fit_prev = NULL;

  iris_freelist_block_t* node = flist->root;

  size_t min_fit_size    = SIZE_MAX;
  size_t curr_worst_size = 0;

  iris_freelist_block_t* prev_node = NULL;
  *prev                            = NULL;

  while (node != NULL)
  {
    if (node->size < aligned_size)
    {
      prev_node = node;
      node      = node->next;
      continue;
    }

    if (policy == IRIS_ALLOCATOR_POLICY_FIRST_FIT)
    {
      *prev = prev_node;
      return node;
    }

    if (node->size > curr_worst_size)
    {
      worst_fit_node  = node;
      curr_worst_size = node->size;
      worst_fit_prev  = prev_node;
    }

    if (node->size < min_fit_size)
    {
      best_fit_node = node;
      min_fit_size  = node->size;
      best_fit_prev = prev_node;
    }

    prev_node = node;
    node      = node->next;
  }

  if (policy == IRIS_ALLOCATOR_POLICY_WORST_FIT && worst_fit_node != NULL)
  {
    *prev = worst_fit_prev;
    return worst_fit_node;
  }
  else if (best_fit_node != NULL)
  {
    *prev = best_fit_prev;
    return best_fit_node;
  }

  return NULL;
}

bool
iris_freelist_alloc(iris_freelist_t* flist, size_t size, size_t alignment, iris_allocator_policy policy, size_t* offset_out)
{
  nv_assert_else_return(flist, false);
  nv_assert_else_return(flist->root != NULL, false);
  nv_assert_else_return(offset_out, false);

  nv_error code = iris_freelist_defrag(flist);
  if (code != NV_SUCCESS)
  {
    return false;
  }

  const size_t aligned_size = align_up_size(size, alignment);

  iris_freelist_block_t* prev          = NULL;
  iris_freelist_block_t* best_fit_node = fl_alloc(flist, aligned_size, policy, &prev);

  if (best_fit_node == NULL)
  {
    return false;
  }

  if (prev != NULL)
  {
    prev->next = best_fit_node->next;
  }
  else
  {
    flist->root = best_fit_node->next;
  }

  size_t const aligned_offset    = align_up_size(best_fit_node->offset, alignment);
  size_t const alignment_padding = aligned_offset - best_fit_node->offset;

  if (best_fit_node->size < alignment_padding + size)
  {
    return NV_ERROR_MALLOC_FAILED != 0;
  }

  best_fit_node->offset = aligned_offset;
  best_fit_node->size -= alignment_padding + size;

  size_t const remaining = best_fit_node->size;

  bool const next_node_can_recieve_remaining = (best_fit_node->next != NULL) && best_fit_node->next->offset == (best_fit_node->offset + size);
  if (remaining > 0 && next_node_can_recieve_remaining)
  {
    best_fit_node->next->offset = best_fit_node->offset + size;
    best_fit_node->next->size += remaining;
    best_fit_node->size = size;
  }
  else if (remaining > 0)
  {
    iris_freelist_block_t* suffix = NULL;

    code = iris_freelist_insert_node_last(flist, &suffix);
    nv_assert_else_return(code == NV_SUCCESS, code);

    suffix->offset      = best_fit_node->offset + size;
    suffix->size        = remaining;
    best_fit_node->size = size;
  }

  *offset_out = best_fit_node->offset;

  free_node(flist, best_fit_node);

  return true;
}

nv_error
iris_freelist_free(iris_freelist_t* flist, size_t offset, size_t size)
{
  nv_assert_else_return(flist != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(flist->root != NULL, NV_ERROR_INVALID_ARG);
  if (size == 0)
  {
    return NV_SUCCESS;
  }

  iris_freelist_block_t* new_node = NULL;

  nv_error code = iris_freelist_insert_node_last(flist, &new_node);
  nv_assert_else_return(new_node != NULL && code == NV_SUCCESS, code);

  new_node->offset = offset;
  new_node->size   = size;

  code = iris_freelist_defrag(flist);
  if (code != NV_SUCCESS)
  {
    return code;
  }

  return NV_SUCCESS;
}

static inline iris_freelist_block_t*
sorted_merge(iris_freelist_block_t* a, iris_freelist_block_t* b)
{
  if (a == NULL)
  {
    return b;
  }
  if (b == NULL)
  {
    return a;
  }

  if (a->offset <= b->offset)
  {
    a->next = sorted_merge(a->next, b);
    return a;
  }
  else
  {
    b->next = sorted_merge(a, b->next);
    return b;
  }
}

static inline void
front_back_split(iris_freelist_block_t* source, iris_freelist_block_t** front, iris_freelist_block_t** back)
{
  iris_freelist_block_t* slow = source;
  iris_freelist_block_t* fast = source->next;

  while (fast != NULL)
  {
    fast = fast->next;
    if (fast != NULL)
    {
      slow = slow->next;
      fast = fast->next;
    }
  }

  *front     = source;
  *back      = slow->next;
  slow->next = NULL;
}

static inline iris_freelist_block_t*
merge_sort(iris_freelist_block_t* root)
{
  if ((root == NULL) || (root->next == NULL))
  {
    return root;
  }

  iris_freelist_block_t* a = NULL;
  iris_freelist_block_t* b = NULL;

  front_back_split(root, &a, &b);

  a = merge_sort(a);
  b = merge_sort(b);

  return sorted_merge(a, b);
}

static inline void
iris_freelist_sort(iris_freelist_t* flist)
{
  flist->root = merge_sort(flist->root);
}

nv_error
iris_freelist_defrag(iris_freelist_t* flist)
{
  iris_freelist_block_t* cur = flist->root;

  while ((cur != NULL) && (cur->next != NULL))
  {
    iris_freelist_block_t* next = cur->next;

    if (cur->offset + cur->size == next->offset)
    {
      cur->size += next->size;
      cur->next = next->next;

      free_node(flist, next);
    }
    else
    {
      cur = next;
    }
  }

  return NV_SUCCESS;
}