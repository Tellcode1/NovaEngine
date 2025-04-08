#include "engine/nvsm.h"
#include "GPU/pipeline.h"
#include "GPU/vk.h"
#include "GPU/vkstdafx.h"
#include "common/mem.h"
#include "containers/hashmap.h"
#include "std/errorcodes.h"
#include "std/hash.h"
#include "std/print.h"
#include "std/stdafx.h"
#include "std/string.h"
#include <stdio.h>

nv_errorc _nvsm_load_list_file(nvsm_ctx_t* ctx, nvsm_list_file_t* file);

/* returns NOVA_ERROR_CODE_FILE_NOT_FOUND to indicate the file was not found. This should be handled by the dev appropriately. */
nv_errorc _nvsm_load_cache_file(nvsm_ctx_t* ctx, nvsm_cache_file_t* file);

/* write the cache to disk */
nv_errorc _nvsm_generate_and_write_cache_file(nvsm_list_file_t* file, size_t list_file_mtime, FILE* out_file);

/* remember to move the file seeker to the beginning after youre done */
size_t get_file_max_line_length(FILE* f);

/* ^^ */
size_t count_file_lines(FILE* fp);

#include <time.h>
#ifdef _WIN32
#  include <windows.h>
#else
#  include <sys/stat.h>
#endif

#ifdef _WIN32
size_t
make_timestamp(const SYSTEMTIME* st)
{
  size_t timestamp = 0;

  timestamp |= (size_t)st->wYear << 44;   // 4 bytes
  timestamp |= (size_t)st->wMonth << 40;  // 1 byte
  timestamp |= (size_t)st->wDay << 35;    // 1 byte
  timestamp |= (size_t)st->wHour << 30;   // 1 byte
  timestamp |= (size_t)st->wMinute << 24; // 1 byte
  timestamp |= (size_t)st->wSecond << 18; // 1 byte
  timestamp |= (size_t)st->wMilliseconds; // 2 bytes

  return timestamp;
}
#endif

/* https://qb64phoenix.com/forum/showthread.php?tid=2724&pid=25455#pid25455 */
static inline size_t
get_last_modified_time(const char* filepath)
{
#ifdef _WIN32
  WIN32_FILE_ATTRIBUTE_DATA fileInfo;
  if (GetFileAttributesEx(filepath, GetFileExInfoStandard, &fileInfo))
  {
    FILETIME   ft = fileInfo.ftLastWriteTime;
    SYSTEMTIME st;
    FileTimeToSystemTime(&ft, &st);

    /* we form a size_t from the struct to use */
    return make_timestamp(&st);
  }
  else
  {
    nv_log_error("Failed to get file attributes for %s\n", filepath);
  }

  // TODO: does this work on android? I mean android *is* linux, right?
#else /* unix */
  struct stat attr;
  if (stat(filepath, &attr) == 0)
  {
    return attr.st_mtime;
  }
  else
  {
    nv_log_error("stat");
  }
#endif

  return 0;
}

static inline bool
was_file_modified(const char* filepath, size_t saved_mtime)
{
  return get_last_modified_time(filepath) > saved_mtime;
}

/* https://cboard.cprogramming.com/c-programming/77564-line-counting-post548900.html#post548900 */
size_t
count_file_lines(FILE* fp)
{
  const size_t buffer_size = 256;
  nv_assert(buffer_size <= __INT_MAX__);

  char   buffer[256];
  size_t count = 0;

  while (fgets(buffer, (int)buffer_size, fp) != NULL)
  {
    count++;
  }

  return count;
}

size_t
get_file_max_line_length(FILE* file)
{
  nv_assert_and_ret(file != NULL, 0);

  size_t largest = 0, current = 0;
  int    chr;

  while ((chr = fgetc(file)) != EOF)
  {
    if (chr == '\n')
    {
      if (current > largest)
      {
        largest = current;
      }
      current = 0;
    }
    else
    {
      current++;
    }
  }
  if (current > largest)
  {
    largest = current;
  }
  return largest;
}

