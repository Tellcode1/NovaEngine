#ifndef __NOVA_STDAFX_H__
#define __NOVA_STDAFX_H__

// implementation: core.c

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef __cplusplus
#  define NOVA_HEADER_START extern "C" {
#  define NOVA_HEADER_END }
#else
#  define NOVA_HEADER_START
#  define NOVA_HEADER_END
#endif

NOVA_HEADER_START;

#if !defined(NV_RESTRICT)
#  if defined(_MSC_VER)
#    define NV_RESTRICT __restrict
#  elif defined(__GNUC__) || defined(__clang__)
#    define NV_RESTRICT __restrict__
#  else
#    define NV_RESTRICT
#  endif
#endif

#ifndef NV_TYPEOF
#  if defined(__GNUC__) || defined(__clang__)
#    define NV_TYPEOF(x) __typeof__(x)
#  elif defined(_MSC_VER)
#    define NV_TYPEOF(x) decltype(x)
#  else
#    error "no typeof"
#  endif
#endif

#ifndef real_t
#  define real_t double
#endif

#ifndef flt_t
#  define flt_t float
#endif

#define DEBUG

#define NV_MAX(a, b) ((a) > (b) ? (a) : (b))
#define NV_MIN(a, b) ((a) < (b) ? (a) : (b))
#define NV_CONCAT(x, y) x##y

#if defined(__GNUC__)
#  define nv_arrlen(arr) _Generic(&(arr), typeof (*(arr))(*): 0, default: (sizeof(arr) / sizeof((arr)[0])))
#elif defined(__has_builtin) && __has_builtin(__builtin_choose_expr) && __has_builtin(__builtin_types_compatible_p)
#  define nv_arrlen(arr) __builtin_choose_expr(__builtin_types_compatible_p(typeof(arr), typeof(&(arr)[0])), 0, (sizeof(arr) / sizeof((arr)[0])))
#else
#  define nv_arrlen(arr) ((size_t)(sizeof(arr) / sizeof(arr[0])))
#endif

#ifndef NDEBUG
#  define nv_assert_and_ret(expr, retval)                                                                                                                                     \
    if (!((bool)(expr)))                                                                                                                                                      \
      {                                                                                                                                                                       \
        nv_log_and_abort("Assertion failed -> %s", #expr);                                                                                                                    \
        return retval;                                                                                                                                                        \
    }
#  define nv_assert(expr)                                                                                                                                                     \
    if (!((bool)(expr)))                                                                                                                                                      \
      {                                                                                                                                                                       \
        nv_log_and_abort("Assertion failed -> %s", #expr);                                                                                                                    \
    }
#else
// These are typecasted to void because they give warnings because result (its
// like expr != NULL) is not used
#  define nv_assert_and_ret(expr, retval) (void)(expr)
#  define nv_assert(expr) (void)(expr)
#  pragma message "Assertions disabled"
#endif

// puts but with formatting and with the preceder "error". does not stop
// execution of program if you want that, use nv_log_and_abort instead.
#define nv_log_error(err, ...) _nv_log_error(__PRETTY_FUNCTION__, err, ##__VA_ARGS__)

// formats the string, puts() it with the preceder "fatal error" and then aborts
// the program
#define nv_log_and_abort(err, ...) _nv_log_and_abort(__PRETTY_FUNCTION__, err, ##__VA_ARGS__)

// puts but with formatting and with the preceder "warning"
#define nv_log_warning(err, ...) _nv_log_warning(__PRETTY_FUNCTION__, err, ##__VA_ARGS__)

// puts but with formatting and with the preceder "info"
#define nv_log_info(err, ...) _nv_log_info(__PRETTY_FUNCTION__, err, ##__VA_ARGS__)

// puts but with formatting and with the preceder "debug"
#define nv_log_debug(err, ...) _nv_log_debug(__PRETTY_FUNCTION__, err, ##__VA_ARGS__)

// puts but with formatting and with a custom preceder
#define nv_log_custom(preceder, err, ...) _nv_log_custom(__PRETTY_FUNCTION__, preceder, err, ##__VA_ARGS__)

extern void _nv_log_error(const char* func, const char* fmt, ...);
extern void _nv_log_and_abort(const char* func, const char* fmt, ...);
extern void _nv_log_warning(const char* func, const char* fmt, ...);
extern void _nv_log_info(const char* func, const char* fmt, ...);
extern void _nv_log_debug(const char* func, const char* fmt, ...);
extern void _nv_log_custom(const char* func, const char* preceder, const char* fmt, ...);

extern void _nv_log(va_list args, const char* fn, const char* succeeder, const char* preceder, const char* str, unsigned char err);

#define _nv_time_wrapper1(x, y) NV_CONCAT(x, y)

// May god never have a look at this define. I will not be spared.
#define _nv_TIME_FUNCTION(func, LINE)                                                                                                                                         \
  const size_t _nv_time_wrapper1(__COUNTER_BEGIN__, __LINE__) = SDL_GetTicks64();                                                                                             \
  func; /* Call the function*/                                                                                                                                                \
  nv_log_debug("[Line %d] Function %s took %ldms", LINE, #func, SDL_GetTicks64() - _nv_time_wrapper1(__COUNTER_BEGIN__, __LINE__), LINE);
#define nv_time_function(func) _nv_time_function(func, __LINE__)

static inline struct tm*
_nv_get_time()
{
  time_t     now;
  struct tm* tm;

  now = time(0);
  if ((tm = localtime(&now)) == NULL)
    {
      nv_log_error("Error extracting time stuff");
      return NULL;
  }

  return tm;
}

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;
typedef uint8_t  uchar;
typedef int8_t   sbyte;
typedef uint8_t  ubyte;

typedef int64_t i64;
typedef int32_t i32;
typedef int16_t i16;
typedef int8_t  i8;

// They ARE 32 and 64 bits by IEEE-754 but aren't set by the standard
// But there is a 99.9% chance that they will be
typedef float  f32;
typedef double f64;

NOVA_HEADER_END;

#endif
