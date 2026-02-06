
#ifndef LUNAR_SHADER_MANAGER_HPP_INCLUDED
#define LUNAR_SHADER_MANAGER_HPP_INCLUDED

#include "../../external/volk/volk.h"
#include "../std/include/containers/hashmap.h"
#include "../std/include/error.h"
#include "../std/include/types.h"
#include "shaderresource.h"

#include "../../external/volk/volk.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * We're incorporating the glslang project into nvsm
 * It would have been done later down the road anyway.
 * glslang gives us reflection and what not to allow us to do virtually anything with shaders
 *
 * TODO: Hot reloading? Hot reloading.
 * WARNING: Currently, NVSM does not check for duplicate entries.
 * TODO: Also, the hard limit of 256 characters for strings is very bad. Fix.
 */

/**
 * @brief The function used to hash the name and the shader path for the cache files and the list files for comparison
 * NOTE: if this function is changed, you will need to manually recompile all the shaders. This is why its a define and not a runtime variable.
 */
#define NVSM_STRING_HASH_FUNCTION nv_hash_fnv1a_string

/**
 * @brief Add debugging info to the shaders. May be disabled if not needed
 * The user may define this before hand to disable debugging
 */
#if !defined(NVSM_SHADERS_ENABLE_DEBUGGING)
#  if defined(DEBUG)
#    define NVSM_SHADERS_ENABLE_DEBUGGING true
#  else
#    define NVSM_SHADERS_ENABLE_DEBUGGING false
#  endif // defined(DEBUG)
#endif   //! defined(NVSM_SHADERS_ENABLE_DEBUGGING)

/**
 * Cache file structure:
 *  u32    canary
 *  size_t list file mod time
 *  u32    number of entries
 */

/**
 * @brief The file where the "cache" is stored. The cache contains the hashes for the shader name and the shader's current path
 * This cache may also be stored on the /tmp folder but that is not used in lieu of compatability
 * You may do so just by adding the /tmp/ prefix to the filename
 * All parent directories will automatically be created
 * WARNING: You need the slash in front of the name because this define is appended to a user defined directory
 */
#ifndef NVSM_CACHE_FILENAME
#  define NVSM_CACHE_FILENAME "/.nvsmcache"
#endif // NVSM_CACHE_FILENAME

/**
 * @brief The directory where the compiled shaders's spirv will be stored. Any parent directories (including this directory) will automatically be created.
 */
#ifndef NVSM_SHADER_SPIRV_DIRNAME
#  define NVSM_SHADER_SPIRV_DIRNAME ".nvshaders/"
#endif // NVSM_SHADER_SPIRV_DIRNAME

#ifndef NVSM_ENIVRONMENT
/**
 * Not currently used!
 * When OpenGL  compatability is implemented, this will be used to allow nvsm to run on OpenGL
 */
#  define NVSM_ENIVRONMENT VULKAN
#endif

  struct resources_t;
  struct nvvk_ctx;

  typedef struct nvsm_ctx              nvsm_ctx_t;
  typedef struct nvsm_list_file_entry  nvsm_list_file_entry_t;
  typedef struct nvsm_cache_file_entry nvsm_cache_file_entry_t;
  typedef struct nvsm_list_file        nvsm_list_file_t;
  typedef struct nvsm_cache_file       nvsm_cache_file_t;
  typedef struct nvsm_spirv_binary     nvsm_spirv_binary_t;
  typedef struct nvsm_compile_options  nvsm_compile_options_t;