static inline int
read_shader_spirv(const char* output, unsigned** spirv, size_t* spirvsize)
{
  FILE* f = fopen(output, "rb");
  if (f == NULL)
  {
    nv_log_error("%s : %s\n", output, strerror(*__errno_location()));
    goto err;
  }

  // I'm sorry i used goto please spare me i have a loving family please no

  NOVA_CALL_FILE_FN(fseek(f, 0, SEEK_END));
  size_t fsize = ftell(f);
  if (fsize == (size_t)-1)
  {
    goto err;
  }
  NOVA_CALL_FILE_FN(fseek(f, 0, SEEK_SET));

  unsigned* buffer = nv_malloc(fsize);
  if (!buffer)
  {
    goto err;
  }

  nv_assert(fread(buffer, 1, fsize, f) != 0);

  NOVA_CALL_FILE_FN(fclose(f));

  *spirv     = buffer;
  *spirvsize = fsize;
  return 0;

err:
  nv_log_error("Could not read in spirv for output path \"%s\"", output);
  if (f)
  {
    NOVA_CALL_FILE_FN(fclose(f));
    *spirv     = NULL;
    *spirvsize = 0;
  }
  return -1;
}

nv_errorc
_nvsm_load_list_file(nvsm_ctx_t* ctx, nvsm_list_file_t* file)
{
  nv_assert_and_ret(ctx != NULL, NOVA_ERROR_CODE_INVALID_ARG);
  nv_assert_and_ret(file != NULL, NOVA_ERROR_CODE_INVALID_ARG);

  const char* list_file_path = ctx->list_file;
  if (!list_file_path)
  {
    list_file_path = "Shaders/shaderlist";
  }

  FILE* list_file = fopen(list_file_path, "r");
  nv_assert_and_ret(list_file != NULL, NOVA_ERROR_CODE_FILE_NOT_FOUND);

  size_t max_line_len = get_file_max_line_length(list_file);
  nv_assert_and_ret(max_line_len != 0, NOVA_ERROR_CODE_IO_ERROR);
  if (fseek(list_file, 0, SEEK_SET) != 0)
  {
    nv_log_error("IO error: %s\n", strerror(errno));
    return NOVA_ERROR_CODE_IO_ERROR;
  }

  size_t num_lines_list_file = count_file_lines(list_file);
  nv_assert_and_ret(num_lines_list_file != 0, NOVA_ERROR_CODE_IO_ERROR);

  if (fseek(list_file, 0, SEEK_SET) != 0)
  {
    nv_log_error("IO error: %s\n", strerror(errno));
    return NOVA_ERROR_CODE_IO_ERROR;
  }

  /* may be wrong (some lines may be garbage), so it is correctly set after the loop */
  file->num_entries = num_lines_list_file;

  file->entries = (nvsm_list_file_entry_t*)nv_calloc(num_lines_list_file * sizeof(nvsm_list_file_entry_t));
  nv_assert_and_ret(file->entries != NULL, NOVA_ERROR_CODE_MALLOC_FAILED);

  char* line = nv_calloc(max_line_len + 1);
  nv_assert_and_ret(line != NULL, NOVA_ERROR_CODE_MALLOC_FAILED);

  nv_assert_and_ret(max_line_len <= __INT_MAX__, NOVA_ERROR_CODE_INVALID_INPUT);

  size_t idx = 0;
  while (fgets(line, (int)max_line_len + 1, list_file) != NULL)
  {
    nvsm_list_file_entry_t* entry = &file->entries[idx];

    /* empty lines */
    if (*line == '\n')
    {
      continue;
    }

    nv_bzero(entry->name, sizeof(entry->name));
    nv_bzero(entry->shader_path, sizeof(entry->shader_path));
    nv_bzero(entry->spirv_path, sizeof(entry->spirv_path));
    nv_bzero(entry->stage, sizeof(entry->stage));

    nv_strlcpy(entry->spirv_path, NVSM_SHADER_SPIRV_DIRNAME, sizeof(entry->spirv_path));
    char buffer[sizeof(entry->spirv_path)];

    /* if we were successful in reading the line, only then continue */
    /* also, ignore the stoopid sheet at the start, it's basically reading everything up to the first colon into the name */
    if (sscanf(line, " %255[^:]: path: %255s output: %255s stage: %s", entry->name, entry->shader_path, buffer, entry->stage) == 4)
    {
      nv_strlcat(entry->spirv_path, buffer, sizeof(entry->spirv_path));
      idx++;
    }
  }

  file->num_entries = idx;

  fclose(list_file);

  return NOVA_SUCCESS;
}

