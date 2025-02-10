#ifndef __NOVA_PRINT_H__
#define __NOVA_PRINT_H__

#include "stdafx.h"
#include <stdarg.h>
#include <stdio.h>

NOVA_HEADER_START;

/**
 * @brief default size of write buffer
 */
#ifndef NOVA_WBUF_SIZE
#  define NOVA_WBUF_SIZE 1024
#endif

/**
 * @brief Sets the write buffer for the print functions.
 *
 * if buf is NULL and size != 0, a buffer of 'size' bytes is allocated.
 * if buf is NULL and size is 0, a buffer of NOVA_WBUF_SIZE bytes is allocated.
 *
 * @param buf Pointer to the buffer or NULL.
 * @param size Size of the buffer.
 */
extern void nv_setwbuf(char* buf, size_t size);

/**
 * @brief Sets the standard output stream.
 *
 * Calls to nv_printf are routed to this stream.
 * No validity checks are performed.
 *
 * @param stream A valid FILE pointer.
 */
extern void nv_setstdout(FILE* stream);

/**
 * @brief Prints formatted output to the g_stdstream
 * @return The number of characters written.
 */
extern size_t nv_printf(const char* fmt, ...);

/**
 * @brief Prints formatted output to file.
 */
extern size_t nv_fprintf(FILE* f, const char* fmt, ...);

/**
 * @brief Prints no more than max_chars characters to the g_stdstream.
 */
extern size_t nv_nprintf(size_t max_chars, const char* fmt, ...);

/**
 * @brief Prints formatted output to a string.
 *
 * @note Usage of this function is not recommended; use nv_snprintf() instead.
 */
extern size_t nv_sprintf(char* dest, const char* fmt, ...);

/**
 * @brief Prints formatted output using a va_list.
 */
extern size_t nv_vprintf(const char* fmt, va_list args);

/**
 * @brief Prints formatted output using a va_list to a file.
 */
extern size_t nv_vfprintf(FILE* f, const char* fmt, va_list args);

/**
 * @brief Prints formatted output to a string writing no more than max_chars
 */
extern size_t nv_snprintf(char* dest, size_t max_chars, const char* fmt, ...);

/**
 * @brief Prints no more than max_chars characters to g_stdstream, using a va_list.
 */
extern size_t nv_vnprintf(size_t max_chars, va_list args, const char* fmt);

/**
 * @brief Prints no more than max_chars characters to a string using a va_list.
 */
extern size_t nv_vsnprintf(char* dest, size_t max_chars, const char* fmt, va_list args);

/**
 * @brief The core print function.
 *
 * All nv_printf* functions eventually call this function.
 * It stops formatting when max_chars is reached.
 *
 * @param dest Destination buffer or file pointer.
 * @param is_file Set to true if dest is a FILE.
 * @return The number of characters written.
 */
extern size_t _nv_vsfnprintf(void* dest, bool is_file, size_t max_chars, const char* fmt, va_list args);

NOVA_HEADER_END;

#endif // __NOVA_PRINT_H__