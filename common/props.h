#ifndef __NOVA_PROGRAM_OPTIONS_H__
#define __NOVA_PROGRAM_OPTIONS_H__

// implementation: core.c

// Place this at the end of your options array to
// let the library know this is the end
#define NV_OPTION_SENTINEL ((nv_option_t){ NV_OP_TYPE_SENTINEL })

#include "stdafx.h"

typedef struct nv_option_t nv_option_t;

typedef enum nv_option_type
{
  NV_OP_TYPE_SENTINEL = 0,
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

// Returns -1 on error. 0 on Success
extern int nv_props_parse(int argc, char* argv[], nv_option_t* options, char* error, size_t error_size);

#endif //__NOVA_PROGRAM_OPTIONS_H__