nv_errorc
_nvsm_load_cache_file(nvsm_ctx_t* ctx, nvsm_cache_file_t* file)
{
  nv_assert_and_ret(ctx != NULL, NOVA_ERROR_CODE_INVALID_ARG);
  nv_assert_and_ret(file != NULL, NOVA_ERROR_CODE_INVALID_ARG);

  if (ctx->cache_file_dir)
  {
    nv_assert_and_ret(nv_strlen(ctx->cache_file_dir) < 256, NOVA_ERROR_CODE_INVALID_ARG);
  }

  if (!ctx->cache_file_dir)
  {
    ctx->cache_file_dir = ".";
  }

  char cache_file_path[256] = {};
  nv_strlcpy(cache_file_path, ctx->cache_file_dir, sizeof(cache_file_path));
  nv_strlcat(cache_file_path, "/" NVSM_CACHE_FILENAME, sizeof(cache_file_path));

  /* the canary that should be in the cache file, if it isn't, then the cache file is an impostor (sus) */
  const u32 canary = 0xDEADBEEF;

  FILE* cache_file = fopen(cache_file_path, "r");
  if (cache_file == NULL)
  {
    nv_log_info("No cache file.\n");
    return NOVA_ERROR_CODE_FILE_NOT_FOUND;
  }

  u32 file_canary = 0;
  if (fread(&file_canary, sizeof(canary), 1, cache_file) != 1 || file_canary != canary)
  {
    return NOVA_ERROR_CODE_INVALID_CACHE;
  }

  const char* list_file_path = ctx->list_file;
  if (!list_file_path)
  {
    list_file_path = "Shaders/shaderlist";
  }

  // the mtime of the list file to check if it itself has been modified.
  // If the list has been modified, then this cache is obviously out of date
  size_t list_file_mtime = 0;
  if (fread(&list_file_mtime, sizeof(list_file_mtime), 1, cache_file) != 1 || list_file_mtime != get_last_modified_time(list_file_path))
  {
    return NOVA_ERROR_CODE_INVALID_CACHE;
  }

  /* after the canary and the list file modtime is the number of entries so we read that */

  /* currently, the number of *expected* entries, not number of valid entries */
  if ((fread(&file->num_entries, sizeof(file->num_entries), 1, cache_file) != 1) || (file->num_entries == 0))
  {
    return NOVA_ERROR_CODE_INVALID_CACHE;
  }

  file->entries = (nvsm_cache_file_entry_t*)nv_calloc(file->num_entries * sizeof(nvsm_cache_file_entry_t));
  nv_assert_and_ret(file->entries != NULL, NOVA_ERROR_CODE_MALLOC_FAILED);

  /* we did not read as many entries as the header reported. Maybe the file wasn't written fully when the program terminated. */
  if (fread(file->entries, sizeof(nvsm_cache_file_entry_t), file->num_entries, cache_file) != file->num_entries)
  {
    fclose(cache_file);
    return NOVA_ERROR_CODE_INVALID_CACHE;
  }

  fclose(cache_file);

  return NOVA_SUCCESS;
}

nv_errorc
_nvsm_generate_and_write_cache_file(nvsm_list_file_t* file, size_t list_file_mtime, FILE* out_file)
{
  nv_assert_and_ret(file != NULL, NOVA_ERROR_CODE_INVALID_ARG);
  nv_assert_and_ret(out_file != NULL, NOVA_ERROR_CODE_INVALID_ARG);
  nv_assert_and_ret(file->num_entries != 0, NOVA_ERROR_CODE_INVALID_ARG);
  nv_assert_and_ret(file->entries != NULL, NOVA_ERROR_CODE_INVALID_ARG);

  const u32 canary = 0xDEADBEEF;

  /* write a canary for protection */
  if (fwrite(&canary, sizeof(u32), 1, out_file) != 1)
  {
    return NOVA_ERROR_CODE_IO_ERROR;
  }

  if (fwrite(&list_file_mtime, sizeof(list_file_mtime), 1, out_file) != 1)
  {
    return NOVA_ERROR_CODE_IO_ERROR;
  }

  if (fwrite(&file->num_entries, sizeof(file->num_entries), 1, out_file) != 1)
  {
    return NOVA_ERROR_CODE_IO_ERROR;
  }

  for (size_t idx = 0; idx < file->num_entries; idx++)
  {
    const nvsm_list_file_entry_t* entry = &file->entries[idx];

    nvsm_cache_file_entry_t cache_converted = nv_zero_init(nvsm_cache_file_entry_t);
    nv_strlcpy(cache_converted.name, entry->name, sizeof(cache_converted.name));
    cache_converted.last_mod_time = get_last_modified_time(entry->shader_path);

    if (fwrite(&cache_converted, sizeof(cache_converted), 1, out_file) != 1)
    {
      return NOVA_ERROR_CODE_IO_ERROR;
    }
  }

  return NOVA_SUCCESS;
}

