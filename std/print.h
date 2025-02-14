#ifndef __NOVA_PRINT_H__
#define __NOVA_PRINT_H__

#include "stdafx.h"
#include <stdarg.h>
#include <stdio.h>

NOVA_HEADER_START;

/**
 * default size of the write buffer
 */
#ifndef NOVA_WBUF_SIZE
#  define NOVA_WBUF_SIZE 1024
#endif

/**
 * set the write buffer for printf
 *
 * if buf is NULL and size != 0, it allocates a buffer of 'size' bytes
 * if buf is NULL and size is 0, it allocates a buffer of NOVA_WBUF_SIZE bytes
 *
 * @param buf ptr to buffer or NULL
 * @param size size of the buffer
 */
extern void nv_setwbuf(char* buf, size_t size);

/**
 * set the output stream for printf
 *
 * calls to nv_printf go to this stream. no checks are done.
 *
 * @param stream a valid FILE ptr
 */
extern void nv_setstdout(FILE* stream);

/**
 * prints formatted output to the g_stdstream
 * @return number of characters written
 */
extern size_t nv_printf(const char* fmt, ...);

/**
 * prints formatted output to a file
 */
extern size_t nv_fprintf(FILE* f, const char* fmt, ...);

/**
 * prints no more than max_chars to g_stdstream
 */
extern size_t nv_nprintf(size_t max_chars, const char* fmt, ...);

/**
 * prints formatted output to a string
 *
 * note: not recommended. use nv_snprintf instead.
 */
extern size_t nv_sprintf(char* dest, const char* fmt, ...);

/**
 * prints formatted output using a va_list
 */
extern size_t nv_vprintf(const char* fmt, va_list args);

/**
 * prints formatted output using a va_list to a file
 */
extern size_t nv_vfprintf(FILE* f, const char* fmt, va_list args);

/**
 * prints formatted output to a string, writing no more than max_chars
 */
extern size_t nv_snprintf(char* dest, size_t max_chars, const char* fmt, ...);

/**
 * prints no more than max_chars to g_stdstream using a va_list
 */
extern size_t nv_vnprintf(size_t max_chars, va_list args, const char* fmt);

/**
 * prints no more than max_chars to a string using a va_list
 */
extern size_t nv_vsnprintf(char* dest, size_t max_chars, const char* fmt, va_list args);

/**
 * the core print function
 *
 * all nv_printf* funcs call this in the end.
 * stops formatting when max_chars is hit.
 *
 * @param dest destination buffer or FILE ptr
 * @return number of characters written
 */
extern size_t _nv_vsfnprintf(void* dest, bool is_file, size_t max_chars, const char* fmt, va_list args);

NOVA_HEADER_END;

#endif // __NOVA_PRINT_H__