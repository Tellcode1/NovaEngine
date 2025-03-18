#ifndef __NOVA_SM_DEV_H
#define __NOVA_SM_DEV_H

// implementation: preprocessors.c

#include "../GPU/fwdefs.h"
#include "../std/stdafx.h"

NOVA_HEADER_START

typedef struct VkShaderModule_T          VkShaderModule_T;
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
  char              m_name[128];
  VkShaderModule_T* m_shader_module; // the vk shader handle
  unsigned          m_stage;
};

struct nvsm_shader_entry_t
{
  char m_path[256];
  char m_output_path[256];
  char m_name[128];
  char m_stage[4];
  long m_last_modified;
};

struct nvsm_shader_cache_entry_t
{
  char m_path[256];
  char m_output_path[256];
  char m_name[128];
  long m_last_modified;
};

// how the NVSM shader is stored on the disk
struct nvsm_shader_disk_t
{
  char m_name[128];
  char m_path[128];
  long m_last_modified;
};

extern void _nvsm_create_shader(VkDevice_T* __restrict vkdevice, const unsigned* __restrict bytes, size_t nbytes, struct nvsm_shader_t* __restrict out);
extern void nvsm_register_all_shaders(VkDevice device, nvsm_shader_entry_t* entries, int nentries);

NOVA_HEADER_END

#endif //__NOVA_SM_DEV_H
