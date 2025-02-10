#ifndef __NOVA_STRING_CONV_H__
#define __NOVA_STRING_CONV_H__

#include "stdafx.h"

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

#endif //__NOVA_STRING_CONV_H__