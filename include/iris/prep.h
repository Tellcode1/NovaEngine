#ifndef NOVA_PREP_H_INCLUDED
#define NOVA_PREP_H_INCLUDED

#include "../std/include/errorcodes.h"
#include <stddef.h>
#include <stdio.h>

/**
 * Resolve all includes of a file and output them into a file.
 */
extern nv_error nv_prep_flatten_file_to_file(const char* file, FILE* out);

/**
 * The buffer will be NULL terminated for your sake
 */
extern nv_error nv_prep_flatten_file_to_buffer(const char* file, char** buffer, size_t* buffer_size);

#endif
