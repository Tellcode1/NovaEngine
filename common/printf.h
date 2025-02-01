#ifndef __NOVA_PRINTF_H__
#define __NOVA_PRINTF_H__

#include "stdafx.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

NOVA_HEADER_START;

// The DEFAULT size of each buffer that nv_printf may allocate
// nv_printf may use at most 2 buffers.
// one is used for fast writing access and one is used when writing to a stream
// as an intermediary

// we could copy nv_vsnprintf into a vnfprintf that would stream the
// write_buffer to the stream we'll see

#ifndef NOVA_PRINTF_BUFSIZ
#define NOVA_PRINTF_BUFSIZ 1024
#endif

// @brief Set the write buffer for the print functions
extern void nv_setprintbuf(char *buf, size_t size);

// Set the standard output stream
// Calls to nv_printf are routed to this stream.
// No checks are made on stream, ensure that it is always valid!
extern void nv_setstdout(FILE *stream);

extern bool nv_is_format_specifier(char c);

// @return The number of characters written excluding the NULL terminator
extern size_t nv_itoa2(intmax_t x, char out[], int base, size_t max);

// ITOA but only for unsigned types
extern size_t nv_itoa_u2(uintmax_t x, char out[], int base, size_t max);

// @return The number of characters written excluding the NULL terminator
extern size_t nv_ftoa2(double x, char out[], int precision, size_t max, bool remove_zeroes);

// Pointer to string.
// @return The number of characters written excluding the NULL terminator
extern size_t nv_ptoa2(void *p, char *buf, size_t max);

// Bytes to ASCII
// if upgrade is enabled, 1000 bytes will be converted to 1 MB or 1000MB will be converted to 1GB
// Supports up to 1 Petabyte, you can EASILY add more levels by just adding them to the stages array
// @return The number of characters written excluding the NULL terminator
extern size_t nv_btoa2(size_t x, bool upgrade, char *buf, size_t max);

extern intmax_t nv_atoi(const char s[]);

extern double nv_atof(const char s[]);

static inline char *nv_itoa(intmax_t x, char out[], int base, size_t max) {
  nv_itoa2(x, out, base, max);
  return out;
}

static inline char *nv_itoa_u(uintmax_t x, char out[], int base, size_t max) {
  nv_itoa_u2(x, out, base, max);
  return out;
}

static inline char *nv_ftoa(double x, char out[], int precision, size_t max, bool remove_zeroes) {
  nv_ftoa2(x, out, precision, max, remove_zeroes);
  return out;
}

static inline char *nv_ptoa(void *p, char *buf, size_t max) {
  nv_ptoa2(p, buf, max);
  return buf;
}

static inline char *nv_btoa(size_t x, bool upgrade, char *buf, size_t max) {
  nv_btoa2(x, upgrade, buf, max);
  return buf;
}

// @return the numbers of characters written
extern size_t nv_printf(const char *fmt, ...);
extern size_t nv_fprintf(FILE *f, const char *fmt, ...);
extern size_t nv_nprintf(size_t max_chars, const char *fmt, ...);
extern size_t nv_sprintf(char *dest, const char *fmt, ...);
extern size_t nv_vprintf(const char *fmt, va_list args);
extern size_t nv_vfprintf(FILE *f, const char *fmt, va_list args);
extern size_t nv_snprintf(char *dest, size_t max_chars, const char *fmt, ...);
extern size_t nv_vnprintf(size_t max_chars, va_list args, const char *fmt);

// The core print function. All nv_printf* functions eventually end up calling
// this function. It is guranteed that dest will not be written to if it is NULL
// Stops formatting when it reaches max_chars
extern size_t nv_vsnprintf(char *dest, size_t max_chars, const char *fmt, va_list args);

NOVA_HEADER_END;

#endif //__NOVA_PRINTF_H__
