#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

#ifndef WIN32
#include <unistd.h>
#endif

#ifdef WIN32
#define stat _stat
#endif

#if !(NVSM_EXECUTABLE)

struct nvsm_shader_t* shader_map = NULL;
int                   nshaders   = 0;

#endif

#include "../common/stdafx.h"
#include "../include/engine/shadermanagerdev.h"

const char* shader_compiler      = "glslangValidator";
const char* shader_compiler_args = " -V ";
const char* list                 = "../compilelist.txt";

#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#define PATH_SEP '\\'
#else
#include <sys/stat.h>
#include <sys/types.h>
#define MKDIR(path) (mkdir(path, 0777))
#define PATH_SEP '/'
#endif

#define NVSM_HAS_FLAG(flag) (nv_strcmp(argv[i], flag) == 0)

static inline void
_nvsm_log_error(const char* fn, const char* fmt, ...)
{
  const char* preceder  = " nvsm error: ";
  const char* succeeder = "\n";
  va_list     args;
  va_start(args, fmt);
  _nv_log(args, fn, succeeder, preceder, fmt, 1);
  va_end(args);
}

#define nvsm_log_error(err, ...) _nvsm_log_error(__PRETTY_FUNCTION__, err, ##__VA_ARGS__)

#if defined(NVSM)

#include "../common/printf.h"
#include "../common/string.h"
#include "../common/timer.h"

#if NVSM_EXECUTABLE

#include "../include/engine/shadermanager.h"

#define CMD_HELP_MSG                                                                                                                                                          \
  "cmd can be any of:\n\
<default> compile: compile only those that have been changed since last ran,\n\
compile-force: forcefully compile all shaders in list file,\n\
\n"

int
main(int argc, char* argv[])
{
  for (int i = 0; i < argc; i++)
  {
    if (NVSM_HAS_FLAG("help") || NVSM_HAS_FLAG("--h") || NVSM_HAS_FLAG("-h") || NVSM_HAS_FLAG("--help") || NVSM_HAS_FLAG("-help"))
    {
      nv_printf("usage: %s <compile list path = \"../compilelist.txt\"> <cmd "
                "= compile>\n",
          argv[0]);
      nv_printf("%s", CMD_HELP_MSG);
      return 0;
    }
  }

  const char* cmd = (argc < 3) ? "compile" : argv[2];

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
    nv_printf("usage: %s <compile list path = \"../compilelist.txt\"> <cmd = "
              "compile>\n",
        argv[0]);
    nv_printf("%s", CMD_HELP_MSG);
    return -1;
  }

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
  if (nshaders > 0)
  {
    nv_memcpy(new_map, shader_map, nshaders * sizeof(struct nvsm_shader_t));
  }
  if (shader_map != NULL)
    nv_free(shader_map);
  shader_map = new_map;

  struct nvsm_shader_t add;
  nv_strcpy(add.name, entry.name);
  shader_map[nshaders] = add;

  *dst                 = &shader_map[nshaders];

  nshaders++;
  // map is sorted after all shaders are registered.

  qsort(shader_map, nshaders, sizeof(struct nvsm_shader_t), compare_shader_t);
}