#ifdef _WIN32
#  include <direct.h>
#  define MKDIR(path) _mkdir(path)
#  define PATH_SEP '\\'
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#  define MKDIR(path) (mkdir(path, 0777))
#  define PATH_SEP '/'
#endif

static inline void
create_parent_dirs(const char path[256])
{
  char* last_separator = nv_strrchr(path, PATH_SEP);
  if (last_separator != NULL)
  {
    *last_separator = '\0';

    char buffer[256];

    nv_strcpy(buffer, path);
    size_t len = nv_strlen(buffer);

    if (buffer[len - 1] == PATH_SEP)
    {
      buffer[len - 1] = 0;
    }

    for (char* p = buffer + 1; *p; p++)
    {
      if (*p == PATH_SEP)
      {
        *p = 0;
        MKDIR(buffer);
        *p = PATH_SEP;
      }
    }

    MKDIR(buffer);
  }
}

static inline nv_errorc
_nvsm_compile_shader(const char* shader_compiler, const char* shader_compiler_args, const char* shader_path, const char* spirv_path, const char stage[4])
{
  char buffer[1024]  = {};
  char buffer2[1024] = {};
  nv_strlcpy(buffer, spirv_path, sizeof(buffer));
  create_parent_dirs(buffer);

  nv_memset(buffer, 0, sizeof(buffer));

  if (!shader_compiler)
  {
    shader_compiler = "glslangValidator";
  }

  if (shader_compiler_args)
  {
    nv_strlcpy(buffer2, shader_compiler_args, sizeof(buffer2));
  }
  nv_strlcat(buffer2, "-V", sizeof(buffer2));

  size_t written = nv_snprintf(buffer, sizeof(buffer), "%s %s %s -o %s -S %.4s", shader_compiler, buffer2, shader_path, spirv_path, stage);
  if (written != nv_strlen(buffer))
  {
    nv_log_error(
        "Could not fit the command into the buffer %p of size %zu. Could you resize the shader path / spirv path / the compiler args to a smaller size?\n",
        buffer,
        sizeof(buffer));
  }

  buffer[sizeof(buffer) - 1] = '\0';

  if (system(buffer) != 0)
  {
    return NOVA_ERROR_CODE_UNKNOWN;
  }

  return 0;
}

static inline nv_errorc
_nvsm_default_compile_no_cache(nvsm_ctx_t* ctx, nvsm_list_file_t* list_file)
{
  nv_errorc code = NOVA_SUCCESS;
  for (size_t list_i = 0; list_i < list_file->num_entries; list_i++)
  {
    const nvsm_list_file_entry_t* list_entry = &list_file->entries[list_i];

    if ((code = _nvsm_compile_shader(ctx->shader_compiler, ctx->shader_compiler_args, list_entry->shader_path, list_entry->spirv_path, list_entry->stage)) != NOVA_SUCCESS)
    {
      return code;
    }

    nv_hashmap_insert_or_replace(&ctx->shader_map, list_entry->name, &list_file->entries[list_i], NULL);
  }
  return NOVA_SUCCESS;
}

static inline nv_errorc
_nvsm_default_compile_with_cache(nvsm_ctx_t* ctx, nvsm_list_file_t* list_file, nvsm_cache_file_t* cache_file)
{
  nv_errorc code = NOVA_SUCCESS;

  for (size_t list_i = 0; list_i < list_file->num_entries; list_i++)
  {
    const nvsm_list_file_entry_t* list_entry = &list_file->entries[list_i];

    nv_hashmap_insert_or_replace(&ctx->shader_map, list_entry->name, &list_file->entries[list_i], NULL);

    bool wasnt_modified = false;
    for (size_t cache_i = 0; cache_i < cache_file->num_entries; cache_i++)
    {
      const nvsm_cache_file_entry_t* cache_entry = &cache_file->entries[cache_i];
      if (nv_strncmp(list_entry->name, cache_entry->name, NV_MIN(sizeof(list_entry->name), sizeof(cache_entry->name))) == nv_strequal)
      {
        if (!was_file_modified(list_entry->shader_path, cache_entry->last_mod_time))
        {
          wasnt_modified = true;
          break;
        }

        if ((code = _nvsm_compile_shader(ctx->shader_compiler, ctx->shader_compiler_args, list_entry->shader_path, list_entry->spirv_path, list_entry->stage)) != NOVA_SUCCESS)
        {
          return code;
        }
      }
    }

    if (wasnt_modified)
    {
      continue;
    }
  }
  return NOVA_SUCCESS;
}

