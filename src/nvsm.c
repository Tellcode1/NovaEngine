#include "common/mem.h"
#include "containers/list.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifndef WIN32
#  include <unistd.h>
#endif

#ifdef WIN32
#  define stat _stat
#endif

#if !(NVSM_EXECUTABLE)

struct nvsm_shader_t* shader_map = NULL;
int                   nshaders   = 0;

#  include "GPU/pipeline.h"

#endif

#include "engine/shadermanager.h"
#include "engine/shadermanagerdev.h"

const char* shader_compiler      = "glslangValidator";
const char* shader_compiler_args = " -V ";
const char* list                 = "../compilelist.txt";

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

#define NVSM_HAS_FLAG(flag) (nv_strcmp(argv[i], flag) == 0)

#include "std/print.h"
#include "std/stdafx.h"
#include "std/string.h"
#include <errno.h>

#if (NVSM_EXECUTABLE)

#  include "std/props.h"

#  define CMD_HELP_MSG                                                                                                                                                        \
    "cmd can be any of:\n\
<default> compile: compile only those that have been changed since last ran,\n\
compile-force: forcefully compile all shaders in list file,\n\
\n"

int
main(int argc, char* argv[])
{
  char buf[256] = "../compilelist.txt";
  char cmd[256] = "compile";

  bool help = 0;
  // clang-format off
  nv_option_t options[] = {
    { NV_OP_TYPE_STRING, "l", "list", buf, sizeof(buf) },
    { NV_OP_TYPE_STRING, "c", "command", cmd, sizeof(cmd) },
    { NV_OP_TYPE_BOOL, "h", "help", &help, 0 },
  };
  // clang-format on

  char error[256];
  if (nv_props_parse(argc, argv, options, nv_arrlen(options), error, sizeof(error)) != 0)
  {
    nv_log_error("PROPS error: %s", error);
    nv_props_gen_help(options, nv_arrlen(options), error, nv_arrlen(error));
    nv_printf("%s\n", error);
  }

  if (help)
  {
    nv_printf("usage: %s <compile list path = \"../compilelist.txt\"> <cmd = compile>\n" CMD_HELP_MSG, argv[0]);
    return 0;
  }

  nv_log_info("Compile list: %s\n", buf);
  nv_log_info("Command: %s\n", cmd);

  list = buf;

  if (nv_strcmp(cmd, "compile-force") == 0)
  {
    nvsm_compile_all();
  }
  else if (nv_strcmp(cmd, "compile") == 0)
  {
    nvsm_compile_updated();
  }
  else
  {
    return -1;
  }

  return 0;
}

#else

static inline int
compare_shader_t(const void* a, const void* b)
{
  const struct nvsm_shader_t* shader1 = (const struct nvsm_shader_t*)a;
  const struct nvsm_shader_t* shader2 = (const struct nvsm_shader_t*)b;
  return nv_strncmp(shader1->name, shader2->name, 128);
}

static inline void
nvsm_add_shader_to_map(struct nvsm_shader_cache_entry_t entry, nvsm_shader_t** dst)
{
  (void)nvsm_add_shader_to_map;

  struct nvsm_shader_t* new_map = nv_malloc((nshaders + 1) * sizeof(struct nvsm_shader_t));
  if (nshaders > 0)
  {
    nv_memcpy(new_map, shader_map, nshaders * sizeof(struct nvsm_shader_t));
  }
  if (shader_map != NULL)
  {
    nv_free(shader_map);
  }
  shader_map = new_map;

  struct nvsm_shader_t add;
  nv_strcpy(add.name, entry.name);
  shader_map[nshaders] = add;

  *dst = &shader_map[nshaders];

  nshaders++;
  // map is sorted after all shaders are registered.

  qsort(shader_map, nshaders, sizeof(struct nvsm_shader_t), compare_shader_t);
}

static inline struct nvsm_shader_t*
find_shader(const char* name)
{
  struct nvsm_shader_t shader = nv_zero_init(struct nvsm_shader_t);

  nv_strlcpy(shader.name, name, sizeof(shader.name));
  shader.name[sizeof(shader.name) - 1] = '\0';

  return (struct nvsm_shader_t*)bsearch(&shader, shader_map, nshaders, sizeof(struct nvsm_shader_t), compare_shader_t);
}

