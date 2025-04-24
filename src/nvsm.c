#include "engine/nvsm.h"
#include "GPU/pipeline.h"
#include "GPU/types.h"
#include "GPU/vk.h"
#include "std/alloc.h"
#include "std/containers/hashmap.h"
#include "std/errorcodes.h"
#include "std/hash.h"
#include "std/stdafx.h"
#include "std/string.h"

#include "GPU/shaderresource.h"
#include "external/spirv_reflect/spirv_reflect.h"

/* Go to the programmatic interface C (new) section of the page at https://github.com/KhronosGroup/glslang/ */
#include "external/glslang/glslang/Include/glslang_c_interface.h"
#include "external/glslang/glslang/Include/glslang_c_shader_types.h"
#include "external/glslang/glslang/Public/resource_limits_c.h"

#include <SDL2/SDL_rwops.h>
#include <stdio.h>
#include <vulkan/vulkan_core.h>

glslang_stage_t _nvsm_glslang_shader_stage_from_string(const char stage[4]);

nv_error _nvsm_load_list_file(nvsm_ctx_t* ctx, nvsm_list_file_t* file);

/* returns NV_ERROR_FILE_NOT_FOUND to indicate the file was not found. This should be handled by the dev appropriately. */
nv_error _nvsm_load_cache_file(nvsm_ctx_t* ctx, nvsm_cache_file_t* file);

/* write the cache to disk */
nv_error _nvsm_generate_and_write_cache_file(const nvsm_list_file_t* file, size_t list_file_mtime, FILE* out_file);

/* remember to move the file seeker to the beginning after youre done */
size_t get_file_max_line_length(FILE* f);

/* ^^ */
size_t count_file_lines(FILE* fp);

static inline void
_nvsm_get_cache_file_path(const char* cache_file_dir, char buffer[256])
{
  /**
   * WARNING: If this check isn't performed, then ctx->cache_file_dir will be NULL and the program will try writing to the root directory (/.nvsmcache)
   */
  if (!cache_file_dir)
  {
    /**
     * This will change the value of cache_file_path to ./.nvsmcache
     * note the leading .
     */
    cache_file_dir = ".";
  }

  /**
   * Too many bytes to fit on the buffer
   * Is invalid arg correct here?
   */
  nv_assert_else_return(nv_strlen(cache_file_dir) < 256, );

  nv_strlcpy(buffer, cache_file_dir, 256);
  nv_strlcat(buffer, NVSM_CACHE_FILENAME, 256);
}

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
  size_t count = 0;

  int chr = 0;
  while ((chr = fgetc(fp)) != EOF)
  {
    if (chr == '\n')
    {
      count++;
    }
  }

  return count;
}