nv_errorc
nvsm_compile_shaders(nvsm_ctx_t* ctx)
{
  nv_assert_and_ret(ctx != NULL, NOVA_ERROR_CODE_INVALID_ARG);

  const char* list_file_path = ctx->list_file;
  if (!list_file_path)
  {
    list_file_path = "Shaders/shaderlist";
  }

  nv_errorc code = NOVA_ERROR_CODE_SUCCESS;

  nvsm_list_file_t  list_file  = nv_zero_init(nvsm_list_file_t);
  nvsm_cache_file_t cache_file = nv_zero_init(nvsm_cache_file_t);

  if ((code = _nvsm_load_list_file(ctx, &list_file)) != NOVA_SUCCESS)
  {
    return code;
  }

  code = _nvsm_load_cache_file(ctx, &cache_file);
  if (code == NOVA_ERROR_CODE_FILE_NOT_FOUND || code == NOVA_ERROR_CODE_INVALID_CACHE)
  {
    // continue without cache
    if ((code = _nvsm_default_compile_no_cache(ctx, &list_file)) != NOVA_SUCCESS)
    {
      return code;
    }
  }
  else if (code != NOVA_SUCCESS)
  {
    return code;
  }
  else
  {
    /* we have the cache in this branch */
    if ((code = _nvsm_default_compile_with_cache(ctx, &list_file, &cache_file)) != NOVA_SUCCESS)
    {
      return code;
    }
  }
  if (!ctx->cache_file_dir)
  {
    ctx->cache_file_dir = ".";
  }

  char cache_file_path[256] = {};
  nv_strlcpy(cache_file_path, ctx->cache_file_dir, sizeof(cache_file_path));
  nv_strlcat(cache_file_path, "/" NVSM_CACHE_FILENAME, sizeof(cache_file_path));

  FILE* generated_cache_file = fopen(cache_file_path, "wb");
  nv_assert_and_ret(generated_cache_file != NULL, NOVA_ERROR_CODE_IO_ERROR);

  _nvsm_generate_and_write_cache_file(&list_file, get_last_modified_time(list_file_path), generated_cache_file);

  fclose(generated_cache_file);

  return NOVA_SUCCESS;
}

nv_errorc
nvsm_create_shader_modules(nvsm_ctx_t* ctx)
{
  nv_assert_and_ret(ctx != NULL, NOVA_ERROR_CODE_INVALID_ARG);
  nv_assert_and_ret(nv_hashmap_size(&ctx->shader_map) != 0, NOVA_ERROR_CODE_INVALID_ARG);

  size_t             _hashmap_iter = 0;
  nv_hashmap_node_t* node          = NULL;
  while ((node = nv_hashmap_iterate(&ctx->shader_map, &_hashmap_iter)) != NULL)
  {
    nvsm_list_file_entry_t* entry = (nvsm_list_file_entry_t*)node->value;
    if (!entry)
    {
      continue;
    }

    unsigned* spirv      = NULL;
    size_t    spirv_size = 0;
    if (read_shader_spirv(entry->spirv_path, &spirv, &spirv_size) != 0 || !spirv || spirv_size == 0)
    {
      continue;
    }

    const VkShaderModuleCreateInfo info = {
      .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = spirv_size,
      .pCode    = spirv,
    };
    nvvk_result_check(vkCreateShaderModule(nvvk_context.device, &info, NOVA_VK_ALLOCATOR, &entry->module));

    nv_free((void*)spirv);
  }

  return NOVA_SUCCESS;
}