static inline bool
does_shader_exist(const char* name)
{
  return find_shader(name) != NULL;
}

int
nvsm_load_shader(const char* name, struct nvsm_shader_t** out)
{
  if (name == NULL || nv_strlen(name) == 0)
  {
    return -1;
  }

  struct nvsm_shader_t shader = nv_zero_init(struct nvsm_shader_t);

  nv_strlcpy(shader.name, name, sizeof(shader.name));
  shader.name[sizeof(shader.name) - 1] = '\0';

  struct nvsm_shader_t* shaderptr = (struct nvsm_shader_t*)bsearch(&shader, shader_map, nshaders, sizeof(struct nvsm_shader_t), compare_shader_t);

  if (shaderptr)
  {
    *out = shaderptr;
  }
  else
  {
    // should we check out is NULL before setting it or not
    *out = NULL;
    return -1;
  }

  return 0;
}

int
nvsm_load_shader_from_disk(const char* path, nvsm_shader_t** out)
{
  (void)path;
  (void)out;
  // nvsm_shader_entry_t entry = {0};

  // char line[256];
  // strcpy(line, path);

  // nvsm_load_shader_file(line, &entry);

  // nvsm_shader_cache_entry_t cache_e = {};
  // strcpy(cache_e.path, entry.path);
  // strcpy(cache_e.output_path, entry.output_path);
  // strcpy(cache_e.name, entry.name);
  // cache_e.last_modified = entry.last_modified;

  // nvsm_add_shader_to_map(cache_e, out);
  nv_assert(0);
  return 0;
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

#  include "GPU/pipeline.h"
#  include "external/volk/volk.h"

void
_nvsm_create_shader(VkDevice vkdevice, const unsigned* bytes, size_t nbytes, struct nvsm_shader_t* out)
{
  // SpvReflectShaderModule reflect_module;
  // spvReflectCreateShaderModule(nbytes, bytes, &reflect_module);

  // reflect_shader_descriptors(&reflect_module, out);

  // spvReflectDestroyShaderModule(&reflect_module);

  const VkShaderModuleCreateInfo info = {
    .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = nbytes,
    .pCode    = bytes,
  };
  nvvk_result_check(vkCreateShaderModule(vkdevice, &info, NOVA_VK_ALLOCATOR, (VkShaderModule*)&out->shader_module));

  nv_free((void*)bytes);
}

void
nvsm_register_all_shaders(VkDevice vkdevice, struct nvsm_shader_entry_t* entries, int nentries)
{
  struct nvsm_shader_t* new_shader_map = nv_malloc((nshaders + nentries) * sizeof(struct nvsm_shader_t));
  if (shader_map)
  {
    nv_memcpy(new_shader_map, shader_map, nshaders * sizeof(struct nvsm_shader_t));
    nv_free(shader_map);
  }
  shader_map = new_shader_map;

  int index = 0;
  for (int i = 0; i < nentries; i++)
  {
    nvsm_shader_t* shader = &shader_map[nshaders + index];
    if (nv_strncmp(entries[i].stage, "vert", 4) == 0)
    {
      shader->stage = VK_SHADER_STAGE_VERTEX_BIT;
    }
    else if (nv_strncmp(entries[i].stage, "frag", 4) == 0)
    {
      shader->stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    else if (nv_strncmp(entries[i].stage, "tese", 4) == 0)
    {
      shader->stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    }
    else if (nv_strncmp(entries[i].stage, "tesc", 4) == 0)
    {
      shader->stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    }
    else if (nv_strncmp(entries[i].stage, "geom", 4) == 0)
    {
      shader->stage = VK_SHADER_STAGE_GEOMETRY_BIT;
    }
    else if (nv_strncmp(entries[i].stage, "comp", 4) == 0)
    {
      shader->stage = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    else
    {
      nv_log_error("Invalid stage for shader \"%s\". It will not be added.", entries[i].name);
      continue;
    }
    nv_strlcpy(shader->name, entries[i].name, sizeof(shader->name));

    unsigned* spirv     = NULL;
    size_t    spirvsize = 0;
    if (read_shader_spirv((const char*)entries[i].output_path, &spirv, &spirvsize) != 0)
    {
      if (spirv)
      {
        nv_free(spirv);
      }
      continue;
    }
    _nvsm_create_shader(vkdevice, spirv, spirvsize, &shader_map[nshaders + index]);

    nshaders++;
  }
  qsort(shader_map, nshaders, sizeof(struct nvsm_shader_t), compare_shader_t);
}

#endif // NVSM_EXECUTABLE != 1

void
nvsm_set_list_file(const char* path)
{
  list = path;
}

void
nvsm_set_shader_compiler(const char* exec)
{
  shader_compiler = exec;
}

void
nvsm_set_shader_compiler_args(const char* args)
{
  shader_compiler_args = args;
}

char const*
nvsm_get_shader_compiler_args(void)
{
  return shader_compiler_args;
}

static inline nvsm_shader_cache_entry_t*
load_cache(int* count)
{
  nv_assert(count != NULL);

  FILE* f = fopen("shaders.cache", "rb");
  if (f == NULL)
  {
    *count = 0;
    nv_log_error("io error %s\n", strerror(errno));
    return NULL; // safe to return. nvsm will gracefully handle this.
  }

  if (fread(count, sizeof(int), 1, f) != 1)
  {
    *count = 0;
    nv_log_error("io error %s\n", strerror(errno));
    NOVA_CALL_FILE_FN(fclose(f));
    return NULL;
  }
  nvsm_shader_disk_t* write = (nvsm_shader_disk_t*)nv_calloc(*count * sizeof(nvsm_shader_disk_t));
  if (fread(write, sizeof(nvsm_shader_disk_t), *count, f) != (size_t)(*count))
  {
    *count = 0;
    nv_log_error("io error %s\n", strerror(errno));
    NOVA_CALL_FILE_FN(fclose(f));
    return NULL;
  }

  nvsm_shader_cache_entry_t* entries = nv_calloc(*count * sizeof(nvsm_shader_cache_entry_t));
  for (int i = 0; i < (*count); i++)
  {
    entries[i].canary = 0xDEADBEEF;
    nv_strlcpy(entries[i].name, write[i].name, sizeof(entries[i].name));
    nv_strlcpy(entries[i].path, write[i].path, sizeof(entries[i].path));
    entries[i].last_modified = write[i].last_modified;
  }

  nv_free(write);

  NOVA_CALL_FILE_FN(fclose(f));
  return entries;
}

static inline void
update_cache(const nvsm_shader_cache_entry_t* restrict entries, int count)
{
  FILE* f = fopen("shaders.cache", "wb");
  if (!f)
  {
    nv_log_error("Could not open cache file for update due to %s\n", strerror(errno));
    return;
  }

  nv_list_t write_list;
  nv_list_init(sizeof(nvsm_shader_disk_t), count, nv_allocator_get_default(), &write_list);

  for (int i = 0; i < count; i++)
  {
    if (entries[i].canary != 0xDEADBEEF)
    {
      continue;
    }
    nvsm_shader_disk_t* write = (nvsm_shader_disk_t*)nv_list_push_empty(&write_list);
    nv_strlcpy(write->name, entries[i].name, sizeof(write->name));
    nv_strlcpy(write->path, entries[i].path, sizeof(write->path));
    write->last_modified = entries[i].last_modified;
  }

  nv_assert(fwrite(&count, sizeof(int), 1, f) == 1);
  nv_assert(fwrite(nv_list_data(&write_list), sizeof(nvsm_shader_disk_t), nv_list_size(&write_list), f) == nv_list_size(&write_list));

  nv_list_destroy(&write_list);

  NOVA_CALL_FILE_FN(fclose(f));
}

static inline void
write_new_cache(const nvsm_shader_entry_t* restrict entries, int count)
{
  FILE* f = fopen("shaders.cache", "wb");
  if (!f)
  {
    return;
  }

  nvsm_shader_disk_t* write = nv_malloc(sizeof(nvsm_shader_disk_t) * count);
  if (!write)
  {
    NOVA_CALL_FILE_FN(fclose(f));
    return;
  }

  for (int i = 0; i < count; i++)
  {
    const char* name = entries[i].name;
    const char* path = entries[i].path;
    nv_strlcpy(write[i].name, name, sizeof(write[i].name));
    nv_strlcpy(write[i].path, path, sizeof(write[i].path));
    write[i].last_modified = entries[i].last_modified;
  }

  if (fwrite(&count, sizeof(int), 1, f) != 1)
  {
    NOVA_CALL_FILE_FN(fclose(f));
    return;
  }
  if (fwrite(write, sizeof(nvsm_shader_disk_t), count, f) != (size_t)count)
  {
    NOVA_CALL_FILE_FN(fclose(f));
    return;
  }

  NOVA_CALL_FILE_FN(fclose(f));

  nv_log_info("NVSM cache written successfully\n");
}

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

static inline time_t
get_mtime(const char* fpath)
{
  struct stat file_stats;
  if (stat(fpath, &file_stats) == 0)
  {
    return file_stats.st_mtime;
  }
  else
  {
    nv_log_error("stat error: %s\n", strerror(errno));
  }
  return -1;
}

static inline nvsm_shader_entry_t*
load_all_entries(const char* shader_list_file_path, int* count)
{
  FILE* f = fopen(shader_list_file_path, "r");
  if (f == NULL)
  {
    nv_log_error("Could not open list file for reading: %s\n", strerror(errno));
    return NULL;
  }

  int                  currallocsize = 16;
  nvsm_shader_entry_t* entries       = nv_malloc(currallocsize * sizeof(nvsm_shader_entry_t));
  nv_assert_and_ret(entries != NULL, NULL);

  char line[256];
  for (int i = 0;; i++)
  {
    if (!fgets(line, 256, f))
    {
      break;
    }
    else if (nv_strlen(line) == 1)
    {
      continue; // line only contains \n
    }
    line[nv_strcspn(line, "\n")] = 0;

    if (i >= currallocsize)
    {
      currallocsize *= 2;
      entries = nv_realloc(entries, currallocsize * sizeof(nvsm_shader_entry_t));
      nv_assert_and_ret(entries != NULL, NULL);
    }

    entries[*count]            = nv_zero_init(nvsm_shader_entry_t);
    nvsm_shader_entry_t* entry = &entries[*count];

    nv_strlcpy(entry->path, line, sizeof(entry->path));

    // to get stage + verify that it exists
    FILE* shader_file = fopen(entry->path, "r");
    if (shader_file == NULL)
    {
      nv_log_error("Could not open shader file \"%s\": %s", entry->path, strerror(errno));
      continue;
    }

    nv_assert(fgets(line, 256, shader_file) != NULL);

    const char* li = line;
    while (*li == ' ')
    {
      li++;
    }
    if (nv_strncmp(li, "//", 2) == 0)
    {
      sscanf(li, "// output: %s stage: %s name: %s", entry->output_path, entry->stage, entry->name);
    }
    else
    {
      nv_strcpy(entry->stage, "000");
      nv_strcpy(entry->output_path, "");
      nv_log_error(
          "Shader \"%s\": has invalid or no header.\nHeader Format "
          "-> // output: "
          "{output} stage: {stage} name: {name}",
          entry->path);
    }

    NOVA_CALL_FILE_FN(fclose(shader_file));

    entry->last_modified = get_mtime(entry->path);

    (*count)++;
  }

  NOVA_CALL_FILE_FN(fclose(f));
  return entries;
}

#if defined(__linux)
// int _nvsm_linux_run() {

// }
// #define system _nvsm_linux_run
#endif

static char* g_Buffer = NULL;

static inline int
compile_shader(const struct nvsm_shader_entry_t* entry)
{
  if (!g_Buffer)
  {
    g_Buffer = nv_calloc(1024);
  }

  if (!g_Buffer)
  {
    nv_log_error("Global buffer allocation failed\n");
    exit(-1);
    return -1;
  }

  // if (nv_strcmp(entry->output_path, "") == 0)
  // {
  //   nv_log_error("Empty output path!\n");
  //   return -1;
  // }

  // if (nv_strcmp(entry->path, "") == 0)
  // {
  //   nv_log_error("Empty input path!\n");
  //   return -1;
  // }

  // if (nv_strcmp(entry->stage, "") == 0)
  // {
  //   nv_log_error("Empty stage!\n");
  //   return -1;
  // }

  char copy[256];
  nv_strlcpy(copy, entry->output_path, sizeof(copy));

  create_parent_dirs(copy);

  nv_snprintf(
      g_Buffer,
      1024,
      "%s %s %s -o %s -S %.4s",
      shader_compiler,
      shader_compiler_args,
      entry->path,
      nv_strcmp(entry->output_path, "") != 0 ? entry->output_path : "",
      entry->stage);

  if (g_Buffer != NULL && system(g_Buffer) != 0)
  {
    return -1;
  }

  if (g_Buffer)
  {
    g_Buffer[1023] = '\0';
  }

  return 0;
}

static inline int
nvsm_compile_from_cache(nvsm_shader_entry_t* entries, int nentries, nvsm_shader_cache_entry_t* cacheentries, int cachecount)
{
  int compiled = 0;
  for (int i = 0; i < nentries; i++)
  {
    for (int j = 0; j < cachecount; j++)
    {
      if (nv_strcmp(cacheentries[j].path, entries[i].path) != 0)
      {
        continue;
      }
      if (cacheentries[j].last_modified == entries[i].last_modified)
      {
        continue;
      }
      if (compile_shader(&entries[i]) != 0)
      {
        nv_log_error("Error while compiling shader \"%s\".", entries[i].path);
        continue;
      }
      compiled++;
      cacheentries[j].last_modified = entries[i].last_modified;
      break;
    }
  }
  return compiled;
}

static inline int
nvsm_compile_without_cache(nvsm_shader_entry_t* entries, int nentries)
{
  int compiled = 0;
  for (int i = 0; i < nentries; i++)
  {
    if (compile_shader(&entries[i]) != 0)
    {
      nv_log_error("Error while compiling shader \"%s\".", entries[i].path);
      continue;
    }
    compiled++;
  }
  return compiled;
}

int
nvsm_compile_updated(void)
{
  int                  nentries = 0;
  nvsm_shader_entry_t* entries  = load_all_entries(list, &nentries);

  int                        cachecount   = 0;
  nvsm_shader_cache_entry_t* cacheentries = load_cache(&cachecount);

  int num_shaders_compiled = 0;

  if (cacheentries == NULL)
  {
    nv_log_error("Could not open cache for reading. return.\n");
    num_shaders_compiled = nvsm_compile_without_cache(entries, nentries);
  }
  else
  {
    num_shaders_compiled = nvsm_compile_from_cache(entries, nentries, cacheentries, cachecount);
  }

  if (cacheentries == NULL)
  {
    nv_log_error("No cache or modified cache. Writing new cache file...\n");
    write_new_cache(entries, nentries);
  }
  else
  {
    update_cache(cacheentries, nentries);
  }

#if NVSM_EXECUTABLE != 1
  nvsm_register_all_shaders(nvvk_context.device, entries, nentries);
#endif // NVSM_EXECUTABLE != 1

  nv_free(entries);

  if (cacheentries)
  {
    nv_free(cacheentries);
  }

  return num_shaders_compiled;
}

int
nvsm_compile_all(void)
{
  int                  count   = 0;
  nvsm_shader_entry_t* entries = load_all_entries(list, &count);

#if NVSM_EXECUTABLE != 1
  nvsm_register_all_shaders(nvvk_context.device, entries, count);
#endif // #if NVSM_EXECUTABLE != 1

  int num_shaders_compiled = 0;

  for (int i = 0; i < count; i++)
  {
    if (compile_shader(&entries[i]) != 0)
    {
      nv_log_error("Error while compiling shader \"%s\".", entries[i].path);
    }
    else
    {
      num_shaders_compiled++;
    }
  }

  write_new_cache(entries, count);

  nv_free(entries);
  return num_shaders_compiled;
}

void
nvsm_shutdown(void)
{
#if !(NVSM_EXECUTABLE)
  for (int i = 0; i < nshaders; i++)
  {
    struct nvsm_shader_t* shader = &shader_map[i];
    if (shader->shader_module)
    {
      vkDestroyShaderModule(nvvk_context.device, shader->shader_module, NOVA_VK_ALLOCATOR);
    }
  }
#endif
}
