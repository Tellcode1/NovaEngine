#ifndef __NOVA_STDAFX_H__
#define __NOVA_STDAFX_H__

// implementation: core.c

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
#  define NOVA_HEADER_START                                                                                                                                                   \
    extern "C"                                                                                                                                                                \
    {
#  define NOVA_HEADER_END }
/*
  extern "C" {
  }
*/
#else
#  define NOVA_HEADER_START
#  define NOVA_HEADER_END
#endif

NOVA_HEADER_START

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

#ifndef NV_ALIGN_TO
#  if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L // C11+
#    define NV_ALIGN_TO(N) _Alignas(N)
#  elif defined(__GNUC__) || defined(__clang__)
#    define NV_ALIGN_TO(N) __attribute__((aligned(N)))
#  elif defined(_MSC_VER) // MSVC
#    define NV_ALIGN_TO(N) __declspec(align(N))
#  elif defined(__INTEL_COMPILER)
#    define NV_ALIGN_TO(N) __declspec(align(N))
#  else
#    error "no align"
#  endif
#endif

#ifndef NV_USED
#  if defined(__GNUC__) || defined(__clang__)
#    define NV_USED __attribute__((__used__))
#  else
#    define NV_USED
#  endif
#endif

/* [fallthrough] is a C23 extension. I get it now. Shut up please. */
#if defined(__GNUC__) && __GNUC__ >= 7 || defined(__clang__) && __clang_major__ >= 12
#  define NV_FALLTHROUGH __attribute__((fallthrough))
#else
#  define NV_FALLTHROUGH /* fallthrough */
#endif

#ifndef real_t
#  define real_t double
#endif

#ifndef flt_t
#  define flt_t float
#endif

#define DEBUG

#define NV_MAX(a, b) ((a) > (NV_TYPEOF(a))(b) ? (a) : (NV_TYPEOF(a))(b))
#define NV_MIN(a, b) ((a) < (NV_TYPEOF(a))(b) ? (a) : (NV_TYPEOF(a))(b))
#define NV_CONCAT(x, y) x##y

/*
 * GNUC and builtin have protection from accidentally passing in pointers instead of stack arrays
 */
#if defined(__GNUC__) && (__STDC_VERSION__ >= 201112L)
#  define nv_arrlen(arr) _Generic(&(arr), NV_TYPEOF(*(arr))(*): 0, default: (sizeof(arr) / sizeof((arr)[0])))
#elif defined(__has_builtin) && __has_builtin(__builtin_choose_expr) && __has_builtin(__builtin_types_compatible_p)
#  define nv_arrlen(arr) __builtin_choose_expr(__builtin_types_compatible_p(NV_TYPEOF(arr), NV_TYPEOF(&(arr)[0])), 0, (sizeof(arr) / sizeof((arr)[0])))
#else
#  define nv_arrlen(arr) ((size_t)(sizeof(arr) / sizeof((arr)[0])))
#endif

#ifndef nv_zero_init
#  define nv_zero_init(TYPE) (TYPE){ 0 }
#endif

/* https://stackoverflow.com/a/11172679 */
/* Stupid fix to , ##__VA_ARGS__ being a GNU extension */
/* Only supports up to 10 arguments! However, increasing the limit is easy */
/* Go to _GNUC_HELP_ME_PLEASE_SELECT_10TH and just add more variables and set the define to the last one */

#define _GNUC_HELP_ME_PLEASE_FIRST(...) _GNUC_HELP_ME_PLEASE_FIRST_HELPER(__VA_ARGS__, throwaway)
#define _GNUC_HELP_ME_PLEASE_FIRST_HELPER(first, ...) first

/*
 * if there's only one argument, expands to nothing.  if there is more
 * than one argument, expands to a comma followed by everything but
 * the first argument.  only supports up to 9 arguments but can be
 * trivially expanded.
 */
#define _GNUC_HELP_ME_PLEASE_REST(...) _GNUC_HELP_ME_PLEASE_REST_HELPER(_GNUC_HELP_ME_PLEASE_NUM(__VA_ARGS__), __VA_ARGS__)
#define _GNUC_HELP_ME_PLEASE_REST_HELPER(qty, ...) _GNUC_HELP_ME_PLEASE_REST_HELPER2(qty, __VA_ARGS__)
#define _GNUC_HELP_ME_PLEASE_REST_HELPER2(qty, ...) _GNUC_HELP_ME_PLEASE_REST_HELPER_##qty(__VA_ARGS__)
#define _GNUC_HELP_ME_PLEASE_REST_HELPER_ONE(first)
#define _GNUC_HELP_ME_PLEASE_REST_HELPER_TWOORMORE(first, ...) , __VA_ARGS__
#define _GNUC_HELP_ME_PLEASE_NUM(...)                                                                                                                                         \
  _GNUC_HELP_ME_PLEASE_SELECT_10TH(__VA_ARGS__, TWOORMORE, TWOORMORE, TWOORMORE, TWOORMORE, TWOORMORE, TWOORMORE, TWOORMORE, TWOORMORE, ONE, throwaway)
#define _GNUC_HELP_ME_PLEASE_SELECT_10TH(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, ...) a10

#define nv_push_error(...) _nv_push_error(__func__, (_nv_get_time()), _GNUC_HELP_ME_PLEASE_FIRST(__VA_ARGS__) _GNUC_HELP_ME_PLEASE_REST(__VA_ARGS__))

extern const char* nv_pop_error(void);

/* Print all error messages in the queue and clean it */
extern void nv_flush_errors(void);

#ifndef NDEBUG
#  define nv_assert_and_ret(expr, retval)                                                                                                                                     \
    if (!((bool)(expr)))                                                                                                                                                      \
    {                                                                                                                                                                         \
      nv_push_error("Assertion failed -> %s", #expr);                                                                                                                         \
      return retval;                                                                                                                                                          \
    }
#  define nv_assert(expr)                                                                                                                                                     \
    if (!((bool)(expr)))                                                                                                                                                      \
    {                                                                                                                                                                         \
      nv_log_and_abort("Assertion failed -> %s", #expr);                                                                                                                      \
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
#define nv_log_error(...) _nv_log_error(__func__, _GNUC_HELP_ME_PLEASE_FIRST(__VA_ARGS__) _GNUC_HELP_ME_PLEASE_REST(__VA_ARGS__))

// formats the string, puts() it with the preceder "fatal error" and then aborts
// the program
#define nv_log_and_abort(...) _nv_log_and_abort(__func__, _GNUC_HELP_ME_PLEASE_FIRST(__VA_ARGS__) _GNUC_HELP_ME_PLEASE_REST(__VA_ARGS__))

// puts but with formatting and with the preceder "warning"
#define nv_log_warning(...) _nv_log_warning(__func__, _GNUC_HELP_ME_PLEASE_FIRST(__VA_ARGS__) _GNUC_HELP_ME_PLEASE_REST(__VA_ARGS__))

// puts but with formatting and with the preceder "info"
#define nv_log_info(...) _nv_log_info(__func__, _GNUC_HELP_ME_PLEASE_FIRST(__VA_ARGS__) _GNUC_HELP_ME_PLEASE_REST(__VA_ARGS__))

// puts but with formatting and with the preceder "debug"
#define nv_log_debug(...) _nv_log_debug(__func__, _GNUC_HELP_ME_PLEASE_FIRST(__VA_ARGS__) _GNUC_HELP_ME_PLEASE_REST(__VA_ARGS__))

// puts but with formatting and with a custom preceder
#define nv_log_custom(preceder, ...) _nv_log_custom(__func__, preceder, _GNUC_HELP_ME_PLEASE_FIRST(__VA_ARGS__) _GNUC_HELP_ME_PLEASE_REST(__VA_ARGS__))

extern void _nv_log_error(const char* func, const char* fmt, ...);
extern void _nv_log_and_abort(const char* func, const char* fmt, ...);
extern void _nv_log_warning(const char* func, const char* fmt, ...);
extern void _nv_log_info(const char* func, const char* fmt, ...);
extern void _nv_log_debug(const char* func, const char* fmt, ...);
extern void _nv_log_custom(const char* func, const char* preceder, const char* fmt, ...);

extern void _nv_log(va_list args, const char* fn, const char* succeeder, const char* preceder, const char* str, unsigned char err);

extern void _nv_push_error(const char* func, struct tm* time, const char* fmt, ...);

#define _nv_time_wrapper1(x, y) NV_CONCAT(x, y)

// May god never have a look at this define. I will not be spared.
/* And I thought this was bad. How innocent I was. */
#define _nv_time_function(func, LINE)                                                                                                                                         \
  const size_t _nv_time_wrapper1(__COUNTER_BEGIN__, __LINE__) = SDL_GetTicks64();                                                                                             \
  func; /* Call the function*/                                                                                                                                                \
  nv_log_debug("[Line %d] Function %s took %ldms", LINE, #func, SDL_GetTicks64() - _nv_time_wrapper1(__COUNTER_BEGIN__, __LINE__), LINE);
#define nv_time_function(func) _nv_time_function(func, __LINE__)

static inline struct tm*
_nv_get_time(void)
{
  time_t     now;
  struct tm* tm;

  now = time(0);
  if ((tm = localtime(&now)) == NULL)
  {
    nv_push_error("Error extracting time stuff");
    return NULL;
  }

  return tm;
}

#define nv_safecall_c_fn(fn)                                                                                                                                                  \
  if ((fn) != 0)                                                                                                                                                              \
  {                                                                                                                                                                           \
    nv_push_error("%s() => %i", #fn, errno);                                                                                                                                  \
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
typedef real_t f64;

NOVA_HEADER_END

#endif