#define nvsm_shader_t nvsm_list_file_entry_t

  extern nv_error nvsm_init(nvsm_ctx_t* ctx);
  extern void     nvsm_shutdown(struct nvvk_ctx* vkctx, nvsm_ctx_t* ctx);

  extern nv_error nvsm_compile_shaders(nvsm_ctx_t* ctx);

  /**
   * @brief Compile all the shaders forecfully, bypassing the cache
   * Does not use a cache, but can generate and output one using the flag
   * Note that you will need to use nvsm_compile_shaders for the generated cache to have an effect
   * And if you aren't going to use a cache anytime (for some reason), generate_cache should be off
   *
   * Also note that generating a cache isn't particularly expensive, only a few hashes and file fwrites
   */
  extern nv_error nvsm_compile_shaders_force(nvsm_ctx_t* ctx, bool generate_cache);

  /**
   * @brief Creates all the shader modules for vulkan
   * The shaders are required to be compiled into spirv before a call to this function.
   */
  extern nv_error nvsm_create_shader_modules(struct nvvk_ctx* vkctx, nvsm_ctx_t* ctx);

  /**
   * @brief Load a shader. If an error occurs, out is set to NULL and this function returns.
   */
  extern nv_error nvsm_load_shader(nvsm_ctx_t* ctx, const char* name, nvsm_shader_t** out);

  /**
   * @brief Destroy a shader. All preexisting copies of this shader will be left in an undefined state.
   */
  extern void nvsm_destroy_shader_named(struct nvvk_ctx* vkctx, nvsm_ctx_t* ctx, const char* name);

  /**
   * Delete a shader. But use a pointer to the shader instead of a context.
   */
  extern void nvsm_destroy_shader(struct nvvk_ctx* vkctx, nvsm_shader_t* shader);

  /* Returns (VkShaderStageFlags)-1 on error/invalid stage */
  /**
   * @brief Convert from the glslangValidator's supported set of shader stages (which are used in the file extensions) into VkShaderStageFlags
   * @return VkShaderStageFlags : -1 on error and a valid enum if success
   * vert => Vertex
   * frag => Fragment
   * tese => Tesselation Evaluation
   * tesc => Tesselation Control
   * geom => Geometry(Mesh)
   * comp => Compute
   */
  extern VkShaderStageFlags nvsm_shader_stage_from_string(const char stage[4]);

  struct nvsm_compile_options
  {
    /* If NVSM_SHADERS_ENABLE_DEBUGGING is defined, this is enabled */
    bool enable_debug_mode;

    /* If NVSM_SHADERS_ENABLE_DEBUGGING is defined, these are disabled */
    bool enable_optimizations;
    bool enable_size_optimizations;

    /**
     * Validate the shader source file for errors before compiling it?
     * Default: true
     */
    bool validate;
  };
  extern nvsm_compile_options_t nvsm_get_default_compile_options(void) NOVA_ATTR_CONST;

  struct nvsm_ctx
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
     * If NULL, then glslangValidator is called. It is loaded as a submodule into the project and compiled alongside it.
     */
    const char* shader_compiler;

    /**
     * "-V" is appended to this list. You do not need to add it yourself.
     */
    const char* shader_compiler_args;

    /* Default: False */
    bool enable_custom_spirv_options;

    /**
     * Only used if enable_custom_spirv_options is enabled
     */
    nvsm_compile_options_t custom_options;
  };

  struct nvsm_spirv_binary
  {
    u32* words;
    /* 'size' is misleading as words is a uint32_t pointer, some people may expect it to be the number of words */
    size_t byte_count;
  };

  struct nvsm_list_file_entry
  {
    /* we technically only need 4 bytes */
    char stage[8];
    // I think this can be replaced by a GL_uint to allow for OpenGL compatability
    VkShaderModule handle;
    nvsm_ctx_t*    ctx;

    nvsm_spirv_binary_t bin;
    char                shader_path[256];
    char                spirv_path[256];
    char                name[256];

    size_t                   num_resources;
    nvsm_shader_resources_t* resources;
  };

  /**
   * @brief A structure holding a data of a shader, on the disk.
   */
  struct nvsm_cache_file_entry
  {
    size_t last_mod_time;
    size_t hash_name;
    size_t hash_shader_path;
  };

  struct nvsm_list_file
  {
    size_t                  num_entries;
    nvsm_list_file_entry_t* entries;
  };

  struct nvsm_cache_file
  {
    size_t                   num_entries;
    nvsm_cache_file_entry_t* entries;
  };

#ifdef __cplusplus
}
#endif

#endif // NOVA_SHADER_MANAGER_H