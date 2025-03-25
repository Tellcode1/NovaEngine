#ifndef __NOVA_SM_DEV_H
#define __NOVA_SM_DEV_H

// implementation: preprocessors.c

#include "../GPU/vkstdafx.h"
#include "../std/stdafx.h"

NOVA_HEADER_START

typedef struct nvsm_shader_entry_t       nvsm_shader_entry_t;
typedef struct nvsm_shader_disk_t        nvsm_shader_disk_t;
typedef struct nvsm_shader_cache_entry_t nvsm_shader_cache_entry_t;

extern const char* shader_compiler;
extern const char* shader_compiler_args;
extern const char* list;

extern struct nvsm_shader_t* shader_map;
extern int                   nshaders;

struct nvsm_shader_t
{
  char           name[128];
  VkShaderModule shader_module; // the vk shader handle
  unsigned       stage;
};

struct nvsm_shader_entry_t
{
  char path[256];
  char output_path[256];
  char name[128];
  char stage[4];
  long last_modified;
};

struct nvsm_shader_cache_entry_t
{
  uint32_t canary; // == 0xDEADBEEF
  char     path[256];
  char     output_path[256];
  char     name[128];
  long     last_modified;
};

// how the NVSM shader is stored on the disk
struct nvsm_shader_disk_t
{
  char name[128];
  char path[128];
  long last_modified;
};

extern void _nvsm_create_shader(VkDevice vkdevice, const u32* bytes, size_t nbytes, struct nvsm_shader_t* out);
extern void nvsm_register_all_shaders(VkDevice device, nvsm_shader_entry_t* entries, int nentries);

NOVA_HEADER_END

#endif //__NOVA_SM_DEV_H
