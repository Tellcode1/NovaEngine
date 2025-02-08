#ifndef __NOVA_IO_H__
#define __NOVA_IO_H__

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
 * @brief Converts an integer to ASCII (Alpha).
 *
 * @param x The integer to convert.
 * @param out Output buffer.
 * @param base Conversion base.
 * @param max Maximum number of characters to write.
 * @return The number of characters written (excluding the null terminator).
 */
extern size_t nv_itoa2(intmax_t x, char out[], int base, size_t max);

/**
 * @brief Converts an unsigned integer to ASCII.
 */
extern size_t nv_itoa_u2(uintmax_t x, char out[], int base, size_t max);

/**
 * @brief Converts a double to ASCII.
 *
 * @param x The double value.
 * @param out Output buffer.
 * @param precision Number of digits after the decimal point.
 * @param max Maximum number of characters.
 * @param remove_zeroes If true, trailing zeroes are removed.
 * @return The number of characters written.
 */
extern size_t nv_ftoa2(double x, char out[], int precision, size_t max, bool remove_zeroes);

/**
 * @brief Converts a pointer to ASCII.
 */
extern size_t nv_ptoa2(void* p, char* buf, size_t max);

/**
 * @brief Converts a byte count to ASCII.
 *
 * Supports upgrade modes (e.g. converting 1000 bytes to "1KB", etc.).
 * Up to 1 petabyte is supported. It can go farther but it is undefined behaviour
 * Writes the bytes (using itoa_u) and writes the suffix ( B/KB/MB/GB/PB )
 * @return The number of characters written.
 */
extern size_t nv_btoa2(size_t x, bool upgrade, char* buf, size_t max);

/**
 * @brief Converts a string to an integer.
 */
extern intmax_t nv_atoi(const char s[]);

/**
 * @brief Converts a string to a double.
 */
extern double nv_atof(const char s[]);

/**
 * @brief Converts a string to a boolean.
 */
extern bool nv_atobool(const char s[]);

static inline char*
nv_itoa(intmax_t x, char out[], int base, size_t max)
{
  nv_itoa2(x, out, base, max);
  return out;
}

static inline char*
nv_itoa_u(uintmax_t x, char out[], int base, size_t max)
{
  nv_itoa_u2(x, out, base, max);
  return out;
}

static inline char*
nv_ftoa(double x, char out[], int precision, size_t max, bool remove_zeroes)
{
  nv_ftoa2(x, out, precision, max, remove_zeroes);
  return out;
}

static inline char*
nv_ptoa(void* p, char* buf, size_t max)
{
  nv_ptoa2(p, buf, max);
  return buf;
}

static inline char*
nv_btoa(size_t x, bool upgrade, char* buf, size_t max)
{
  nv_btoa2(x, upgrade, buf, max);
  return buf;
}

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

#endif // __NOVA_IO_H__