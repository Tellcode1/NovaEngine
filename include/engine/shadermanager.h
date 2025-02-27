#ifndef __NOVA_SHADERMANAGER_H__
#define __NOVA_SHADERMANAGER_H__

// implementation: preprocessors.c

#include "../../std/stdafx.h"

NOVA_HEADER_START

#ifndef NVSM_EXECUTABLE
#  define NVSM_EXECUTABLE 0
#endif

typedef struct nvsm_pipeline nvsm_pipeline;

extern void nvsm_compile_updated(void);
extern void nvsm_compile_all(void);

extern void nvsm_shutdown(void);

extern void        nvsm_set_list_file(const char* path);
extern void        nvsm_set_shader_compiler(const char* exec);
extern void        nvsm_set_shader_compiler_args(const char* args);
extern char const* nvsm_get_shader_compiler_args(void);

typedef struct nvsm_shader_t nvsm_shader_t; // You can still use struct nvsm_shader_t but it's less effort (Not neccessarily cleaner)

/// @brief nvsm_shader_t *shadr; nvsm_load_shader("Unlit/vertex", &shadr);
/// @return -1 on failure. 0 on success
extern int nvsm_load_shader(const char* name, struct nvsm_shader_t** out);

/*
    You can pass NULL to output_name to signify that no output file should be created and it should not be added to the cache.
    The shader will then live entirely in memory but can still be referenced(nvsm_load_shader) by name.
*/
extern int nvsm_load_shader_from_disk(const char* path, struct nvsm_shader_t** out);

NOVA_HEADER_END

#endif //__NOVA_SHADERMANAGER_H__
