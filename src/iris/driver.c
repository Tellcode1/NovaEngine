#include "../../include/iris/driver.h"
#include "../../external/volk/volk.h"
#include "../../include/iris/buffer.h"
#include "../../include/iris/memory.h"
#include "../../include/iris/prep.h"
#include "../../include/iris/sampler.h"
#include "../../include/iris/types.h"
#include "../../include/iris/utils.h"
#include "../../include/std/include/alloc.h"
#include "../../include/std/include/containers/list.h"
#include "../../include/std/include/errorcodes.h"
#include "../../include/std/include/print.h"
#include "../../include/std/include/stdafx.h"
#include "../../include/std/include/string.h"
#include "../../include/std/include/types.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef IRIS_DISABLE_OPTIMIZATIONS
#  define IRIS_DISABLE_OPTIMIZATIONS (true)
#endif

#define DOES_ALIAS(ptr, array, size) ((void*)(ptr) >= (void*)(array) && (void*)(ptr) <= (void*)((uchar*)(array) + (size)))

static inline bool
has_flag(u32 flags, u32 want)
{
  return (flags & want) != 0u;
}

static inline nv_error
generate_and_insert_buffer_copy(VkCommandBuffer cmd, VkBuffer dst, VkBuffer src, size_t num_bytes, size_t dst_offset, size_t src_offset)
{
  VkBufferCopy const copy = (VkBufferCopy){
    .srcOffset = src_offset,
    .dstOffset = dst_offset,
    .size      = num_bytes,
  };
  vkCmdCopyBuffer(cmd, src, dst, 1, &copy);

  return NV_SUCCESS;
}

nv_error
iris_driver_init(nvvk_ctx_t* ctx, iris_driver_t* dst)
{
  nv_assert_else_return(nvvk_ctx_is_valid(ctx) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);

  nv_bzero(dst, sizeof(iris_driver_t));

  dst->canary = 0xDEADBEEF;

  dst->vkctx = ctx;

  nv_error code = NV_SUCCESS;

  /* NOTE: POINTERS. We're storing POINTERS */
  code = nv_list_init(sizeof(iris_buffer_t*), 16, nv_allocator_c, NULL, &dst->buffers);
  nv_assert_else_return(code == NV_SUCCESS, code);

  code = nv_list_init(sizeof(iris_sampler_t), 16, nv_allocator_c, NULL, &dst->samplers);
  nv_assert_else_return(code == NV_SUCCESS, code);

  iris_memory_pool_create_info_t pool_ci = (iris_memory_pool_create_info_t){
    .size              = 10000000,
    .minimum_alignment = 1,
    .memory_flags      = IRIS_MEMORY_FLAGS_MAPPABLE_BIT,
    .type              = IRIS_ALLOCATOR_FREELIST,
    .policy            = IRIS_ALLOCATOR_POLICY_BEST_FIT,
    .flags             = 0,
  };
  code = iris_memory_pool_init(dst, &pool_ci, &dst->cpu_mappable_pool);
  nv_assert_else_return(code == NV_SUCCESS, code);

  pool_ci = (iris_memory_pool_create_info_t){
    .size              = 10000000,
    .minimum_alignment = 1,
    .memory_flags      = 0,
    .type              = IRIS_ALLOCATOR_FREELIST,
    .policy            = IRIS_ALLOCATOR_POLICY_WORST_FIT,
    .flags             = 0,
  };
  code = iris_memory_pool_init(dst, &pool_ci, &dst->gpu_local_pool);
  nv_assert_else_return(code == NV_SUCCESS, code);

  iris_buffer_extra_create_info_t extra_info = nv_zero_init(iris_buffer_extra_create_info_t);
  extra_info.custom_memory_flags             = dst->cpu_mappable_pool.memory_flags | IRIS_MEMORY_FLAGS_PERSISTENT_MAPPED_BIT;
  extra_info.custom_memory_pool              = &dst->cpu_mappable_pool;

  code = iris_buffer_init(
      dst, IRIS_SMALL_TRANSFER_BUFFER_SIZE, IRIS_SMALL_TRANSFER_BUFFER_ALIGNMENT, &extra_info, IRIS_SMALL_TRANSFER_BUFFER_CREATE_FLAGS, &dst->small_transfer_buffer);
  nv_assert_else_return(code == NV_SUCCESS, code);

  code = iris_buffer_init(
      dst, IRIS_LARGE_TRANSFER_BUFFER_INITIAL_SIZE, IRIS_LARGE_TRANSFER_BUFFER_ALIGNMENT, &extra_info, IRIS_LARGE_TRANSFER_BUFFER_CREATE_FLAGS, &dst->large_transfer_buffer);
  nv_assert_else_return(code == NV_SUCCESS, code);

  /**
   * This should never fail and indicates an error in the implementation of iris_driver_is_valid.
   */
  nv_assert_else_return(iris_driver_is_valid(dst) == true, NV_ERROR_INVALID_RETVAL);

  return NV_SUCCESS;
}

