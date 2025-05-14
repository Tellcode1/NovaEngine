#ifndef NOVA_PREP_H_INCLUDED_
#define NOVA_PREP_H_INCLUDED_

#include "../std/errorcodes.h"
#include "../std/stdafx.h"

NOVA_HEADER_START

/**
 * Resolve all includes of a file and output them into a file.
 */
extern nv_error nv_prep_flatten_file_to_file(const char* file, FILE* out);

/**
 * The buffer will be NULL terminated for your sake
 */
extern nv_error nv_prep_flatten_file_to_buffer(const char* file, char** buffer, size_t* buffer_size);

NOVA_HEADER_END

#endif