nv_errorc
nvsm_load_shader(nvsm_ctx_t* ctx, const char* name, nvsm_shader_t** out)
{
  nv_assert_and_ret(name != NULL, NOVA_ERROR_CODE_INVALID_ARG);
  nv_assert_and_ret(out != NULL, NOVA_ERROR_CODE_INVALID_ARG);

  nvsm_shader_t* entry = nv_hashmap_find(&ctx->shader_map, name, NULL);
  if (!entry)
  {
    return NOVA_ERROR_CODE_INVALID_RETVAL;
  }

  *out = (nvsm_shader_t*)entry;

  return NOVA_SUCCESS;
}

nv_errorc
nvsm_compile_shaders_force(nvsm_ctx_t* ctx, bool generate_cache)
{
  nv_assert_and_ret(ctx != NULL, NOVA_ERROR_CODE_INVALID_ARG);

  const char* list_file_path = ctx->list_file;
  if (!list_file_path)
  {
    list_file_path = "Shaders/shaderlist";
  }

  nv_errorc code = NOVA_ERROR_CODE_SUCCESS;

  nvsm_list_file_t list_file = nv_zero_init(nvsm_list_file_t);

  if ((code = _nvsm_load_list_file(ctx, &list_file)) != NOVA_SUCCESS)
  {
    return code;
  }

  if ((code = _nvsm_default_compile_no_cache(ctx, &list_file)) != NOVA_SUCCESS)
  {
    return code;
  }

  if (generate_cache)
  {
    if (!ctx->cache_file_dir)
    {
      ctx->cache_file_dir = ".";
    }

    char cache_file_path[256] = {};
    nv_strlcpy(cache_file_path, ctx->cache_file_dir, sizeof(cache_file_path));
    nv_strlcat(cache_file_path, "/" NVSM_CACHE_FILENAME, sizeof(cache_file_path));

    FILE* generated_cache_file = fopen(cache_file_path, "wb");
    nv_assert_and_ret(generated_cache_file != NULL, NOVA_ERROR_CODE_IO_ERROR);

    _nvsm_generate_and_write_cache_file(&list_file, get_last_modified_time(list_file_path), generated_cache_file);

    fclose(generated_cache_file);
  }

  return NOVA_SUCCESS;
}

VkShaderStageFlags
_nvsm_shader_stage_from_string(const char stage[4])
{
  if (nv_strncmp(stage, "vert", 4) == 0)
  {
    return VK_SHADER_STAGE_VERTEX_BIT;
  }
  else if (nv_strncmp(stage, "frag", 4) == 0)
  {
    return VK_SHADER_STAGE_FRAGMENT_BIT;
  }
  else if (nv_strncmp(stage, "tese", 4) == 0)
  {
    return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
  }
  else if (nv_strncmp(stage, "tesc", 4) == 0)
  {
    return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
  }
  else if (nv_strncmp(stage, "geom", 4) == 0)
  {
    return VK_SHADER_STAGE_GEOMETRY_BIT;
  }
  else if (nv_strncmp(stage, "comp", 4) == 0)
  {
    return VK_SHADER_STAGE_COMPUTE_BIT;
  }
  return (VkShaderStageFlags)-1;
}

nv_errorc
nvsm_init(nvsm_ctx_t* ctx)
{
  nv_assert_and_ret(ctx != NULL, NOVA_ERROR_CODE_INVALID_ARG);

  nv_bzero(ctx, sizeof(nvsm_ctx_t));

  nv_errorc code = nv_hashmap_init(16, sizeof(const char*), sizeof(nvsm_list_file_entry_t), nv_hash_fnv1a_string, nv_allocator_get_default(), &ctx->shader_map);
  if (code != NOVA_SUCCESS)
  {
    return code;
  }

  return NOVA_SUCCESS;
}

void
nvsm_shutdown(nvsm_ctx_t* ctx)
{
  nv_assert_and_ret(ctx != NULL, );

  size_t             _hashmap_iter = 0;
  nv_hashmap_node_t* node          = NULL;
  while ((node = nv_hashmap_iterate(&ctx->shader_map, &_hashmap_iter)) != NULL)
  {
    nvsm_list_file_entry_t* entry = (nvsm_list_file_entry_t*)node->value;
    if (entry->module != VK_NULL_HANDLE)
    {
      vkDestroyShaderModule(nvvk_context.device, entry->module, NOVA_VK_ALLOCATOR);
    }
  }

  nv_hashmap_destroy(&ctx->shader_map);
}
