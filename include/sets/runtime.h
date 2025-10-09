#ifndef SETS_RUNTIME_H
#define SETS_RUNTIME_H
#ifdef __cplusplus
extern "C"
{
#endif

#include "../std/include/containers/hashmap.h"
#include "sets.h"

#define SETS_GET_INT_WITH_FALLBACK(name, fallback) sets_get_setting_defined(name) ? sets_get_setting_int(name) : (fallback)
#define SETS_GET_DOUBLE_WITH_FALLBACK(name, fallback) sets_get_setting_defined(name) ? sets_get_setting_double(name) : (fallback)
#define SETS_GET_STRING_WITH_FALLBACK(name, fallback) sets_get_setting_defined(name) ? sets_get_setting_string(name) : (fallback)
#define SETS_GET_FLAG_WITH_FALLBACK(name, fallback) sets_get_setting_defined(name) ? sets_get_setting_flag(name) : (fallback)

  extern nv_hashmap_t sets_name_variable_pairs_;
  static const char*  sets_configuration_files[] = {
    "cfg/engine.sets",
    "cfg/renderer.sets",
  };

  static inline nv_error
  sets_init(void)
  {
    nv_error code = nv_hashmap_init(16, sizeof(const char*), sizeof(sets_var_t), nv_hash_fnv1a, nv_allocator_c, NULL, &sets_name_variable_pairs_);
    if (code != NV_SUCCESS)
    {
      return code;
    }

    sets_var_t* vars;
    size_t      nvars;

    for (size_t i = 0; i < nv_arrlen(sets_configuration_files); i++)
    {
      code = sets_parse_file(sets_configuration_files[i], &vars, &nvars);
      if (code != NV_SUCCESS)
        return code;

      for (size_t vindex = 0; vindex < nvars; vindex++)
      {
        nv_hashmap_insert_or_replace(&sets_name_variable_pairs_, vars[vindex].name, (void*)&vars[vindex]);
      }
    }

    return NV_SUCCESS;
  }

  static inline void
  sets_shutdown()
  {
    nv_hashmap_destroy(&sets_name_variable_pairs_);
  }

  /**
   * Returns NULL on error or variable not defined.
   */
  static inline const sets_var_t*
  sets_get_setting(const char* name)
  {
    const sets_var_t* var = (sets_var_t*)nv_hashmap_find(&sets_name_variable_pairs_, name);
    return var;
  }

  static inline bool
  sets_get_setting_defined(const char* name)
  {
    return sets_get_setting(name) != NULL;
  }

  static inline bool
  sets_get_typed_value(const char* name, sets_var_type type, void* out)
  {
    const sets_var_t* var = (const sets_var_t*)nv_hashmap_find(&sets_name_variable_pairs_, name);
    if (!var)
    {
      return false;
    }

    switch (type)
    {
      case SETS_VAR_TYPE_INT: *(int*)out = var->value.num; break;
      case SETS_VAR_TYPE_DOUBLE: *(double*)out = var->value.dbl; break;
      case SETS_VAR_TYPE_STR: *(const char**)out = var->value.str; break;
      case SETS_VAR_TYPE_BOOL: *(bool*)out = var->value.flag; break;
    }

    return true;
  }

  /**
   * Returns 0 on error or variable not defined.
   * If DEBUG mode is enabled, an error message will also be printed.
   */
  static inline int
  sets_get_setting_int(const char* name)
  {
    const sets_var_t* var = (sets_var_t*)nv_hashmap_find(&sets_name_variable_pairs_, name);
    if (var)
    {
      return var->value.num;
    }
    nv_log_error("Variable with name: %s not found\n", name);
    return 0;
  }

  /**
   * Returns 0.0 on error or variable not defined.
   * If DEBUG mode is enabled, an error message will also be printed.
   */
  static inline double
  sets_get_setting_double(const char* name)
  {
    const sets_var_t* var = (sets_var_t*)nv_hashmap_find(&sets_name_variable_pairs_, name);
    if (var)
    {
      return var->value.dbl;
    }
    nv_log_error("Variable with name: %s not found\n", name);
    return 0.0;
  }

  /**
   * Returns NULL on error or variable not defined.
   * If DEBUG mode is enabled, an error message will also be printed.
   */
  static inline const char*
  sets_get_setting_string(const char* name)
  {
    const sets_var_t* var = (sets_var_t*)nv_hashmap_find(&sets_name_variable_pairs_, name);
    if (var)
    {
      return var->value.str;
    }
    nv_log_error("Variable with name: %s not found\n", name);
    return NULL;
  }

  /**
   * Returns false on error or variable not defined.
   * If DEBUG mode is enabled, an error message will also be printed.
   */
  static inline int
  sets_get_setting_flag(const char* name)
  {
    const sets_var_t* var = (sets_var_t*)nv_hashmap_find(&sets_name_variable_pairs_, name);
    if (var)
    {
      return var->value.flag;
    }
    nv_log_error("Variable with name: %s not found\n", name);
    return false;
  }

#ifdef __cplusplus
}
#endif
#endif // SETS_RUNTIME_H