void
iris_driver_destroy(iris_driver_t* driver)
{
  if ((driver == NULL) || !iris_driver_is_valid(driver))
  {
    return;
  }

  if (nv_list_size(&driver->buffers) != 0)
  {
    nv_log_error("Driver still held %zu buffers at time of destruction.\n", nv_list_size(&driver->buffers));
  }

  for (size_t i = 0; i < nv_list_size(&driver->samplers); i++)
  {
    iris_sampler_t* sampler = (iris_sampler_t*)nv_list_get(&driver->samplers, i);
    if ((sampler != NULL) && (sampler->handle != NULL))
    {
      vkDestroySampler(driver->vkctx->device, sampler->handle, &driver->vkctx->vkalloc);
    }
  }

  for (size_t i = 0; i < IRIS_COMMAND_BUFFER_CACHE_COUNT; i++)
  {
    vkDestroyFence(driver->vkctx->device, driver->vkctx->cmd_buffer_fences[i], &driver->vkctx->vkalloc);
  }

  nv_list_destroy(&driver->buffers);
  nv_list_destroy(&driver->samplers);

  iris_buffer_destroy(&driver->small_transfer_buffer);
  iris_buffer_destroy(&driver->large_transfer_buffer);

  /**
   * Always destroy child resources (buffers) before parent resources (their pools)
   */
  iris_memory_pool_destroy(&driver->cpu_mappable_pool);
  iris_memory_pool_destroy(&driver->gpu_local_pool);

  nv_bzero(driver, sizeof(iris_driver_t));
}

bool
iris_driver_is_valid(const iris_driver_t* driver)
{
  if (driver == NULL)
  {
    return false;
  }

  if (driver->canary != 0xDEADBEEF)
  {
    return false;
  }

  if (!nv_list_is_valid(&driver->samplers) || !nv_list_is_valid(&driver->buffers))
  {
    return false;
  }

  if (driver->vkctx == NULL || !nvvk_ctx_is_valid(driver->vkctx))
  {
    return false;
  }

  /* iris_driver_is_valid is called by the functions to initialize these, we cannot check these */
  // if (driver->small_transfer_buffer.buffer == VK_NULL_HANDLE || driver->large_transfer_buffer.buffer == VK_NULL_HANDLE)
  // {
  //   return false;
  // }

  return true;
}

#define PATHMAX 4096