struct nvsm_shader_t*
find_shader(const char* name)
{
  struct nvsm_shader_t shader = {};

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
  if (name == NULL || nv_strlen(name) == 0)
  {
    return -1;
  }

  struct nvsm_shader_t shader = {};

  nv_strncpy(shader.name, name, sizeof(shader.name) - 1);
  shader.name[sizeof(shader.name) - 1] = '\0';

  struct nvsm_shader_t* shaderptr      = (struct nvsm_shader_t*)bsearch(&shader, shader_map, nshaders, sizeof(struct nvsm_shader_t), compare_shader_t);

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

int
read_shader_spirv(const char* output, unsigned** spirv, int* spirvsize)
{
  FILE* f = fopen(output, "rb");
  if (f == NULL)
  {
    goto err;
  }

  // I'm sorry i used goto please spare me i have a loving family please no

  fseek(f, 0, SEEK_END);
  size_t fsize = ftell(f);
  if (fsize == (size_t)-1)
  {
    goto err;
  }
  fseek(f, 0, SEEK_SET);

  unsigned* buffer = nv_malloc(fsize);
  if (!buffer)
  {
    goto err;
  }

  fread(buffer, 1, fsize, f);

  fclose(f);

  *spirv     = buffer;
  *spirvsize = fsize;
  return 0;

err:
  nvsm_log_error("Could not read in spirv for output path \"%s\"", output);
  if (f)
  {
    fclose(f);
    *spirv     = NULL;
    *spirvsize = 0;
  }
  return -1;
}

#include "../external/volk/volk.h"
#include "../include/GPU/pipeline.h"

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

  int index  = 0;
  for (int i = 0; i < nentries; i++)
  {
    if (nv_strncmp(entries[i].stage, "vert", 4) == 0)
    {
      shader_map[nshaders + index].stage = VK_SHADER_STAGE_VERTEX_BIT;
    }
    else if (nv_strncmp(entries[i].stage, "frag", 4) == 0)
    {
      shader_map[nshaders + index].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    else if (nv_strncmp(entries[i].stage, "tese", 4) == 0)
    {
      shader_map[nshaders + index].stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    }
    else if (nv_strncmp(entries[i].stage, "tesc", 4) == 0)
    {
      shader_map[nshaders + index].stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    }
    else if (nv_strncmp(entries[i].stage, "geom", 4) == 0)
    {
      shader_map[nshaders + index].stage = VK_SHADER_STAGE_GEOMETRY_BIT;
    }
    else if (nv_strncmp(entries[i].stage, "comp", 4) == 0)
    {
      shader_map[nshaders + index].stage = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    else
    {
      nvsm_log_error("Invalid stage for shader \"%s\". It will not be added.", entries[i].name);
      continue;
    }
    nv_strncpy(shader_map[nshaders + index].name, entries[i].name, 127);
    shader_map[nshaders + index].name[127] = '\0';

    unsigned* spirv;
    int       spirvsize = 0;
    if (read_shader_spirv((const char*)entries[i].output_path, &spirv, &spirvsize) != 0)
    {
      nv_free(spirv);
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
  FILE* f = fopen("shaders.cache", "rb");
  if (f == NULL)
  {
    *count = 0;
    return NULL;
  }

  nv_assert(fread(count, sizeof(int), 1, f) == 1);
  nvsm_shader_disk_t* write = (nvsm_shader_disk_t*)calloc(*count, sizeof(nvsm_shader_disk_t));
  nv_assert(fread(write, sizeof(nvsm_shader_disk_t), *count, f) == (size_t)(*count));

  nvsm_shader_cache_entry_t* entries = calloc(*count, sizeof(nvsm_shader_cache_entry_t));
  for (int i = 0; i < (*count); i++)
  {
    nv_strncpy(entries[i].name, write[i].name, 128);
    nv_strncpy(entries[i].path, write[i].path, 128);
    entries[i].last_modified = write[i].last_modified;
  }

  nv_free(write);

  fclose(f);
  return entries;
}

void
update_cache(const nvsm_shader_cache_entry_t* restrict entries, int count)
{
  FILE* f = fopen("shaders.cache", "wb");
  if (!f)
  {
    nvsm_log_error("Could not open cache file for update. return.");
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

  fclose(f);
}

void
write_new_cache(const nvsm_shader_entry_t* restrict entries, int count)
{
  FILE* f = fopen("shaders.cache", "wb");
  nv_assert(f != NULL);

  nvsm_shader_disk_t* write = nv_malloc(sizeof(nvsm_shader_disk_t) * count);

  for (int i = 0; i < count; i++)
  {
    nv_strncpy(write[i].name, entries[i].name, 128);
    nv_strncpy(write[i].path, entries[i].path, 128);
    write[i].last_modified = entries[i].last_modified;
  }

  nv_assert(fwrite(&count, sizeof(int), 1, f) == 1);
  nv_assert(fwrite(write, sizeof(nvsm_shader_disk_t), count, f) == (size_t)count);

  fclose(f);

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

#include <errno.h>
time_t
get_mtime(const char* fpath)
{
  struct stat file_stats;
  if (stat(fpath, &file_stats) == 0)
  {
    return file_stats.st_mtime;
  }
  else
  {
    nvsm_log_error("stat error %i", errno);
  }
  return -1;
}

nvsm_shader_entry_t*
load_all_entries(const char* shader_list_file_path, int* count)
{
  FILE* f = fopen(shader_list_file_path, "r");
  if (f == NULL)
  {
    nvsm_log_error("Could not open list file for reading. Is the path correct?");
    return NULL;
  }

  int                  currallocsize = 16;
  nvsm_shader_entry_t* entries       = nv_malloc(currallocsize * sizeof(nvsm_shader_entry_t));
  nv_assert(entries != NULL);

  char line[256];
  for (int i = 0;; i++)
  {
    if (!fgets(line, 256, f))
    {
      break;
    }
    else if (nv_strlen(line) == 1)
      continue; // line only contains \n
    line[nv_strcspn(line, "\n")] = 0;

    if (i >= currallocsize)
    {
      currallocsize *= 2;
      entries = realloc(entries, currallocsize * sizeof(nvsm_shader_entry_t));
      nv_assert(entries != NULL);
    }

    entries[*count]            = (nvsm_shader_entry_t){};
    nvsm_shader_entry_t* entry = &entries[*count];

    nv_strncpy(entry->path, line, 256);

    // to get stage + verify that it exists
    FILE* shader_file = fopen(entry->path, "r");
    if (shader_file == NULL)
    {
      nvsm_log_error("Could not open shader file \"%s\". Are you sure that it exists?", entry->path);
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
      nvsm_log_error("Shader \"%s\": has invalid or no header.\nHeader Format "
                     "-> // output: "
                     "{output} stage: {stage} name: {name}",
          entry->path);
    }

    fclose(shader_file);

    entry->last_modified = get_mtime(entry->path);

    (*count)++;
  }

  fclose(f);
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
  if (!g_Buffer)
  {
    g_Buffer = calloc(1, 1024);
  }
  char copy[256];
  copy[255] = '\0';
  nv_strncpy(copy, entry->output_path, 255);
  create_parent_dirs(copy);

  nv_snprintf(g_Buffer, 1024, "%s %s %s -o %s -S %s", shader_compiler, shader_compiler_args, entry->path, nv_strcmp(entry->output_path, "") != 0 ? entry->output_path : "",
      entry->stage);

  if (system(g_Buffer) != 0)
  {
    return -1;
  }
  g_Buffer[1023] = 0;

  return 0;
}

void
nvsm_compile_from_cache(nvsm_shader_entry_t* entries, int nentries, nvsm_shader_cache_entry_t* cacheentries, int cachecount)
{
  for (int i = 0; i < nentries; i++)
  {
    for (int j = 0; j < cachecount; j++)
    {
      if (nv_strcmp(cacheentries[j].path, entries[i].path) == 0)
      {
        if (cacheentries[j].last_modified != entries[i].last_modified)
        {
          if (compile_shader(&entries[i]) != 0)
          {
            nvsm_log_error("Error while compiling shader \"%s\".", entries[i].path);
          }
          cacheentries[j].last_modified = entries[i].last_modified;
        }
        break;
      }
    }
  }
}

void
nvsm_compile_without_cache(nvsm_shader_entry_t* entries, int nentries)
{
  for (int i = 0; i < nentries; i++)
  {
    if (compile_shader(&entries[i]) != 0)
    {
      nvsm_log_error("Error while compiling shader \"%s\".", entries[i].path);
    }
  }
}

void
nvsm_compile_updated()
{
  nv_log_custom(" nvsm: ", "Shader compilation begin");
  timer                      stopwatch    = timer_begin(0.1);

  int                        nentries     = 0;
  nvsm_shader_entry_t*       entries      = load_all_entries(list, &nentries);

  int                        cachecount   = 0;
  nvsm_shader_cache_entry_t* cacheentries = load_cache(&cachecount);

  if (cacheentries == NULL)
  {
    nvsm_log_error("Could not open cache for reading. return.");
    nvsm_compile_without_cache(entries, nentries);
  }
  else
  {
    nvsm_compile_from_cache(entries, nentries, cacheentries, cachecount);
  }

  if (cacheentries == NULL)
  {
    nvsm_log_error("No cache or modified cache. Writing new cache file...");
    write_new_cache(entries, nentries);
  }
  else
  {
    update_cache(cacheentries, nentries);
  }

#if NVSM_EXECUTABLE != 1
  nvsm_register_all_shaders(device, entries, nentries);
#endif // NVSM_EXECUTABLE != 1

  nv_free(entries);

  if (cacheentries)
    nv_free(cacheentries);

  nv_log_custom(" nvsm: ", "Shader compilation end (Task took %f seconds)", timer_time_since_start(&stopwatch));
}

void
nvsm_compile_all()
{
  nv_log_custom(" nvsm: ", "Shader compilation begin");
  timer                stopwatch = timer_begin(0.1);

  int                  count     = 0;
  nvsm_shader_entry_t* entries   = load_all_entries(list, &count);

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
    nvsm_shader_t* shader = &shader_map[i];
    vkDestroyShaderModule(device, shader->shader_module, NOVA_VK_ALLOCATOR);
  }
#endif
}

#endif // NVSM

#if (FONTC)

#include <stdio.h>
#include <stdlib.h>

#include "../common/printf.h"
#include "../common/string.h"
#include "../common/timer.h"

#include "../include/engine/fontc.h"

#if (FONTC_EXECUTABLE)
#include "../common/timer.h"

static const char* FONTC_HELP_MSG = "usage:\n./fontc < Font file path to bake "
                                    "> (optionally, ) -o < output file >";

int
main(int argc, char* argv[])
{
  if (argc < 2)
  {
    nv_log_custom("help", "%s", FONTC_HELP_MSG);
    return -1;
  }

  const char* file = argv[1];
  const char* out;

  if (argc > 2)
  {
    out = argv[3];
  }
  else
  {
    out = "bakedfont";
  }

  char buffer[512] = {};
  getcwd(buffer, 511);
  nv_strcat(buffer, "/");
  nv_strcat(buffer, argv[1]);
  buffer[511] = 0;

  nv_log_info("Attempting to load from %s", buffer);

  timer tm = timer_begin(__FLT_MAX__);
  nv_log_info("start");
  fontc_bake_font(file, out);
  nv_log_info("finished in %.2f s", timer_time_since_start(&tm));
  return 0;
}

#endif // FONTC_EXECUTABLE

fontc_atlas_t
fontc_atlas_init(int init_w, int init_h)
{
  fontc_atlas_t atlas      = {};

  atlas.width              = init_w;
  atlas.height             = init_h;
  atlas.next_x             = 0;
  atlas.next_y             = 0;
  atlas.current_row_height = 0;
  atlas.data               = calloc(init_w, init_h);

  return atlas;
}

bool
fontc_atlas_add_image(fontc_atlas_t* __restrict__ atlas, int w, int h, const unsigned char* __restrict__ data, int* __restrict__ x, int* __restrict__ y)
{
  const int padding = 4;
  const int prev_h = atlas->height, prev_w = atlas->width;
  bool      realloc_needed = 0;
  if (w > atlas->width)
  {
    // ! This doesn't work because the old image is not correctly copied by
    // realloc ! It'll be fixed by copying over the data row by row probably
    // doesn't need fixing right now, will delay it for another eon
    // TODO: FIXME
    atlas->width   = w;
    realloc_needed = 1;
  }

  if (atlas->next_x + w + padding > atlas->width)
  {
    atlas->next_x = 0;
    atlas->next_y += atlas->current_row_height + padding;
    atlas->current_row_height = 0;
  }

  if (atlas->next_y + h + padding > atlas->height)
  {
    atlas->height  = NVM_MAX(atlas->height * 2, atlas->next_y + h + padding);
    realloc_needed = 1;
  }

  if (realloc_needed)
  {
    atlas->data = realloc(atlas->data, atlas->width * atlas->height);
    nv_memset(atlas->data + prev_w * prev_h, 0, (atlas->width - prev_w) * (atlas->height - prev_h));
  }

  for (int y = 0; y < h; y++)
  {
    nv_memcpy(atlas->data + (atlas->next_x + (atlas->next_y + y) * atlas->width), data + (y * w), w);
  }

  *x = atlas->next_x;
  *y = atlas->next_y;

  atlas->next_x += w + padding;
  atlas->current_row_height = NVM_MAX(atlas->current_row_height, h + padding);

  return 0;
}

void
fontc_read_font(const char* path, fontc_file_t* file)
{
  FILE* f = fopen(path, "rb");
  if (!f)
  {
    nv_log_error("Failed to open font file for reading");
    nv_log_error("Are you passing in the path to the baked file?");
    return;
  }
  fread(&file->header, sizeof(fontc_header), 1, f);
  if (file->header.magic != FONTC_MAGIC)
  {
    nv_log_error("Invalid magic number for font file");
    return;
  }

  size_t total_glyph_size          = file->header.numglyphs * sizeof(fontc_glyph);

  file->glyphs                     = nv_malloc(total_glyph_size);
  file->bitmap                     = nv_malloc(file->header.bmpwidth * file->header.bmpheight);
  fontc_glyph*   compressed_glyphs = nv_malloc(file->header.glyphs_compressed_sz);
  unsigned char* compressed_image  = nv_malloc(file->header.img_compressed_sz);

  fread(compressed_glyphs, file->header.glyphs_compressed_sz, 1, f);
  fread(compressed_image, file->header.img_compressed_sz, 1, f);

  nv_assert(nv_bufdecompress(compressed_glyphs, file->header.glyphs_compressed_sz, file->glyphs, total_glyph_size) != -1);
  nv_assert(nv_bufdecompress(compressed_image, file->header.img_compressed_sz, file->bitmap, file->header.bmpwidth * file->header.bmpheight) != -1);

  nv_free(compressed_glyphs);
  nv_free(compressed_image);

  fclose(f);
}

#include <freetype2/ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

void
fontc_bake_font(const char* font_path, const char* out)
{
  FT_Library lib;
  FT_Face    face;
  if (FT_Init_FreeType(&lib))
  {
    nv_log_error("Failed to initialize ft");
  }

  if (FT_New_Face(lib, font_path, 0, &face))
  {
    nv_log_error("Failed to load font file: %s", font_path);
    FT_Done_FreeType(lib);
    return;
  }
  FT_Set_Pixel_Sizes(face, 0, 256);

  fontc_file_t  file      = {};
  fontc_atlas_t atlas     = fontc_atlas_init(4096, 4096);

  file.header.magic       = FONTC_MAGIC;

  file.header.line_height = -face->size->metrics.height / (float)face->height;

  fontc_glyph* glyphs     = nv_malloc(sizeof(fontc_glyph) * 256);
  if (!glyphs)
  {
    nv_log_error("Failed to allocate memory for glyphs");
    return;
  }

  int glyph_count = 0;

  for (int i = 0; i < 256; i++)
  {
    FT_UInt glyph_index = FT_Get_Char_Index(face, i);
    if (glyph_index == 0)
      continue;

    if (FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT))
      continue;

    if (i == ' ')
    {
      file.header.space_width = (float)face->glyph->metrics.horiAdvance / (float)face->units_per_EM;
      continue;
    }

    FT_Render_Glyph(face->glyph, FT_RENDER_MODE_SDF);
    FT_GlyphSlot         g      = face->glyph;

    const int            w      = g->bitmap.width;
    const int            h      = g->bitmap.rows;
    const unsigned char* buffer = g->bitmap.buffer;

    int                  x, y;
    if (fontc_atlas_add_image(&atlas, w, h, buffer, &x, &y))
    {
      nv_log_error("Atlas error");
      continue;
    }

    FT_Glyph gl;
    FT_Get_Glyph(face->glyph, &gl);

    FT_BBox box;
    FT_Glyph_Get_CBox(gl, FT_GLYPH_BBOX_UNSCALED, &box);

    FT_Done_Glyph(gl);

    fontc_glyph glyph   = { .codepoint = i,
        .x0                            = box.xMin / (float)face->units_per_EM,
        .x1                            = box.xMax / (float)face->units_per_EM,
        .y0                            = box.yMin / (float)face->units_per_EM,
        .y1                            = box.yMax / (float)face->units_per_EM,
        .l                             = (float)(x) / atlas.width,
        .r                             = (float)(x + w) / atlas.width,
        .b                             = (float)(y + h) / atlas.height,
        .t                             = (float)(y) / atlas.height,
        .advance                       = (float)face->glyph->metrics.horiAdvance / (float)face->units_per_EM };

    glyphs[glyph_count] = glyph;
    glyph_count++;
  }

  FT_Done_Face(face);
  FT_Done_FreeType(lib);

  file.header.bmpwidth             = atlas.width;
  file.header.bmpheight            = atlas.height;
  file.header.numglyphs            = glyph_count;

  size_t         image_o_size      = atlas.width * atlas.height;
  size_t         glyph_o_size      = file.header.numglyphs * sizeof(fontc_glyph);

  unsigned char* compressed_image  = nv_malloc(image_o_size);
  fontc_glyph*   compressed_glyphs = nv_malloc(glyph_o_size);

  nv_bufcompress(atlas.data, image_o_size, compressed_image, &image_o_size);
  nv_bufcompress(glyphs, glyph_o_size, compressed_glyphs, &glyph_o_size);

  nv_free(atlas.data);
  nv_free(glyphs);

  file.glyphs                      = compressed_glyphs;

  file.header.img_compressed_sz    = image_o_size;
  file.header.glyphs_compressed_sz = glyph_o_size;

  FILE* f                          = fopen(out, "wb");
  fwrite(&file.header, sizeof(fontc_header), 1, f);
  fwrite(compressed_glyphs, glyph_o_size, 1, f);
  fwrite(compressed_image, image_o_size, 1, f);
  fclose(f);

  size_t bytes_written = 0;

  bytes_written += sizeof(fontc_header);
  bytes_written += glyph_o_size;
  bytes_written += image_o_size;

  char buf[128];
  nv_btoa(bytes_written, 1, buf, 127);
  buf[127] = 0;
  nv_log_info("Wrote %s to %s", buf, out);

  nv_free(compressed_image);
  nv_free(compressed_glyphs);
}

#endif // FONTC