#ifndef SETS_H
#define SETS_H
#ifdef __cplusplus
extern "C"
{
#endif

#include "../std/include/error.h"
#include <stdbool.h>
#include <stddef.h>

  typedef union sets_var_value
  {
    int         num;
    double      dbl;
    const char* str;
    bool        flag;
  } sets_var_value_t;

  typedef enum sets_var_type
  {
    SETS_VAR_TYPE_INT,
    SETS_VAR_TYPE_DOUBLE,
    SETS_VAR_TYPE_STR,
    SETS_VAR_TYPE_BOOL, // /flag
  } sets_var_type;

  typedef struct sets_var
  {
    char*            name;
    sets_var_type    type;
    sets_var_value_t value;
  } sets_var_t;

  typedef enum sets_error
  {
    SETS_SUCCESS                  = 0,
    SETS_INVALID_VARIABLE_NAME    = 1,
    SETS_EXPECTED_EQUALS          = 2,
    SETS_EXPECTED_INITIALIZER     = 3,
    SETS_UNRECOGNIZED_INITIALIZER = 4,
    SETS_INVALID_INITIALIZER      = 5,
    SETS_ENDING_QUOTE_MISSING     = 6,
    SETS_UNKNOWN
  } sets_error;

  static inline const char*
  sets_var_type_to_string(sets_var_type type)
  {
    switch (type)
    {
      case SETS_VAR_TYPE_INT: return "int";
      case SETS_VAR_TYPE_STR: return "str";
      case SETS_VAR_TYPE_DOUBLE: return "double";
      case SETS_VAR_TYPE_BOOL: return "bool";
      default: return "Unknown";
    }
  }

  static inline const char*
  sets_error_to_string(sets_error error)
  {
    switch (error)
    {
      case SETS_SUCCESS: return "Success";
      case SETS_INVALID_VARIABLE_NAME: return "Invalid Variable Name";
      case SETS_EXPECTED_EQUALS: return "Expected =";
      case SETS_EXPECTED_INITIALIZER: return "Expected Initializer";
      case SETS_UNRECOGNIZED_INITIALIZER: return "Unrecognized Initializer";
      case SETS_INVALID_INITIALIZER: return "Invalid Initializer";
      case SETS_ENDING_QUOTE_MISSING: return "Ending Quote Missing";
      case SETS_UNKNOWN:
      default: return "Unknown";
    }
  }

  /**
   * resultarrp is set to a pointer to an array of all parsed variables.
   * resultarrsize is set to the number of parsed variables.
   * Acquired list must be freed through nv_free()
   */
  extern nv_error sets_parse(const char* input, sets_var_t** resultarrp, size_t* resultarrsize);

  /**
   * Parse a file to recieve all configuration variables
   * All includes are parsed first through sets_parse_includes_to_file, don't bother doing it yourself.
   * resultarrp is filled with a pointer to an array of all parsed variables.
   * resultarrsize is set to the number of parsed variables.
   * Acquired list must be freed through nv_free()
   */
  extern nv_error sets_parse_file(const char* input, sets_var_t** resultarrp, size_t* resultarrsize);

  /**
   * Resolve all includes of a file and output them into a file.
   */
  extern nv_error sets_parse_includes_to_file(const char* file, FILE* out);

  /**
   * Resolve all includes of a file and output them to a string buffer
   * buffer is set to a pointer to a dynamically allocated buffer
   * Acquired buffer must be freed through nv_free()
   * The buffer will be NULL terminated for your sake
   */
  extern nv_error sets_parse_includes_to_buffer(const char* file, char** buffer, size_t* buffer_size);

#ifdef __cplusplus
}
#endif
#endif // SETS_H