size_t
get_file_max_line_length(FILE* file)
{
  nv_assert_else_return(file != NULL, 0);

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

static inline nvsm_compile_options_t
_nvsm_get_default_compile_options(void)
{
  nvsm_compile_options_t opts = nv_zero_init(nvsm_compile_options_t);
#if NVSM_SHADERS_ENABLE_DEBUGGING
  opts.enable_debug_mode = true;
#else
  opts.enable_optimizations      = true;
  opts.enable_size_optimizations = true;
#endif
  opts.validate = true;
  return opts;
}

static inline nv_error
read_shader_spirv(const char* spirv_path, nvsm_spirv_binary_t* bin)
{
  SDL_RWops* rw = SDL_RWFromFile(spirv_path, "rb");
  if (rw == NULL)
  {
    nv_log_error("%s : %s\n", spirv_path, SDL_GetError());
    return NV_ERROR_FILE_NOT_FOUND;
  }

  bin->byte_count = SDL_RWsize(rw);
  if ((Sint64)bin->byte_count <= 0)
  {
    nv_log_error("Error in read size of stream %p : %s\n", rw, SDL_GetError());
    SDL_RWclose(rw);
    return NV_ERROR_IO_ERROR;
  }

  bin->words = (uint32_t*)nv_calloc(bin->byte_count);
  nv_assert_else_return(bin->words != NULL, NV_ERROR_MALLOC_FAILED);

  /* SDL_RWread returns 0 if an error occured or if the entire stream was read. */
  if (SDL_RWread(rw, (void*)bin->words, 1, bin->byte_count) == 0)
  {
    nv_log_error("Error in read of stream %p (err:%s)\n", rw, SDL_GetError());
    bin->words      = NULL;
    bin->byte_count = 0;
    SDL_RWclose(rw);
    return NV_ERROR_IO_ERROR;
  }

  SDL_RWclose(rw);
  return NV_SUCCESS;
}

nv_error
_nvsm_load_list_file(nvsm_ctx_t* ctx, nvsm_list_file_t* file)
{
  nv_assert_else_return(ctx != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(file != NULL, NV_ERROR_INVALID_ARG);

  const char* list_file_path = ctx->list_file;
  if (!list_file_path)
  {
    list_file_path = "Shaders/shaderlist";
  }

  FILE* list_file = fopen(list_file_path, "r");
  nv_assert_else_return(list_file != NULL, NV_ERROR_FILE_NOT_FOUND);

  size_t max_line_len = get_file_max_line_length(list_file);
  nv_assert_else_return(max_line_len != 0, NV_ERROR_IO_ERROR);
  if (fseek(list_file, 0, SEEK_SET) != 0)
  {
    nv_log_error("IO error: %s\n", strerror(errno));
    return NV_ERROR_IO_ERROR;
  }

  size_t num_lines_list_file = count_file_lines(list_file);
  nv_assert_else_return(num_lines_list_file != 0, NV_ERROR_IO_ERROR);

  if (fseek(list_file, 0, SEEK_SET) != 0)
  {
    nv_log_error("IO error: %s\n", strerror(errno));
    return NV_ERROR_IO_ERROR;
  }

  /* may be wrong (some lines may be garbage/empty), so it is correctly set after the loop */
  file->num_entries = num_lines_list_file;

  file->entries = (nvsm_list_file_entry_t*)nv_calloc(num_lines_list_file * sizeof(nvsm_list_file_entry_t));
  nv_assert_else_return(file->entries != NULL, NV_ERROR_MALLOC_FAILED);

  char* line = nv_calloc(max_line_len + 1);
  nv_assert_else_return(line != NULL, NV_ERROR_MALLOC_FAILED);

  nv_assert_else_return(max_line_len <= __INT_MAX__, NV_ERROR_INVALID_INPUT);

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

    char spirv_path_read[sizeof(entry->spirv_path)];

    /* if we were successful in reading the line, only then continue */
    /* also, ignore the stoopid sheet at the start, it's basically reading everything up to the first colon into the name */
    if (sscanf(line, " %255[^:]: path: %255s output: %255s stage: %s", entry->name, entry->shader_path, spirv_path_read, entry->stage) == 4)
    {
      nv_strlcat(entry->spirv_path, spirv_path_read, sizeof(entry->spirv_path));
      idx++;
    }
  }

  file->num_entries = idx;

  fclose(list_file);
  nv_free(line);

  return NV_SUCCESS;
}

nv_error
_nvsm_load_cache_file(nvsm_ctx_t* ctx, nvsm_cache_file_t* file)
{
  nv_assert_else_return(ctx != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(file != NULL, NV_ERROR_INVALID_ARG);

  if (ctx->cache_file_dir)
  {
    nv_assert_else_return(nv_strlen(ctx->cache_file_dir) < 256, NV_ERROR_INVALID_ARG);
  }

  char cache_file_path[256] = {};
  _nvsm_get_cache_file_path(ctx->cache_file_dir, cache_file_path);

  /* the canary that should be in the cache file, if it isn't, then the cache file is an impostor (sus) */
  const u32 canary = 0xDEADBEEF;

  FILE* cache_file = fopen(cache_file_path, "r");
  if (cache_file == NULL)
  {
    nv_log_info("No cache file.\n");
    return NV_ERROR_FILE_NOT_FOUND;
  }

  u32 file_canary = 0;
  if (fread(&file_canary, sizeof(canary), 1, cache_file) != 1 || file_canary != canary)
  {
    fclose(cache_file);
    return NV_ERROR_INVALID_CACHE;
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
    fclose(cache_file);
    return NV_ERROR_INVALID_CACHE;
  }

  /* after the canary and the list file modtime is the number of entries so we read that */

  /* currently, the number of *expected* entries, not number of valid entries */
  if ((fread(&file->num_entries, sizeof(file->num_entries), 1, cache_file) != 1) || (file->num_entries == 0))
  {
    fclose(cache_file);
    return NV_ERROR_INVALID_CACHE;
  }

  file->entries = (nvsm_cache_file_entry_t*)nv_calloc(file->num_entries * sizeof(nvsm_cache_file_entry_t));
  if (file->entries == NULL)
  {
    nv_assert(fclose(cache_file) == 0);
    nv_assert_else_return(false, NV_ERROR_MALLOC_FAILED);
  }

  /* we did not read as many entries as the header reported. Maybe the file wasn't written fully when the program terminated. */
  if (fread(file->entries, sizeof(nvsm_cache_file_entry_t), file->num_entries, cache_file) != file->num_entries)
  {
    fclose(cache_file);
    return NV_ERROR_INVALID_CACHE;
  }

  fclose(cache_file);

  return NV_SUCCESS;
}

static inline void
_nvsm_destroy_cache_file(nvsm_cache_file_t* file)
{
  if (file->entries)
  {
    nv_assert_else_return(file->num_entries != 0, );
    nv_free(file->entries);
  }
}

static inline void
_nvsm_destroy_list_file(nvsm_list_file_t* file)
{
  if (file->entries)
  {
    nv_assert_else_return(file->num_entries != 0, );
    nv_free(file->entries);
  }
}

nv_error
_nvsm_generate_and_write_cache_file(const nvsm_list_file_t* file, size_t list_file_mtime, FILE* out_file)
{
  nv_assert_else_return(file != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(out_file != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(file->num_entries != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(file->entries != NULL, NV_ERROR_INVALID_ARG);

  const u32 canary = 0xDEADBEEF;

  size_t fwrite_return = 0;

  /* write a canary for protection */
  fwrite_return = fwrite(&canary, sizeof(u32), 1, out_file);
  if (fwrite_return != 1)
  {
    return NV_ERROR_IO_ERROR;
  }

  fwrite_return = fwrite(&list_file_mtime, sizeof(list_file_mtime), 1, out_file);
  if (fwrite_return != 1)
  {
    return NV_ERROR_IO_ERROR;
  }

  fwrite_return = fwrite(&file->num_entries, sizeof(file->num_entries), 1, out_file);
  if (fwrite_return != 1)
  {
    return NV_ERROR_IO_ERROR;
  }

  nvsm_cache_file_entry_t* cache_converted_entries = (nvsm_cache_file_entry_t*)nv_calloc(sizeof(nvsm_cache_file_entry_t) * file->num_entries);
  nv_assert_else_return(cache_converted_entries != NULL, NV_ERROR_MALLOC_FAILED);

  for (size_t idx = 0; idx < file->num_entries; idx++)
  {
    const nvsm_list_file_entry_t* entry   = &file->entries[idx];
    nvsm_cache_file_entry_t*      convert = &cache_converted_entries[idx];

    *convert = nv_zero_init(nvsm_cache_file_entry_t);

    convert->last_mod_time    = get_last_modified_time(entry->shader_path);
    convert->hash_name        = NVSM_STRING_HASH_FUNCTION((void*)entry->name, 0, NULL);
    convert->hash_shader_path = NVSM_STRING_HASH_FUNCTION((void*)entry->shader_path, 0, NULL);
  }

  if (fwrite(cache_converted_entries, sizeof(nvsm_cache_file_entry_t), file->num_entries, out_file) != file->num_entries)
  {
    return NV_ERROR_IO_ERROR;
  }

  nv_free(cache_converted_entries);

  return NV_SUCCESS;
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

/* why was this function modifying a const variable? */
static inline void
create_parent_dirs(const char path[256])
{
  char path_copy[256];
  nv_strlcpy(path_copy, path, sizeof(path_copy));

  char* last_separator = nv_strrchr(path_copy, PATH_SEP);
  if (last_separator != NULL)
  {
    *last_separator = '\0';

    char buffer[256];

    nv_strcpy(buffer, path_copy);
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

static inline nv_error
_nvsm_dump_shader(const nvsm_spirv_binary_t* bin, const char* out_filename)
{
  nv_assert_else_return(bin != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(out_filename != NULL, NV_ERROR_INVALID_ARG);

  /**
   * Does SDL create parent directories?
   * Just to be safe
   */
  create_parent_dirs(out_filename);

  SDL_RWops* rw = SDL_RWFromFile(out_filename, "wb");
  nv_assert_and_exec(rw != NULL, nv_log_error("SDL reports %s\n", SDL_GetError()); return NV_ERROR_IO_ERROR;);

  if (SDL_RWwrite(rw, bin->words, 1, bin->byte_count) != bin->byte_count)
  {
    nv_log_error("Could not dump SPIRV binary to disk");
    return NV_ERROR_IO_ERROR;
  }

  SDL_RWclose(rw);

  return NV_SUCCESS;
}

static inline nv_error
_nvsm_read_shader_file_null_terminated(const char* file_path, char** dst, size_t* dst_size)
{
  nv_assert_else_return(file_path != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst_size != NULL, NV_ERROR_INVALID_ARG);

  /**
   * SDL_LoadFile null terminates the file data by default, we do not have to do anything
   */
  void* shader_file_data = SDL_LoadFile(file_path, dst_size);
  nv_assert_else_return(shader_file_data != NULL, NV_ERROR_IO_ERROR);
  nv_assert_else_return(*dst_size != 0, NV_ERROR_IO_ERROR);

  *dst = (char*)shader_file_data;
  // dst_size is set by sdl

  return NV_SUCCESS;
}

glslang_stage_t
_nvsm_glslang_shader_stage_from_string(const char stage[4])
{
  if (nv_strncmp(stage, "vert", 4) == 0)
  {
    return GLSLANG_STAGE_VERTEX;
  }
  else if (nv_strncmp(stage, "frag", 4) == 0)
  {
    return GLSLANG_STAGE_FRAGMENT;
  }
  else if (nv_strncmp(stage, "tese", 4) == 0)
  {
    return GLSLANG_STAGE_TESSEVALUATION;
  }
  else if (nv_strncmp(stage, "tesc", 4) == 0)
  {
    return GLSLANG_STAGE_TESSCONTROL;
  }
  else if (nv_strncmp(stage, "geom", 4) == 0)
  {
    return GLSLANG_STAGE_GEOMETRY;
  }
  else if (nv_strncmp(stage, "comp", 4) == 0)
  {
    return GLSLANG_STAGE_COMPUTE;
  }
  return GLSLANG_STAGE_COUNT;
}

static inline nv_error
_nvsm_compile_shader(const char* shader_path, const nvsm_compile_options_t* opts, const char* spirv_path, const char stage[4], nvsm_spirv_binary_t* bin, bool dump)
{
  nv_assert_else_return(shader_path != NULL, NV_ERROR_INVALID_ARG);
  if (dump)
  {
    nv_assert_else_return(spirv_path != NULL, NV_ERROR_INVALID_ARG);
  }
  nv_assert_else_return(stage != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(bin != NULL, NV_ERROR_INVALID_ARG);

  nv_error code = NV_SUCCESS;

  char*  shader_source = NULL;
  size_t shader_size   = 0;
  if ((code = _nvsm_read_shader_file_null_terminated(shader_path, &shader_source, &shader_size)) != NV_SUCCESS)
  {
    return code;
  }

  const glslang_input_t input = {
    .language                          = GLSLANG_SOURCE_GLSL,
    .stage                             = _nvsm_glslang_shader_stage_from_string(stage),
    .client                            = GLSLANG_CLIENT_VULKAN,
    .client_version                    = GLSLANG_TARGET_VULKAN_1_0,
    .target_language                   = GLSLANG_TARGET_SPV,
    .target_language_version           = GLSLANG_TARGET_SPV_1_0,
    .code                              = (const char*)shader_source,
    .default_version                   = 450,
    .default_profile                   = GLSLANG_CORE_PROFILE,
    .force_default_version_and_profile = true,
    .forward_compatible                = false,
    .messages                          = GLSLANG_MSG_DEFAULT_BIT,
    .resource                          = glslang_default_resource(),
  };

  glslang_shader_t* shader = glslang_shader_create(&input);
  nv_assert_else_return(shader != NULL, NV_ERROR_EXTERNAL);

  *bin = nv_zero_init(nvsm_spirv_binary_t);

  if (!glslang_shader_preprocess(shader, &input))
  {
    nv_log_error("GLSL preprocessing failed %s\n", shader_path);
    nv_log_error("%s\n", glslang_shader_get_info_log(shader));
    nv_log_error("%s\n", glslang_shader_get_info_debug_log(shader));
    nv_log_error("%s\n", input.code);
    glslang_shader_delete(shader);
    return NV_ERROR_INVALID_INPUT;
  }

  if (!glslang_shader_parse(shader, &input))
  {
    nv_log_error("GLSL parsing failed %s\n", shader_path);
    nv_log_error("%s\n", glslang_shader_get_info_log(shader));
    nv_log_error("%s\n", glslang_shader_get_info_debug_log(shader));
    nv_log_error("%s\n", glslang_shader_get_preprocessed_code(shader));
    glslang_shader_delete(shader);
    return NV_ERROR_INVALID_INPUT;
  }

  glslang_program_t* program = glslang_program_create();
  glslang_program_add_shader(program, shader);

  if (!glslang_program_link(program, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT))
  {
    nv_log_error("GLSL linking failed %s\n", shader_path);
    nv_log_error("%s\n", glslang_program_get_info_log(program));
    nv_log_error("%s\n", glslang_program_get_info_debug_log(program));
    glslang_program_delete(program);
    glslang_shader_delete(shader);
    return NV_ERROR_INVALID_INPUT;
  }

  glslang_spv_options_t spirv_options = nv_zero_init(glslang_spv_options_t);
  spirv_options.disable_optimizer     = !opts->enable_optimizations;
  spirv_options.optimize_size         = opts->enable_size_optimizations;
  spirv_options.generate_debug_info   = opts->enable_debug_mode;
  spirv_options.strip_debug_info      = !opts->enable_debug_mode;
  spirv_options.validate              = opts->validate;

  glslang_program_SPIRV_generate_with_options(program, _nvsm_glslang_shader_stage_from_string(stage), &spirv_options);

  bin->byte_count = glslang_program_SPIRV_get_size(program) * sizeof(uint32_t);
  bin->words      = nv_malloc(bin->byte_count);
  nv_assert_else_return(bin->words != NULL, NV_ERROR_MALLOC_FAILED);

  glslang_program_SPIRV_get(program, bin->words);

  const char* spirv_messages = glslang_program_SPIRV_get_messages(program);
  if (spirv_messages)
  {
    nv_log_info("(%s) %s\n", shader_path, spirv_messages);
  }

  glslang_program_delete(program);
  glslang_shader_delete(shader);

  if (dump)
  {
    code = _nvsm_dump_shader(bin, spirv_path);
    if (code == NV_SUCCESS)
    {
      nv_log_verbose("%s => %s: dumped\n", shader_path, spirv_path);
    }
    else
    {
      return code;
    }
  }

  return NV_SUCCESS;
}

static inline nv_error
_nvsm_default_compile_no_cache(nvsm_ctx_t* ctx, nvsm_list_file_t* list_file)
{
  nvsm_compile_options_t compile_options = _nvsm_get_default_compile_options();
  if (ctx->enable_custom_spirv_options)
  {
    compile_options = ctx->custom_options;
  }

  nv_error code = NV_SUCCESS;
  for (size_t list_i = 0; list_i < list_file->num_entries; list_i++)
  {
    nvsm_list_file_entry_t* list_entry = &list_file->entries[list_i];

    if ((code = _nvsm_compile_shader(list_entry->shader_path, &compile_options, list_entry->spirv_path, list_entry->stage, &list_entry->bin, true)) != NV_SUCCESS)
    {
      continue;
    }

    nv_hashmap_insert_or_replace(&ctx->shader_map, list_entry->name, &list_file->entries[list_i], NULL);
  }
  return code;
}

static inline nv_error
_nvsm_default_compile_with_cache(nvsm_ctx_t* ctx, nvsm_list_file_t* list_file, nvsm_cache_file_t* cache_file)
{
  nv_error code = NV_SUCCESS;

  nvsm_compile_options_t compile_options = _nvsm_get_default_compile_options();
  if (ctx->enable_custom_spirv_options)
  {
    compile_options = ctx->custom_options;
  }

  for (size_t list_i = 0; list_i < list_file->num_entries; list_i++)
  {
    nvsm_list_file_entry_t* list_entry       = &list_file->entries[list_i];
    const size_t            hash_name        = NVSM_STRING_HASH_FUNCTION(list_entry->name, 0, NULL);
    const size_t            hash_shader_path = NVSM_STRING_HASH_FUNCTION(list_entry->shader_path, 0, NULL);

    /*
      Allow for breaking of the outer loop if we know we don't need to process the shader anymore
    */
    bool wasnt_modified = false;
    for (size_t cache_i = 0; cache_i < cache_file->num_entries; cache_i++)
    {
      const nvsm_cache_file_entry_t* cache_entry = &cache_file->entries[cache_i];
      if (hash_name == cache_entry->hash_name && hash_shader_path == cache_entry->hash_shader_path)
      {
        if (!was_file_modified(list_entry->shader_path, cache_entry->last_mod_time))
        {
          /* this is possibly a stupid idea */
          code = read_shader_spirv(list_entry->spirv_path, &list_entry->bin);
          if (code != NV_SUCCESS)
          {
            nv_log_error("Failed to read shader %s\n", list_entry->shader_path);
          }
          wasnt_modified = true;
          break;
        }

        nv_bzero(&list_entry->bin, sizeof(nvsm_spirv_binary_t));
        code = _nvsm_compile_shader(list_entry->shader_path, &compile_options, list_entry->spirv_path, list_entry->stage, &list_entry->bin, true);
        if (code != NV_SUCCESS)
        {
          nv_log_error("Failed to compile shader %s\n", list_entry->shader_path);
        }

        if (list_entry->bin.words == NULL || list_entry->bin.byte_count == 0)
        {
          nv_log_error("Failed to load spirv for shader %s\n", list_entry->name);
          wasnt_modified = true;
          break;
        }

        code = NV_ERROR_INVALID_CACHE;
      }
    }

    nv_hashmap_insert_or_replace(&ctx->shader_map, list_entry->name, &list_file->entries[list_i], NULL);

    if (wasnt_modified)
    {
      continue;
    }
  }

  /**
   * If the cache has been invalidated, i.e. a shader has been compiled
   * Then the return code will be NV_ERROR_INVALID_CACHE (because we set it in the loop)
   * This is handled by the caller to induce a cache rebuild, reducing the cases where we would
   * be rebuilding the cache for no reason (no shaders have been modified, so no need to rebuild the cache)
   */
  return code;
}

/**
 * TODO: Well isn't this confusing?
 * dump cachefile calling write cachefile. That's pretty confusing
 */
static inline nv_error
_generate_and_dump_cache_file(const char* cache_file_dir, const nvsm_list_file_t* list_file, size_t list_file_last_modtime)
{
  /**
   * This is legal
   * The function handles it.
   * // nv_assert_else_return(cache_file_dir != NULL, NV_ERROR_INVALID_ARG);
   */

  nv_assert_else_return(list_file != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(list_file->entries != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(list_file->num_entries != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(list_file_last_modtime != 0, NV_ERROR_INVALID_ARG);

  char cache_file_path[256] = {};
  _nvsm_get_cache_file_path(cache_file_dir, cache_file_path);

  FILE* generated_cache_file = fopen(cache_file_path, "wb");
  nv_assert_and_exec(generated_cache_file != NULL, nv_log_error("w:%s fail. %s\n", cache_file_path, strerror(errno)); return NV_ERROR_IO_ERROR;);

  nv_error code = _nvsm_generate_and_write_cache_file(list_file, list_file_last_modtime, generated_cache_file);
  if (code != NV_SUCCESS)
  {
    fclose(generated_cache_file);
    return code;
  }

  fclose(generated_cache_file);

  return NV_SUCCESS;
}

nv_error
nvsm_compile_shaders(nvsm_ctx_t* ctx)
{
  nv_assert_else_return(ctx != NULL, NV_ERROR_INVALID_ARG);

  const char* list_file_path = ctx->list_file;
  if (!list_file_path)
  {
    list_file_path = "Shaders/shaderlist";
  }

  nv_error code = NV_ERROR_SUCCESS;

  nvsm_list_file_t  list_file  = nv_zero_init(nvsm_list_file_t);
  nvsm_cache_file_t cache_file = nv_zero_init(nvsm_cache_file_t);

  if ((code = _nvsm_load_list_file(ctx, &list_file)) != NV_SUCCESS)
  {
    return code;
  }

  code = _nvsm_load_cache_file(ctx, &cache_file);

  if (code == NV_ERROR_FILE_NOT_FOUND || code == NV_ERROR_INVALID_CACHE)
  {
    // continue without cache
    if ((code = _nvsm_default_compile_no_cache(ctx, &list_file)) != NV_SUCCESS)
    {
      return code;
    }

    /**
     * TODO: Do we always want to dump a cache file? Even for no cache compilation runs?
     * Should we add a ctx configuration for that? Or a preprocessor maybe?
     */
    _generate_and_dump_cache_file(ctx->cache_file_dir, &list_file, get_last_modified_time(list_file_path));
  }
  else if (code != NV_SUCCESS)
  {
    return code;
  }
  else
  {
    char cache_file_path[256];
    _nvsm_get_cache_file_path(ctx->cache_file_dir, cache_file_path);

    /* we have the cache in this branch */
    nv_log_info("Loaded cache file (%s)\n", cache_file_path);

    code = _nvsm_default_compile_with_cache(ctx, &list_file, &cache_file);

    if (code != NV_ERROR_INVALID_CACHE && code != NV_SUCCESS)
    {
      return code;
    }

    /* the cache should only be destroyed in cases where it was actually built */
    _nvsm_destroy_cache_file(&cache_file);

    /**
     * we do, in fact need to build the cache on runs where it was successful
     * becuase the cache can be modified, duffer
     * I mean to say that the cache may become invalidated.
     * The function returns a code if the cache has been invalidated or not, and it is used accordingly
     */
    _generate_and_dump_cache_file(ctx->cache_file_dir, &list_file, get_last_modified_time(list_file_path));
  }

  _nvsm_destroy_list_file(&list_file);

  return NV_SUCCESS;
}

nv_error
nvsm_create_shader_modules(nvvk_ctx_t* nvvkctx, nvsm_ctx_t* ctx)
{
  nv_assert_else_return(ctx != NULL, NV_ERROR_INVALID_ARG);

  nvsm_compile_options_t compile_options = _nvsm_get_default_compile_options();
  if (ctx->enable_custom_spirv_options)
  {
    compile_options = ctx->custom_options;
  }

  /* I do not think this is an error. The iterate function would simply return NULL and we would be out of this function */
  // nv_assert_else_return(nv_hashmap_size(&ctx->shader_map) != 0, NV_ERROR_INVALID_ARG);

  /**
   * Iterate through every list entry and create the shader module for it
   * WARNING: Doesn't compile the shader if it isn't already compiled...
   */

  size_t             _hashmap_iter = 0;
  nv_hashmap_node_t* node          = NULL;
  while ((node = nv_hashmap_iterate_unsafe(&ctx->shader_map, &_hashmap_iter)) != NULL)
  {
    nvsm_list_file_entry_t* entry = (nvsm_list_file_entry_t*)node->value;
    if (!entry)
    {
      continue;
    }

    nv_error code = NV_SUCCESS;

    if (entry->bin.words == NULL || entry->bin.byte_count == 0)
    {
      code = read_shader_spirv(entry->spirv_path, &entry->bin);
      if (code != NV_SUCCESS)
      {
        code = _nvsm_compile_shader(entry->shader_path, &compile_options, entry->spirv_path, entry->stage, &entry->bin, true);
        if (code != NV_SUCCESS)
        {
          nv_log_error("Could not compile %s despite best measures.\n", entry->name);
          continue;
        }
      }
    }

    const VkShaderModuleCreateInfo info = {
      .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = entry->bin.byte_count,
      .pCode    = entry->bin.words,
    };
    nvvk_result_check(*nvvkctx, vkCreateShaderModule(nvvkctx->device, &info, NOVA_VK_ALLOCATOR, &entry->module));

    entry->resources = nv_shader_resources_extract(entry->bin.words, entry->bin.byte_count, &entry->num_resources);

    // TODO: Do we need to maintain the SPIRV?
    nv_free((void*)entry->bin.words);
    entry->bin.words      = NULL;
    entry->bin.byte_count = 0;
  }

  return NV_SUCCESS;
}

nv_error
nvsm_load_shader(nvsm_ctx_t* ctx, const char* name, nvsm_shader_t** out)
{
  nv_assert_else_return(name != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(out != NULL, NV_ERROR_INVALID_ARG);

  nvsm_shader_t* entry = nv_hashmap_find(&ctx->shader_map, name, NULL);
  if (!entry)
  {
    return NV_ERROR_INVALID_RETVAL;
  }

  *out = (nvsm_shader_t*)entry;

  return NV_SUCCESS;
}

nv_error
nvsm_compile_shaders_force(nvsm_ctx_t* ctx, bool generate_cache)
{
  nv_assert_else_return(ctx != NULL, NV_ERROR_INVALID_ARG);

  const char* list_file_path = ctx->list_file;
  if (!list_file_path)
  {
    list_file_path = "Shaders/shaderlist";
  }

  nv_error code = NV_ERROR_SUCCESS;

  nvsm_list_file_t list_file = nv_zero_init(nvsm_list_file_t);

  if ((code = _nvsm_load_list_file(ctx, &list_file)) != NV_SUCCESS)
  {
    return code;
  }

  if ((code = _nvsm_default_compile_no_cache(ctx, &list_file)) != NV_SUCCESS)
  {
    return code;
  }

  if (generate_cache)
  {
    char cache_file_path[256];
    _nvsm_get_cache_file_path(ctx->cache_file_dir, cache_file_path);

    FILE* generated_cache_file = fopen(cache_file_path, "wb");
    nv_assert_else_return(generated_cache_file != NULL, NV_ERROR_IO_ERROR);

    _nvsm_generate_and_write_cache_file(&list_file, get_last_modified_time(list_file_path), generated_cache_file);

    fclose(generated_cache_file);
  }

  _nvsm_destroy_list_file(&list_file);

  return NV_SUCCESS;
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
  return VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
}

nv_error
nvsm_init(nvsm_ctx_t* ctx)
{
  nv_assert_else_return(ctx != NULL, NV_ERROR_INVALID_ARG);

  nv_bzero(ctx, sizeof(nvsm_ctx_t));

  nv_error code = nv_hashmap_init(16, sizeof(const char*), sizeof(nvsm_list_file_entry_t), nv_hash_fnv1a_string, nv_allocator_c, NULL, &ctx->shader_map);
  if (code != NV_SUCCESS)
  {
    return code;
  }

  ctx->custom_options = _nvsm_get_default_compile_options();

  return NV_SUCCESS;
}

void
nvsm_shutdown(nvvk_ctx_t* nvvkctx, nvsm_ctx_t* ctx)
{
  nv_assert_else_return(ctx != NULL, );

  size_t             _hashmap_iter = 0;
  nv_hashmap_node_t* node          = NULL;
  while ((node = nv_hashmap_iterate_unsafe(&ctx->shader_map, &_hashmap_iter)) != NULL)
  {
    nvsm_list_file_entry_t* entry = (nvsm_list_file_entry_t*)node->value;
    if (entry->module != VK_NULL_HANDLE)
    {
      vkDestroyShaderModule(nvvkctx->device, entry->module, NOVA_VK_ALLOCATOR);
    }
    if (entry->resources != NULL && entry->num_resources > 0)
    {
      for (size_t i = 0; i < entry->num_resources; i++)
      {
        nv_shader_resources_t* resource = &entry->resources[i];
        nv_free((void*)resource->name);
      }
      nv_free(entry->resources);
    }
  }

  nv_hashmap_destroy(&ctx->shader_map);
}

nv_shader_resource_type
_nv_shader_resources_type_spv_reflect_type(SpvReflectDescriptorType type)
{
  switch (type)
  {
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER: return NV_SHADER_RESOURCE_TYPE_UNIFORM_BUFFER;
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER: return NV_SHADER_RESOURCE_TYPE_STORAGE_BUFFER;
    case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: return NV_SHADER_RESOURCE_TYPE_COMBINED_IMAGE_SAMPLER;
    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE: return NV_SHADER_RESOURCE_TYPE_SAMPLED_IMAGE;
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE: return NV_SHADER_RESOURCE_TYPE_STORAGE_IMAGE;
    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER: return NV_SHADER_RESOURCE_TYPE_SAMPLER;
    default: return NV_SHADER_RESOURCE_TYPE_UNKNOWN;
  }
}

nv_shader_resources_t*
nv_shader_resources_extract(const u32* spirv_data, size_t spirv_size, size_t* out_count)
{
  nv_assert_else_return(spirv_data != NULL, NULL);
  nv_assert_else_return(spirv_size != 0, NULL);
  nv_assert_else_return(out_count != NULL, NULL);

  SpvReflectShaderModule module;
  SpvReflectResult       result = spvReflectCreateShaderModule(spirv_size, spirv_data, &module);
  if (result != SPV_REFLECT_RESULT_SUCCESS)
  {
    *out_count = 0;
    return NULL;
  }

  u32 binding_count = 0;
  spvReflectEnumerateDescriptorBindings(&module, &binding_count, NULL);

  if (binding_count == 0)
  {
    spvReflectDestroyShaderModule(&module);
    *out_count = 0;
    return NULL;
  }

  SpvReflectDescriptorBinding** bindings = nv_malloc(sizeof(SpvReflectDescriptorBinding*) * binding_count);
  spvReflectEnumerateDescriptorBindings(&module, &binding_count, bindings);

  nv_shader_resources_t* resources = nv_malloc(sizeof(nv_shader_resources_t) * binding_count);
  nv_memset(resources, 0, sizeof(nv_shader_resources_t) * binding_count);

  for (size_t i = 0; i < binding_count; i++)
  {
    SpvReflectDescriptorBinding* binding = bindings[i];

    resources[i].name         = nv_strdup(nv_allocator_c, NULL, binding->name);
    resources[i].set          = binding->set;
    resources[i].binding      = binding->binding;
    resources[i].type         = _nv_shader_resources_type_spv_reflect_type(binding->descriptor_type);
    resources[i].array_length = binding->count;
  }

  *out_count = (size_t)binding_count;

  nv_free(bindings);
  spvReflectDestroyShaderModule(&module);
  return resources;
}