static inline nv_error
readfile(const char* fname, char** data, size_t* data_size)
{
  nv_assert_else_return(fname != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(data != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(data_size != NULL, NV_ERROR_INVALID_ARG);

  FILE* f = fopen(fname, "rb");
  if (f == NULL)
  {
    return NV_ERROR_IO_ERROR;
  }

  fseek(f, 0, SEEK_END);
  long const size = ftell(f);
  if (size < 0)
  {
    fclose(f);
    return NV_ERROR_IO_ERROR;
  }
  if (fseek(f, 0, SEEK_SET) != 0)
  {
    fclose(f);
    return NV_ERROR_IO_ERROR;
  }

  *data = (char*)nv_calloc(size + 1);
  if (*data == NULL)
  {
    fclose(f);
    return NV_ERROR_MALLOC_FAILED;
  }

  size_t const read_size = fread(*data, 1, size, f);
  fclose(f);

  if (read_size != (size_t)size)
  {
    nv_free(*data);
    *data = NULL;
    return NV_ERROR_IO_ERROR;
  }

  *data_size          = read_size;
  (*data)[*data_size] = '\0';
  return NV_SUCCESS;
}

nv_error
nv_prep_flatten_file_to_file(const char* file, FILE* out)
{
  nv_assert_else_return(file != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(out != NULL, NV_ERROR_INVALID_ARG);

  char*  contents      = NULL;
  size_t contents_size = 0;

  nv_error const code = readfile(file, &contents, &contents_size);
  if (code != NV_SUCCESS)
  {
    return code;
  }

  char* ctx  = NULL;
  char* line = nv_strtok(contents, "\n", &ctx);

  while (line != NULL)
  {
    char* p = line;
    while (*p == ' ' || *p == '\t')
    {
      p++;
    }

    if (*p != '#')
    {
      fputs(line, out);
      fputc('\n', out);
      line = nv_strtok(NULL, "\n", &ctx);
      continue;
    }

    p++;

    while (*p == ' ' || *p == '\t')
    {
      p++;
    }

    if (nv_strncmp(p, "include", 7) == 0 && (p[7] == ' ' || p[7] == '\t' || p[7] == '\"'))
    {
      p += sizeof("include") - 1;

      while (*p == ' ' || *p == '\t')
      {
        p++;
      }

      if (*p != '\"')
      {
        fputs(line, out);
        fputc('\n', out);
        line = nv_strtok(NULL, "\n", &ctx);
        continue;
      }

      const char* start = p + 1;
      const char* end   = nv_strchr(start, '\"');

      if ((end == NULL) || end <= start)
      {
        fputs(line, out);
        fputc('\n', out);
        line = nv_strtok(NULL, "\n", &ctx);
        continue;
      }

      size_t const name_len = end - start;

      char tmp[1] = { 0 };

      /* 1) figure out directory of `file` */
      char*       base = NULL;
      const char* s1   = nv_strrchr(file, '/');
      const char* s2   = nv_strrchr(file, '\\');
      const char* sep  = s1 > s2 ? s1 : s2;
      if (sep != NULL)
      {
        size_t const dir_len = sep - file + 1; /* include the slash */
        base                 = (char*)nv_calloc(dir_len + 1);
        nv_memcpy(base, file, dir_len);
        base[dir_len] = '\0';
      }
      else
      {
        /**
         * Point base to a 0 byte.
         * This effectively sets the string to 0 length
         */
        base = tmp;
      }

      size_t const full_len = nv_strlen(base) + name_len;

      char* fullpath = (char*)nv_calloc(full_len + 1);
      nv_snprintf(fullpath, full_len + 1, "%s%.*s", base, (int)name_len, start);
      fullpath[full_len] = 0;

      nv_error const sub = nv_prep_flatten_file_to_file(fullpath, out);
      if (sub != NV_SUCCESS)
      {
        return sub;
      }

      fputc('\n', out);
      line = nv_strtok(NULL, "\n", &ctx);

      /**
       * We may point base to a stack
       * buffer. We dont' want to free that.
       */
      if (base != tmp)
      {
        nv_free(base);
      }
      nv_free(fullpath);
      continue;
    }

    fputs(line, out);
    fputc('\n', out);
    line = nv_strtok(NULL, "\n", &ctx);
  }

  nv_free(contents);
  return NV_SUCCESS;
}

static inline const char*
skip_whitespace(const char* in)
{
  while (*in == ' ' || *in == '\t')
  {
    in++;
  }
  return in;
}

nv_error
nv_prep_flatten_file_to_buffer(const char* file, char** buffer, size_t* buffer_size)
{
  nv_assert_else_return(file != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(buffer != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(buffer_size != NULL, NV_ERROR_INVALID_ARG);

  char*  contents      = NULL;
  size_t contents_size = 0;

  nv_error const code = readfile(file, &contents, &contents_size);
  if (code != NV_SUCCESS)
  {
    return code;
  }

  if ((*buffer == NULL) || *buffer_size == 0)
  {
    const size_t buffer_start_size = 4096;

    *buffer      = (char*)nv_calloc(buffer_start_size);
    *buffer_size = buffer_start_size;
  }

  const size_t buflen = nv_strlen(*buffer);
  if ((*buffer_size - buflen) <= contents_size)
  {
    const size_t new_buffer_size = NV_MAX(*buffer_size * 2, buflen + contents_size);
    *buffer                      = (char*)nv_realloc(*buffer, new_buffer_size);
    *buffer_size                 = new_buffer_size;
  }

  char* ctx  = NULL;
  char* line = nv_strtok(contents, "\n", &ctx);

  while (line != NULL)
  {
    char* p = line;
    while (*p == ' ' || *p == '\t')
    {
      p++;
    }

    if (*p != '#')
    {
      nv_strlcat(*buffer, line, *buffer_size);
      nv_strlcat(*buffer, "\n", *buffer_size);
      line = nv_strtok(NULL, "\n", &ctx);
      continue;
    }

    p++;

    while (*p == ' ' || *p == '\t')
    {
      p++;
    }

    if (nv_strncmp(p, "include", 7) == 0 && (p[7] == ' ' || p[7] == '\t' || p[7] == '\"'))
    {
      p += sizeof("include") - 1;

      while (*p == ' ' || *p == '\t')
      {
        p++;
      }

      if (*p != '\"')
      {
        nv_strlcat(*buffer, line, *buffer_size);
        nv_strlcat(*buffer, "\n", *buffer_size);
        line = nv_strtok(NULL, "\n", &ctx);
        continue;
      }

      const char* start = p + 1;
      const char* end   = nv_strchr(start, '\"');

      if ((end == NULL) || end <= start)
      {
        nv_strlcat(*buffer, line, *buffer_size);
        nv_strlcat(*buffer, "\n", *buffer_size);
        line = nv_strtok(NULL, "\n", &ctx);
        continue;
      }

      size_t const name_len = end - start;

      char tmp[1] = { 0 };

      /* 1) figure out directory of `file` */
      char*       base = NULL;
      const char* s1   = nv_strrchr(file, '/');
      const char* s2   = nv_strrchr(file, '\\');
      const char* sep  = s1 > s2 ? s1 : s2;
      if (sep != NULL)
      {
        size_t const dir_len = sep - file + 1; /* include the slash */
        base                 = (char*)nv_calloc(dir_len + 1);
        nv_memcpy(base, file, dir_len);
        base[dir_len] = '\0';
      }
      else
      {
        /**
         * Point base to a 0 byte.
         * This effectively sets the string to 0 length
         */
        base = tmp;
      }

      size_t const full_len = nv_strlen(base) + name_len;

      char* fullpath = (char*)nv_calloc(full_len + 1);
      nv_snprintf(fullpath, full_len + 1, "%s%.*s", base, (int)name_len, start);
      fullpath[full_len] = 0;

      nv_strlcat(*buffer, "\n", *buffer_size);

      nv_error const sub = nv_prep_flatten_file_to_buffer(fullpath, buffer, buffer_size);
      if (sub != NV_SUCCESS)
      {
        return sub;
      }

      nv_strlcat(*buffer, "\n", *buffer_size);

      line = nv_strtok(NULL, "\n", &ctx);

      /**
       * We may point base to a stack
       * buffer. We dont' want to free that.
       */
      if (base != tmp)
      {
        nv_free(base);
      }
      nv_free(fullpath);
      continue;
    }

    nv_strlcat(*buffer, line, *buffer_size);
    nv_strlcat(*buffer, "\n", *buffer_size);
    line = nv_strtok(NULL, "\n", &ctx);
  }

  nv_free(contents);
  return NV_SUCCESS;
}

void
iris_begin_upload_batch(iris_driver_t* driver)
{
  if (driver->active_upload_cmd)
  {
    nv_log_error("An upload batch is already active.\n");
    return;
  }
  driver->active_upload_cmd = nv_vk_begin_command_buffer(driver);
}

void
iris_end_upload_batch(iris_driver_t* driver)
{
  if (!driver->active_upload_cmd)
  {
    nv_log_error("An upload batch has not been started\n");
    return;
  }
  nv_vk_end_command_buffer(driver, driver->active_upload_cmd, driver->vkctx->graphics_queue, true);
  driver->active_upload_cmd = VK_NULL_HANDLE;
}

bool
iris_is_upload_batch_active(const iris_driver_t* driver)
{
  return driver->active_upload_cmd != VK_NULL_HANDLE;
}
