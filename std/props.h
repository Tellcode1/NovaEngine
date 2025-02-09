#ifndef __NOVA_PROGRAM_OPTIONS_H__
#define __NOVA_PROGRAM_OPTIONS_H__

// implementation: core.c

#include "stdafx.h"

typedef struct nv_option_t nv_option_t;

typedef enum nv_option_type
{
  NV_OP_TYPE_BOOL, // bool
  NV_OP_TYPE_STRING,
  NV_OP_TYPE_INT,
  NV_OP_TYPE_FLOAT,
  NV_OP_TYPE_DOUBLE
} nv_option_type;

struct nv_option_t
{
  nv_option_type type;
  const char*    short_name; // a one character option
  const char*    long_name;  // a string option
  void*          value;      // Pointer to where the value will be stored
                             // For strings, pass the buffer instead.
  size_t buffer_size;        // The size of the char buffer when option type is string
};

/**
 * @brief parse command-line options.
 *
 * @param error buffer for error messages.
 * @param error_size size of the error buffer.
 */
extern int nv_props_parse(int argc, char* argv[], nv_option_t* options, int noptions, char* error, size_t error_size);

extern void nv_props_gen_and_print_help(const nv_option_t* options, int noptions);

#endif //__NOVA_PROGRAM_OPTIONS_H__