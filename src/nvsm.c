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

#  include "../include/GPU/pipeline.h"

#endif

#include "../include/engine/shadermanager.h"
#include "../include/engine/shadermanagerdev.h"

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

#include "../std/print.h"
#include "../std/stdafx.h"
#include "../std/string.h"
#include "../std/timer.h"
#include <errno.h>

#if (NVSM_EXECUTABLE)

#  include "../std/props.h"

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
  if (nv_props_parse(argc, argv, options, nv_arrlen(options), error, sizeof(error)) == -1)
  {
    nv_push_error("PROPS error: %s", error);
    nv_props_gen_help(options, nv_arrlen(options), error, nv_arrlen(error));
    nv_printf("%s\n", error);
  }

  if (help)
  {
    nv_printf("usage: %s <compile list path = \"../compilelist.txt\"> <cmd = compile>\n" CMD_HELP_MSG, argv[0]);
    return 0;
  }

  nv_log_info("Compile list: %s", buf);
  nv_log_info("Command: %s", cmd);

  list = buf;

  if (nv_strcmp(cmd, "compile-force") == 0) { nvsm_compile_all(); }
  else if (nv_strcmp(cmd, "compile") == 0) { nvsm_compile_updated(); }
  else { return -1; }

  return 0;
}

#else

int
compare_shader_t(const void* a, const void* b)
{
  const struct nvsm_shader_t* shader1 = (const struct nvsm_shader_t*)a;
  const struct nvsm_shader_t* shader2 = (const struct nvsm_shader_t*)b;
  return nv_strncmp(shader1->name, shader2->name, 128);
}

void
nvsm_add_shader_to_map(struct nvsm_shader_cache_entry_t entry, nvsm_shader_t** dst)
{
  struct nvsm_shader_t* new_map = nv_malloc((nshaders + 1) * sizeof(struct nvsm_shader_t));
  if (nshaders > 0) { nv_memcpy(new_map, shader_map, nshaders * sizeof(struct nvsm_shader_t)); }
  if (shader_map != NULL) nv_free(shader_map);
  shader_map = new_map;

  struct nvsm_shader_t add;
  nv_strcpy(add.name, entry.name);
  shader_map[nshaders] = add;

  *dst = &shader_map[nshaders];

  nshaders++;
  // map is sorted after all shaders are registered.

  qsort(shader_map, nshaders, sizeof(struct nvsm_shader_t), compare_shader_t);
}

struct nvsm_shader_t*
find_shader(const char* name)
{
  struct nvsm_shader_t shader = nv_zero_init(struct nvsm_shader_t);

  nv_strncpy(shader.name, name, sizeof(shader.name) - 1);
  shader.name[sizeof(shader.name) - 1] = '\0';

  return (struct nvsm_shader_t*)bsearch(&shader, shader_map, nshaders, sizeof(struct nvsm_shader_t), compare_shader_t);
}

bool
does_shader_exist(const char* name)
{
  return find_shader(name) != NULL;
}

