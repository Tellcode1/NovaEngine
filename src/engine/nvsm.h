#ifndef __NOVA_SHADER_MANAGER_H__
#define __NOVA_SHADER_MANAGER_H__

#include "../GPU/vkstdafx.h"
#include "../containers/hashmap.h"
#include "../std/stdafx.h"

// TODO: Hot reloading? Hot reloading.

/**
 * Cache file structure:
 *  canary: u32
 *  list file last mod time : size_t
 *  number of entries : u32
 */

NOVA_HEADER_START

#ifndef NVSM_CACHE_FILENAME
#  define NVSM_CACHE_FILENAME ".nvsmcache"
#endif // NVSM_CACHE_FILENAME

#ifndef NVSM_SHADER_SPIRV_DIRNAME
#  define NVSM_SHADER_SPIRV_DIRNAME ".nvshaders"
#endif

typedef struct nvsm_ctx_t              nvsm_ctx_t;
typedef struct nvsm_list_file_entry_t  nvsm_list_file_entry_t;
typedef struct nvsm_cache_file_entry_t nvsm_cache_file_entry_t;
typedef struct nvsm_list_file_t        nvsm_list_file_t;
typedef struct nvsm_cache_file_t       nvsm_cache_file_t;

struct nvvk_ctx_t;

#define nvsm_shader_t nvsm_list_file_entry_t

extern nv_errorc nvsm_init(nvsm_ctx_t* ctx);
extern void      nvsm_shutdown(struct nvvk_ctx_t* nvvkctx, nvsm_ctx_t* ctx);

extern nv_errorc nvsm_compile_shaders(nvsm_ctx_t* ctx);
extern nv_errorc nvsm_compile_shaders_force(nvsm_ctx_t* ctx, bool generate_cache);

extern nv_errorc nvsm_create_shader_modules(struct nvvk_ctx_t* nvvkctx, nvsm_ctx_t* ctx);

extern nv_errorc nvsm_load_shader(nvsm_ctx_t* ctx, const char* name, nvsm_shader_t** out);

/* Returns (VkShaderStageFlags)-1 on error/invalid stage */
extern VkShaderStageFlags _nvsm_shader_stage_from_string(const char stage[4]);

struct nvsm_ctx_t
{
  /**
   * Mapping from name to a nvsm_shader_t
   * The name is taken from the shader file header.
   */
  nv_hashmap_t shader_map;

  /**
   * The file containing all the shaders that need to be compiled.
   * In newer versions, this file will be made the sole governor
   * And it will basically contain all the info currently stored in the shader header.
   * Note that the list file is copied to the build directory and this path is relative to the build directory.
   */
  const char* list_file;

  /**
   * "/shaders.cache" is appended to this directory.
   * No sanity checks are done and nvsm will simply exit if this is
   * not a valid directory. Do not add an extra '/' after the path.
   */
  const char* cache_file_dir;

  /**
   * Defaults to NULL
   * If NULL, then glslangValidator is called. Which may fail if it isn't installed.
   */
  const char* shader_compiler;

  /**
   * "-V" is appended to this list. You do need to add it yourself.
   */
  const char* shader_compiler_args;
};

typedef struct nvsm_list_file_entry_t
{
  /* we technically only need 4 bytes */
  char           stage[8];
  VkShaderModule module;
  char           shader_path[256];
  char           spirv_path[256];
  char           name[256];
} nvsm_list_file_entry_t;

typedef struct nvsm_cache_file_entry_t
{
  size_t last_mod_time;
  char   name[256];
} nvsm_cache_file_entry_t;

typedef struct nvsm_list_file_t
{
  size_t                  num_entries;
  nvsm_list_file_entry_t* entries;
} nvsm_list_file_t;

typedef struct nvsm_cache_file_t
{
  size_t                   num_entries;
  nvsm_cache_file_entry_t* entries;
} nvsm_cache_file_t;

NOVA_HEADER_END

#endif //__NOVA_SHADER_MANAGER_H__