int
nvsm_load_shader(const char* name, struct nvsm_shader_t** out)
{
  if (name == NULL || nv_strlen(name) == 0) { return -1; }

  struct nvsm_shader_t shader = nv_zero_init(struct nvsm_shader_t);

  nv_strncpy(shader.name, name, sizeof(shader.name) - 1);
  shader.name[sizeof(shader.name) - 1] = '\0';

  struct nvsm_shader_t* shaderptr = (struct nvsm_shader_t*)bsearch(&shader, shader_map, nshaders, sizeof(struct nvsm_shader_t), compare_shader_t);

  if (shaderptr) { *out = shaderptr; }
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

int
read_shader_spirv(const char* output, unsigned** spirv, int* spirvsize)
{
  FILE* f = fopen(output, "rb");
  if (f == NULL)
  {
    nv_push_error("%s : %s", output, strerror(*__errno_location()));
    goto err;
  }

  // I'm sorry i used goto please spare me i have a loving family please no

  nv_safecall_c_fn(fseek(f, 0, SEEK_END));
  size_t fsize = ftell(f);
  if (fsize == (size_t)-1) { goto err; }
  nv_safecall_c_fn(fseek(f, 0, SEEK_SET));

  unsigned* buffer = nv_malloc(fsize);
  if (!buffer) { goto err; }

  nv_assert(fread(buffer, 1, fsize, f) != 0);

  nv_safecall_c_fn(fclose(f));

  *spirv     = buffer;
  *spirvsize = fsize;
  return 0;

err:
  nv_push_error("Could not read in spirv for output path \"%s\"", output);
  if (f)
  {
    nv_safecall_c_fn(fclose(f));
    *spirv     = NULL;
    *spirvsize = 0;
  }
  return -1;
}

#  include "../external/volk/volk.h"
#  include "../include/GPU/pipeline.h"

void
_nvsm_create_shader(VkDevice vkdevice, const unsigned* bytes, int nbytes, struct nvsm_shader_t* out)
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
    if (nv_strncmp(entries[i].stage, "vert", 4) == 0) { shader_map[nshaders + index].stage = VK_SHADER_STAGE_VERTEX_BIT; }
    else if (nv_strncmp(entries[i].stage, "frag", 4) == 0) { shader_map[nshaders + index].stage = VK_SHADER_STAGE_FRAGMENT_BIT; }
    else if (nv_strncmp(entries[i].stage, "tese", 4) == 0) { shader_map[nshaders + index].stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT; }
    else if (nv_strncmp(entries[i].stage, "tesc", 4) == 0) { shader_map[nshaders + index].stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT; }
    else if (nv_strncmp(entries[i].stage, "geom", 4) == 0) { shader_map[nshaders + index].stage = VK_SHADER_STAGE_GEOMETRY_BIT; }
    else if (nv_strncmp(entries[i].stage, "comp", 4) == 0) { shader_map[nshaders + index].stage = VK_SHADER_STAGE_COMPUTE_BIT; }
    else
    {
      nv_push_error("Invalid stage for shader \"%s\". It will not be added.", entries[i].name);
      continue;
    }
    nv_strncpy(shader_map[nshaders + index].name, entries[i].name, 127);
    shader_map[nshaders + index].name[127] = '\0';

    unsigned* spirv     = NULL;
    int       spirvsize = 0;
    if (read_shader_spirv((const char*)entries[i].output_path, &spirv, &spirvsize) != 0)
    {
      if (spirv) nv_free(spirv);
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

nvsm_shader_cache_entry_t*
load_cache(int* count)
{
  nv_assert(count != NULL);

  FILE* f = fopen("shaders.cache", "rb");
  if (f == NULL)
  {
    *count = 0;
    nv_push_error("io error %s", strerror(errno));
    return NULL; // safe to return. nvsm will gracefully handle this.
  }

  if (fread(count, sizeof(int), 1, f) != 1)
  {
    *count = 0;
    nv_push_error("io error %s", strerror(errno));
    nv_safecall_c_fn(fclose(f));
    return NULL;
  }
  nvsm_shader_disk_t* write = (nvsm_shader_disk_t*)nv_calloc(*count * sizeof(nvsm_shader_disk_t));
  if (fread(write, sizeof(nvsm_shader_disk_t), *count, f) != (size_t)(*count))
  {
    *count = 0;
    nv_push_error("io error %s", strerror(errno));
    nv_safecall_c_fn(fclose(f));
    return NULL;
  }

  nvsm_shader_cache_entry_t* entries = nv_calloc(*count * sizeof(nvsm_shader_cache_entry_t));
  for (int i = 0; i < (*count); i++)
  {
    nv_strncpy(entries[i].name, write[i].name, 128);
    nv_strncpy(entries[i].path, write[i].path, 128);
    entries[i].last_modified = write[i].last_modified;
  }

  nv_free(write);

  nv_safecall_c_fn(fclose(f));
  return entries;
}

void
update_cache(const nvsm_shader_cache_entry_t* restrict entries, int count)
{
  FILE* f = fopen("shaders.cache", "wb");
  if (!f)
  {
    nv_push_error("Could not open cache file for update due to %s", strerror(errno));
    return;
  }

  nvsm_shader_disk_t* write = nv_malloc(sizeof(nvsm_shader_disk_t) * count);

  for (int i = 0; i < count; i++)
  {
    nv_strncpy(write[i].name, entries[i].name, 128);
    nv_strncpy(write[i].path, entries[i].path, 128);
    write[i].last_modified = entries[i].last_modified;
  }

  nv_assert(fwrite(&count, sizeof(int), 1, f) == 1);
  nv_assert(fwrite(write, sizeof(nvsm_shader_disk_t), count, f) == (size_t)count);

  nv_free(write);

  nv_safecall_c_fn(fclose(f));
}

void
write_new_cache(const nvsm_shader_entry_t* restrict entries, int count)
{
  FILE* f = fopen("shaders.cache", "wb");
  if (!f) { return; }

  nvsm_shader_disk_t* write = nv_malloc(sizeof(nvsm_shader_disk_t) * count);
  if (!write)
  {
    nv_safecall_c_fn(fclose(f));
    return;
  }

  for (int i = 0; i < count; i++)
  {
    const char* name = entries[i].name;
    const char* path = entries[i].path;
    nv_strncpy(write[i].name, name, nv_strlen(name));
    nv_strncpy(write[i].path, path, nv_strlen(path));
    write[i].last_modified = entries[i].last_modified;
  }

  if (fwrite(&count, sizeof(int), 1, f) != 1)
  {
    nv_safecall_c_fn(fclose(f));
    return;
  }
  if (fwrite(write, sizeof(nvsm_shader_disk_t), count, f) != (size_t)count)
  {
    nv_safecall_c_fn(fclose(f));
    return;
  }

  nv_safecall_c_fn(fclose(f));

  nv_log_info("NVSM cache written successfully");
}

void
create_parent_dirs(const char path[256])
{
  char* last_separator = nv_strrchr(path, PATH_SEP);
  if (last_separator != NULL)
  {
    *last_separator = '\0';

    char buffer[256];

    nv_strcpy(buffer, path);
    int len = nv_strlen(buffer);

    if (buffer[len - 1] == PATH_SEP) { buffer[len - 1] = 0; }

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

time_t
get_mtime(const char* fpath)
{
  struct stat file_stats;
  if (stat(fpath, &file_stats) == 0) { return file_stats.st_mtime; }
  else { nv_push_error("stat error: %s", strerror(errno)); }
  return -1;
}

nvsm_shader_entry_t*
load_all_entries(const char* shader_list_file_path, int* count)
{
  FILE* f = fopen(shader_list_file_path, "r");
  if (f == NULL)
  {
    nv_push_error("Could not open list file for reading: %s", strerror(errno));
    return NULL;
  }

  int                  currallocsize = 16;
  nvsm_shader_entry_t* entries       = nv_malloc(currallocsize * sizeof(nvsm_shader_entry_t));
  nv_assert(entries != NULL);

  char line[256];
  for (int i = 0;; i++)
  {
    if (!fgets(line, 256, f)) { break; }
    else if (nv_strlen(line) == 1)
      continue; // line only contains \n
    line[nv_strcspn(line, "\n")] = 0;

    if (i >= currallocsize)
    {
      currallocsize *= 2;
      entries = realloc(entries, currallocsize * sizeof(nvsm_shader_entry_t));
      nv_assert(entries != NULL);
    }

    entries[*count]            = nv_zero_init(nvsm_shader_entry_t);
    nvsm_shader_entry_t* entry = &entries[*count];

    nv_strncpy(entry->path, line, 256);

    // to get stage + verify that it exists
    FILE* shader_file = fopen(entry->path, "r");
    if (shader_file == NULL)
    {
      nv_push_error("Could not open shader file \"%s\": %s", entry->path, strerror(errno));
      continue;
    }

    nv_assert(fgets(line, 256, shader_file) != NULL);

    const char* li = line;
    while (*li == ' ')
    {
      li++;
    }
    if (nv_strncmp(li, "//", 2) == 0) { sscanf(li, "// output: %s stage: %s name: %s", entry->output_path, entry->stage, entry->name); }
    else
    {
      nv_strcpy(entry->stage, "000");
      nv_strcpy(entry->output_path, "");
      nv_push_error(
          "Shader \"%s\": has invalid or no header.\nHeader Format "
          "-> // output: "
          "{output} stage: {stage} name: {name}",
          entry->path);
    }

    nv_safecall_c_fn(fclose(shader_file));

    entry->last_modified = get_mtime(entry->path);

    (*count)++;
  }

  nv_safecall_c_fn(fclose(f));
  return entries;
}

#if defined(__linux)
// int _nvsm_linux_run() {

// }
// #define system _nvsm_linux_run
#endif

static char* g_Buffer = NULL;

int
compile_shader(const struct nvsm_shader_entry_t* entry)
{
  if (!g_Buffer) { g_Buffer = nv_calloc(1024); }
  char copy[256];
  copy[255] = '\0';
  nv_strncpy(copy, entry->output_path, 255);
  create_parent_dirs(copy);

  nv_snprintf(
      g_Buffer,
      1024,
      "%s %s %s -o %s -S %s",
      shader_compiler,
      shader_compiler_args,
      entry->path,
      nv_strcmp(entry->output_path, "") != 0 ? entry->output_path : "",
      entry->stage);

  if (system(g_Buffer) != 0) { return -1; }
  g_Buffer[1023] = 0;

  return 0;
}

void
nvsm_compile_from_cache(nvsm_shader_entry_t* entries, int nentries, nvsm_shader_cache_entry_t* cacheentries, int cachecount)
{
  int compiled = 0;
  for (int i = 0; i < nentries; i++)
  {
    for (int j = 0; j < cachecount; j++)
    {
      if (nv_strcmp(cacheentries[j].path, entries[i].path) == 0)
      {
        if (cacheentries[j].last_modified != entries[i].last_modified)
        {
          if (compile_shader(&entries[i]) != 0) { nv_push_error("Error while compiling shader \"%s\".", entries[i].path); }
          else { compiled++; }
          cacheentries[j].last_modified = entries[i].last_modified;
        }
        break;
      }
    }
  }
  nv_log_custom(" nvsm: ", "Compiled %i shaders", compiled);
}

void
nvsm_compile_without_cache(nvsm_shader_entry_t* entries, int nentries)
{
  for (int i = 0; i < nentries; i++)
  {
    if (compile_shader(&entries[i]) != 0) { nv_push_error("Error while compiling shader \"%s\".", entries[i].path); }
  }
}

void
nvsm_compile_updated()
{
  nv_log_custom(" nvsm: ", "Shader compilation begin");
  timer stopwatch = timer_begin(0.1);

  int                  nentries = 0;
  nvsm_shader_entry_t* entries  = load_all_entries(list, &nentries);

  int                        cachecount   = 0;
  nvsm_shader_cache_entry_t* cacheentries = load_cache(&cachecount);

  if (cacheentries == NULL)
  {
    nv_push_error("Could not open cache for reading. return.");
    nvsm_compile_without_cache(entries, nentries);
  }
  else { nvsm_compile_from_cache(entries, nentries, cacheentries, cachecount); }

  if (cacheentries == NULL)
  {
    nv_push_error("No cache or modified cache. Writing new cache file...");
    write_new_cache(entries, nentries);
  }
  else { update_cache(cacheentries, nentries); }

#if NVSM_EXECUTABLE != 1
  nvsm_register_all_shaders(device, entries, nentries);
#endif // NVSM_EXECUTABLE != 1

  nv_free(entries);

  if (cacheentries) { nv_free(cacheentries); }

  nv_log_custom(" nvsm: ", "Shader compilation end in %fs", timer_time_since_start(&stopwatch));
}

void
nvsm_compile_all()
{
  nv_log_custom(" nvsm: ", "Shader compilation begin");
  timer stopwatch = timer_begin(0.1);

  int                  count   = 0;
  nvsm_shader_entry_t* entries = load_all_entries(list, &count);

#if NVSM_EXECUTABLE != 1
  nvsm_register_all_shaders(device, entries, count);
#endif // #if NVSM_EXECUTABLE != 1

  for (int i = 0; i < count; i++)
  {
    compile_shader(&entries[i]);
  }

  write_new_cache(entries, count);

  nv_free(entries);

  nv_log_custom(" nvsm: ", "Shader compilation end (Task took %f seconds)", timer_time_since_start(&stopwatch));
}

void
nvsm_shutdown()
{
#if !(NVSM_EXECUTABLE)
  for (int i = 0; i < nshaders; i++)
  {
    struct nvsm_shader_t* shader = &shader_map[i];
    vkDestroyShaderModule(device, shader->shader_module, NOVA_VK_ALLOCATOR);
  }
#endif
}