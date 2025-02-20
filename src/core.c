#include <SDL2/SDL.h>
#include <bits/pthreadtypes.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "../common/containers/atlas.h"
#include "../common/containers/bitset.h"
#include "../common/containers/dynarray.h"
#include "../common/containers/freelist.h"
#include "../common/containers/hashmap.h"
#include "../common/containers/string.h"
#include "../common/image.h"
#include "../common/mem.h"
#include "../std/async.h"
#include "../std/math/math.h"
#include "../std/print.h"
#include "../std/props.h"
#include "../std/stdafx.h"
#include "../std/strconv.h"
#include "../std/string.h"

#define ALIGN_UP(ptr, alignment) (void*)(((uintptr_t)(ptr) + (alignment - 1)) & ~(alignment - 1))
#define ALIGN_UP_SIZE(size, align) (((size) + (align) - 1) & ~((align) - 1))

static FILE*  g_stdstream   = NULL;
static char*  g_writebuf    = NULL;
static size_t g_writebufsiz = NOVA_WBUF_SIZE;

#if !defined(NVSM) && !defined(FONTC)

void
_nv_log(va_list args, const char* fn, const char* succeeder, const char* preceder, const char* s, unsigned char err)
{
  FILE* out = (err) ? stderr : stdout;

  struct tm* time = _nv_get_time();

  nv_fprintf(out, "[%d:%d:%d]%s%s(): ", time->tm_hour % 12, time->tm_min, time->tm_sec, preceder, fn);

  nv_vfprintf(out, s, args);
  nv_fprintf(out, "%s", succeeder);
}

// printf

void
nv_setwbuf(char* buf, size_t size)
{
  nv_assert(size > 0);
  if (g_writebuf) nv_free(g_writebuf);

  g_writebufsiz = size == 0 ? NOVA_WBUF_SIZE : size;
  g_writebuf    = buf ? buf : nv_malloc(size);
}

void
nv_setstdout(FILE* stream)
{
  g_stdstream = stream;
}

size_t
nv_itoa2(intmax_t x, char out[], int base, size_t max)
{
  nv_assert(base >= 2 && base <= 36);
  nv_assert(out != NULL);

  if (max == 0)
  {
    return 0; // this shouldn't be an error
  }
  else if (max == 1)
  {
    out[0] = 0;
    return 0;
  }

  // now, max should atleast be 1

  if (x == 0)
  {
    *out       = '0';
    *(out + 1) = 0;
    return 1;
  }

  size_t    i   = 0;
  uintmax_t pwr = 1;

  while (x / base >= (intmax_t)pwr)
    pwr *= base;

  if (x < 0 && base == 10)
  {
    out[i++] = '-';
    x        = -x;
  }

  // we need 1 space for NULL terminator!!
  // max will be greater or equal to 1 due to past checks
  max--;
  do
  {
    if (i >= max)
    {
      out[i] = 0;
      return i;
    }

    int dig  = x / pwr;
    out[i++] = (dig < 10) ? '0' + dig : 'A' + (dig - 10);

    x %= pwr;
    pwr /= base;
  } while (pwr > 0);

  out[i] = 0;
  return i;
}

size_t
nv_itoa_u2(uintmax_t x, char out[], int base, size_t max)
{
  nv_assert(base >= 2 && base <= 36);
  nv_assert(out != NULL);

  if (max == 0) return 0;
  if (max == 1)
  {
    out[0] = 0;
    return 0;
  }

  size_t    i   = 0;
  uintmax_t pwr = 1;

  while (x / base >= pwr)
    pwr *= base;

  max--;

  do
  {
    if (i >= max)
    {
      out[i] = 0;
      return i;
    }

    int dig  = x / pwr;
    out[i++] = (dig < 10) ? '0' + dig : 'A' + (dig - 10);

    x %= pwr;
    pwr /= base;
  } while (pwr > 0);

  out[i] = 0;
  return i;
}

#  define NOVA_FTOA_HANDLE_CASE(fn, n, str)                                                                                                                                   \
    if (fn(n))                                                                                                                                                                \
    {                                                                                                                                                                         \
      if (signbit(n) == 0)                                                                                                                                                    \
        return nv_strncpy2(s, str, max);                                                                                                                                      \
      else                                                                                                                                                                    \
        return nv_strncpy2(s, "-" str, max);                                                                                                                                  \
    }

// WARNING::: I didn't write most of this, stole it from stack overflow.
// if it explodes your computer its your fault!!!
size_t
nv_ftoa2(real_t n, char s[], int precision, size_t max, bool remove_zeros)
{
  if (max == 0) return 0;
  if (max == 1)
  {
    s[0] = 0;
    return 0;
  }

  NOVA_FTOA_HANDLE_CASE(isnan, n, "nan");
  NOVA_FTOA_HANDLE_CASE(isinf, n, "inf");
  NOVA_FTOA_HANDLE_CASE(0.0 ==, n, "0.0");

  char* c   = s;
  int   neg = (n < 0);
  if (neg)
  {
    n      = -n;
    *(c++) = '-';
  }

  int exp    = (n == 0.0) ? 0 : (int)log10(n);
  int useExp = (exp >= 14 || (neg && exp >= 9) || exp <= -9);
  if (useExp) { n /= pow(10.0, exp); }

  real_t rounding = pow(10.0, -precision) * 0.5;
  n += rounding;

  uint64_t int_part  = (uint64_t)n;
  real_t   frac_part = n - int_part;

  char* start = c;
  do
  {
    *(c++) = '0' + (int_part % 10);
    int_part /= 10;
  } while (int_part && (size_t)(c - s) < max - 1);

  char* end = c - 1;
  while (start < end)
  {
    char tmp = *start;
    *start++ = *end;
    *end--   = tmp;
  }

  if (precision > 0 && (size_t)(c - s) < max - 2)
  {
    *(c++) = '.';
    for (int i = 0; i < precision && (size_t)(c - s) < max - 1; i++)
    {
      frac_part *= 10;
      int digit = (int)frac_part;
      *(c++)    = '0' + digit;
      frac_part -= digit;
    }
  }

  if (remove_zeros && precision > 0)
  {
    while (*(c - 1) == '0')
      c--;
    if (*(c - 1) == '.') c--;
  }

  if (useExp && (size_t)(c - s) < max - 4)
  {
    *(c++) = 'e';
    *(c++) = (exp >= 0) ? '+' : '-';
    exp    = (exp >= 0) ? exp : -exp;

    if (exp >= 100) *(c++) = '0' + (exp / 100);
    if (exp >= 10) *(c++) = '0' + ((exp / 10) % 10);
    *(c++) = '0' + (exp % 10);
  }

  *c = 0;
  return c - s;
}

#  define NV_SKIP_WHITSPACE(s)                                                                                                                                                \
    while (*(s) && isspace(*(s)))                                                                                                                                             \
    (s)++

intmax_t
nv_atoi(const char s[])
{
  if (!s) { return __INTMAX_MAX__; }

  const char* i   = s;
  intmax_t    ret = 0;

  NV_SKIP_WHITSPACE(i);

  bool neg = 0;
  if (*i == '-')
  {
    neg = 1;
    i++;
  }
  else if (*i == '+') { i++; }

  while (*i)
  {
    if (!isdigit(*i)) { break; }

    int digit = *i - '0';
    ret       = ret * 10 + digit;

    i++;
  }

  if (neg) { ret *= -1; }

  return ret;
}

real_t
nv_atof(const char s[])
{
  if (!s) { return INFINITY; }

  real_t      result = 0.0, fraction = 0.0;
  int         divisor = 1;
  bool        neg     = 0;
  const char* i       = s;

  NV_SKIP_WHITSPACE(i);

  if (*i == '-')
  {
    neg = 1;
    i++;
  }
  else if (*i == '+') { i++; }

  while (isdigit(*i))
  {
    result = result * 10 + (*i - '0');
    i++;
  }

  if (*i == '.')
  {
    i++;
    while (isdigit(*i))
    {
      fraction = fraction * 10 + (*i - '0');
      divisor *= 10;
      i++;
    }
    result += fraction / divisor;
  }

  if (*s == 'e' || *i == 'E')
  {
    i++;
    int exp_sign = 1;
    int exponent = 0;

    if (*i == '-')
    {
      exp_sign = -1;
      i++;
    }
    else if (*s == '+') { i++; }

    while (isdigit(*i))
    {
      exponent = exponent * 10 + (*i - '0');
      i++;
    }

    result = ldexp(result, exp_sign * exponent);
  }

  if (neg) { result *= -1.0; }

  return result;
}

bool
nv_atobool(const char s[])
{
  NV_SKIP_WHITSPACE(s);
  if (nv_strcasecmp(s, "false") == 0 || nv_strcmp(s, "0") == 0) { return false; }
  return true;
}

size_t
nv_ptoa2(void* p, char* buf, size_t max)
{
  if (p == NULL) { return nv_strncpy2(buf, "NULL", max); }

  unsigned long addr   = (unsigned long)p;
  const char    digs[] = "0123456789abcdef";

  size_t w = 0;

  w += nv_strncpy2(buf, "0x", max);

  // stolen from stack overflow
  for (int i = (sizeof(addr) * 2) - 1; i >= 0 && w < max - 1; i--)
  {
    int dig = (addr >> (i * 4)) & 0xF;
    buf[w]  = digs[dig];
    w++;
  }
  buf[w] = 0;
  return w;
};

size_t
nv_btoa2(size_t x, bool upgrade, char* buf, size_t max)
{
  size_t written = 0;
  if (upgrade)
  {
    const char* stages[] = { " B", " KB", " MB", " GB", " TB", " PB", " Comically large number of bytes" };
    real_t      b        = (real_t)x;
    u32         stagei   = 0;

    const size_t num_stages = nv_arrlen(stages) - 1;
    while (b >= 1000.0 && stagei < num_stages)
    {
      stagei++;
      b /= 1000.0;
    }

    written = nv_ftoa2(b, buf, 3, max, 1);
    nv_strcat_max(buf, stages[stagei], max);
    written += nv_strlen(stages[stagei]);
    written = NV_MIN(written, max);
  }
  else { written = nv_itoa2(x, buf, 10, max); }
  return written;
}

// Moral of the story? FU@# SIZE_MAX
// I spent an HOUR trying to figure out what's going wrong
// and I didn't even bat an eye towards it

size_t
nv_printf(const char* fmt, ...)
{
  va_list args;
  va_start(args, fmt);

  size_t chars_written = nv_vnprintf(g_writebufsiz, args, fmt);

  va_end(args);

  return chars_written;
}

size_t
nv_fprintf(FILE* f, const char* fmt, ...)
{
  va_list args;
  va_start(args, fmt);

  size_t chars_written = _nv_vsfnprintf(f, 1, SIZE_MAX, fmt, args);

  va_end(args);

  return chars_written;
}

size_t
nv_sprintf(char* dest, const char* fmt, ...)
{
  va_list args;
  va_start(args, fmt);

  size_t chars_written = _nv_vsfnprintf(dest, 0, SIZE_MAX, fmt, args);

  va_end(args);

  return chars_written;
}

size_t
nv_vprintf(const char* fmt, va_list args)
{
  return _nv_vsfnprintf(g_stdstream, 1, SIZE_MAX, fmt, args);
}

size_t
nv_vfprintf(FILE* f, const char* fmt, va_list args)
{
  return _nv_vsfnprintf(f, 1, SIZE_MAX, fmt, args);
}

size_t
nv_snprintf(char* dest, size_t max_chars, const char* fmt, ...)
{
  va_list args;
  va_start(args, fmt);

  size_t chars_written = nv_vsnprintf(dest, max_chars, fmt, args);

  va_end(args);

  return chars_written;
}

size_t
nv_nprintf(size_t max_chars, const char* fmt, ...)
{
  va_list args;
  va_start(args, fmt);

  size_t chars_written = _nv_vsfnprintf(g_stdstream, 1, max_chars, fmt, args);

  va_end(args);

  return chars_written;
}

size_t
nv_vnprintf(size_t max_chars, va_list args, const char* fmt)
{
  return _nv_vsfnprintf(g_stdstream, 1, max_chars, fmt, args);
}

void
_nv_free_write_buffer()
{
  if (g_writebuf) { free(g_writebuf); }
}

size_t
nv_vsnprintf(char* dest, size_t max_chars, const char* fmt, va_list src)
{
  return _nv_vsfnprintf(dest, 0, max_chars, fmt, src);
}

static inline void
_nv_printf_write(void* _write, bool file, size_t* chars_written, size_t max_chars, const char* write_buffer, size_t written)
{
  if (*chars_written >= max_chars) return;

  size_t remaining = max_chars - *chars_written;
  size_t to_write  = (written > remaining) ? remaining : written;
  *chars_written += to_write;
  if (!write_buffer) return;

  if (file)
  {
    FILE* f = (FILE*)_write;
    fwrite(write_buffer, 1, to_write, f);
  }
  else
  {
    char** write = (char**)_write;
    if (*write && to_write > 0)
    {
      nv_memcpy(*write, write_buffer, to_write);
      (*write) += to_write;
    }
  }
}

size_t
_nv_vsfnprintf(void* vdest, bool file, size_t max_chars, const char* fmt, va_list src)
{
  if (!g_writebuf)
  {
    g_writebuf = nv_malloc(g_writebufsiz);
    nv_assert(g_writebuf != NULL);
    atexit(_nv_free_write_buffer);
  }
  if (!g_stdstream) { g_stdstream = stdout; }
  if (file && !vdest) { vdest = g_stdstream; }

  nv_assert(fmt != NULL);

  void*  _writeptr     = NULL;
  size_t chars_written = 0;
  size_t written       = 0;
  int    padding       = 0;
  int    padding_w     = 0;
  int    precision     = 6;

  char* writep = (char*)vdest;
  if (file)
    _writeptr = (FILE*)vdest;
  else
    _writeptr = &writep;

  const char* const fmt_end = fmt + nv_strlen(fmt); // point to NULL terminator of fmt

  NV_ALIGN_TO(64) char pad_buf[64];

  const char* s = NULL;

  const char* iter = fmt;
  va_list     args;
  va_copy(args, src);

  for (; *iter && chars_written < max_chars; iter++)
  {
    if (*iter == '%')
    {
      iter++;

      bool wbuffer_used = true;
      bool pad_zero     = false;
      bool left_align   = false;
      padding_w         = 0;
      precision         = 6;

      if (*iter == '-')
      {
        left_align = true;
        iter++;
      }
      if (*iter == '0')
      {
        pad_zero = true;
        iter++;
      }

      if (*iter == '*')
      {
        padding_w = va_arg(args, int);
        if (padding_w < 0)
        { // negative width means left align
          left_align = true;
          padding_w  = -padding_w;
        }
        iter++;
      }
      else
      {
        while (isdigit((unsigned char)*iter))
        {
          padding_w = padding_w * 10 + (*iter - '0');
          iter++;
        }
      }

      if (*iter == '.')
      {
        iter++;
        if (*iter == '*')
        {
          precision = va_arg(args, int);
          if (precision < 0) precision = 6;
          iter++;
        }
        else
        {
          precision = 0;
          while (isdigit((unsigned char)*iter))
          {
            precision = precision * 10 + (*iter - '0');
            iter++;
          }
        }
      }

      switch (*iter)
      {
        case 'F':
        case 'f': written = nv_ftoa2(va_arg(args, real_t), g_writebuf, precision, max_chars - chars_written, 0); break;
        case 'l':
          if ((iter + 1) < fmt_end) { iter++; }

          if (*iter == 'd' || *iter == 'i') { written = nv_itoa2(va_arg(args, long int), g_writebuf, 10, max_chars - chars_written); }
          else if (*iter == 'u') { written = nv_itoa_u2(va_arg(args, long unsigned), g_writebuf, 10, max_chars - chars_written); }
          else if (*iter == 'f' || *iter == 'F') { written = nv_ftoa2(va_arg(args, real_t), g_writebuf, precision, max_chars - chars_written, 0); }
          break;
        case 'd':
        case 'i': written = nv_itoa2(va_arg(args, int), g_writebuf, 10, max_chars - chars_written); break;
        case 'z':
          if ((iter + 1) < fmt_end) { iter++; }
          if (*iter == 'i') { written = nv_itoa2(va_arg(args, ssize_t), g_writebuf, 10, max_chars - chars_written); }
          else { written = nv_itoa_u2(va_arg(args, size_t), g_writebuf, 10, max_chars - chars_written); }
          break;
        case 'u': written = nv_itoa_u2(va_arg(args, unsigned), g_writebuf, 10, max_chars - chars_written); break;
        case '#':
          if ((iter + 1) < fmt_end && (*(iter + 1) == 'x'))
          {
            iter++;
            g_writebuf[0] = '0';
            g_writebuf[1] = 'x';
            written       = 2 + nv_itoa_u2(va_arg(args, uintmax_t), g_writebuf + 2, 16, max_chars - chars_written);
          }
          break;
        case 'x': written = nv_itoa2(va_arg(args, intmax_t), g_writebuf, 16, max_chars - chars_written); break;
        case 'p': written = nv_ptoa2(va_arg(args, void*), g_writebuf, max_chars - chars_written); break;
        /* bytes, custom */
        case 'b': written = nv_btoa2(va_arg(args, size_t), 1, g_writebuf, max_chars - chars_written); break;
        case 's':
          s = va_arg(args, const char*) ?: "(null)"; // this operator cool!!
          _nv_printf_write(_writeptr, file, &chars_written, max_chars, s, nv_strlen(s));
          wbuffer_used = false;
          break;
        case 'c':
        case '%':
        default:
          if (chars_written < max_chars - 1)
          {
            // if user is asking for literal % sign, *iter will be the percent sign!!
            char ch = (*iter == 'c') ? (char)va_arg(args, int) : *iter;
            if (file)
              fputc(ch, (FILE*)_writeptr);
            else if (writep)
              *writep++ = ch;
            wbuffer_used = false;
            chars_written++;
          }
          break;
      }

      if (wbuffer_used)
      {
        padding       = padding_w - (int)written;
        char pad_char = pad_zero ? '0' : ' ';
        if (padding < 0) padding = 0;

        if (!left_align && padding > 0)
        {
          nv_memset(pad_buf, pad_char, sizeof(pad_buf));
          while (padding)
          {
            int chunk = (padding > (int)sizeof(pad_buf)) ? (int)sizeof(pad_buf) : padding;
            _nv_printf_write(_writeptr, file, &chars_written, max_chars, pad_buf, chunk);
            padding -= chunk;
          }
        }

        _nv_printf_write(_writeptr, file, &chars_written, max_chars, g_writebuf, written);

        if (left_align && padding > 0)
        {
          nv_memset(pad_buf, pad_char, sizeof(pad_buf));
          while (padding)
          {
            int chunk = (padding > (int)sizeof(pad_buf)) ? (int)sizeof(pad_buf) : padding;
            _nv_printf_write(_writeptr, file, &chars_written, max_chars, pad_buf, chunk);
            padding -= chunk;
          }
        }
      }
    }
    else
    {
      if (chars_written < max_chars - 1)
      {
        if (file) { fputc(*iter, (FILE*)_writeptr); }
        else if (writep)
        {
          *writep = *iter;
          writep++;
        }
        chars_written++;
      }
    }
  }

  if (!file && writep && max_chars > 0)
  {
    size_t w          = (chars_written < max_chars) ? chars_written : max_chars - 1;
    ((char*)vdest)[w] = 0;
  }

  va_end(args);

  return chars_written;
}

// printf

void
_nv_log_error(const char* func, const char* fmt, ...)
{
  // it was funny while it lasted.
  const char* preceder  = " err: ";
  const char* succeeder = "\n";
  va_list     args;
  va_start(args, fmt);
  _nv_log(args, func, succeeder, preceder, fmt, 1);
  va_end(args);
}

void
_nv_log_and_abort(const char* func, const char* fmt, ...)
{
  const char* preceder  = " fatal error: ";
  const char* succeeder = "\nabort.\n";
  va_list     args;
  va_start(args, fmt);
  _nv_log(args, func, succeeder, preceder, fmt, 1);
  va_end(args);
  abort();
}

void
_nv_log_warning(const char* func, const char* fmt, ...)
{
  const char* preceder  = " warning: ";
  const char* succeeder = "\n";
  va_list     args;
  va_start(args, fmt);
  _nv_log(args, func, succeeder, preceder, fmt, 0);
  va_end(args);
}

void
_nv_log_info(const char* func, const char* fmt, ...)
{
  const char* preceder  = " info: ";
  const char* succeeder = "\n";
  va_list     args;
  va_start(args, fmt);
  _nv_log(args, func, succeeder, preceder, fmt, 0);
  va_end(args);
}

void
_nv_log_debug(const char* func, const char* fmt, ...)
{
  const char* preceder  = " debug: ";
  const char* succeeder = "\n";
  va_list     args;
  va_start(args, fmt);
  _nv_log(args, func, succeeder, preceder, fmt, 0);
  va_end(args);
}

void
_nv_log_custom(const char* func, const char* preceder, const char* fmt, ...)
{
  const char* succeeder = "\n";
  va_list     args;
  va_start(args, fmt);
  _nv_log(args, func, succeeder, preceder, fmt, 0);
  va_end(args);
}

// nv_image_t
#  include <jpeglib.h>
#  include <png.h>
#  include <zlib.h>

const char*
get_file_extension(const char* path)
{
  const char* dot = strrchr(path, '.');
  // Imagine someone actually uses this project.
  // And then they see this.
  if (!dot || dot == path) return "piss";
  return dot + 1;
}

int
nv_bufcompress(const void* NV_RESTRICT input, size_t input_size, void* NV_RESTRICT output, size_t* NV_RESTRICT output_size)
{
  z_stream stream = nv_zero_init(z_stream);

  if (deflateInit(&stream, Z_BEST_COMPRESSION) != Z_OK) { return -1; }

  stream.next_in  = (unsigned char*)input;
  stream.avail_in = input_size;

  stream.next_out  = output;
  stream.avail_out = *output_size;

  if (deflate(&stream, Z_FINISH) != Z_STREAM_END)
  {
    deflateEnd(&stream);
    return -1;
  }

  *output_size = stream.total_out;

  deflateEnd(&stream);
  return 0;
}

int
nv_bufdecompress(const void* NV_RESTRICT compressed_data, size_t compressed_size, void* NV_RESTRICT o_buf, size_t o_buf_sz)
{
  z_stream strm  = { 0 };
  strm.next_in   = (unsigned char*)compressed_data;
  strm.avail_in  = compressed_size;
  strm.next_out  = o_buf;
  strm.avail_out = o_buf_sz;

  if (inflateInit(&strm) != Z_OK) { return -1; }

  int ret = inflate(&strm, Z_FINISH);
  if (ret != Z_STREAM_END)
  {
    inflateEnd(&strm);
    return -1;
  }

  inflateEnd(&strm);
  return strm.total_out;
}

nv_image_t
nv_image_load(const char* path)
{
  const char* ext = get_file_extension(path);
  if (nv_strcmp(ext, "jpeg") == 0 || nv_strcmp(ext, "jpg") == 0) { return nv_image_load_jpeg(path); }
  else if (nv_strcmp(ext, "png") == 0) { return nv_image_load_png(path); }
  nv_assert(0);
  return (nv_image_t){};
}

unsigned char*
nv_image_pad_channels(const nv_image_t* src, int dst_channels)
{
  const int src_channels = nv_format_get_num_channels(src->fmt);
  nv_assert(src_channels < dst_channels);

  uint8_t* dst = nv_calloc(src->w * src->h * dst_channels * sizeof(uchar));

  for (size_t y = 0; y < src->h; y++)
  {
    for (size_t x = 0; x < src->w; x++)
    {
      for (int c = 0; c < dst_channels; c++)
      {
        if (c < src_channels) { dst[(y * src->w + x) * dst_channels + c] = src->data[(y * src->w + x) * src_channels + c]; }
        else
        {
          if (c == 3)
          { // alpha channel
            dst[(y * src->w + x) * dst_channels + c] = 255;
          }
          else { dst[(y * src->w + x) * dst_channels + c] = 0; }
        }
      }
    }
  }

  return dst;
}

bool
nv_image_overlay(nv_image_t* dest, const nv_image_t* src, int dst_x_offset, int dst_y_offset, int src_x_offset, int src_y_offset)
{
  nv_assert(dest != NULL);
  nv_assert(src != NULL);

  const int src_channels = nv_format_get_num_channels(src->fmt);

  for (ssize_t y = src_y_offset; y < (ssize_t)src->h; y++)
  {
    for (ssize_t x = src_x_offset; x < (ssize_t)src->w; x++)
    {
      ssize_t dst_x = dst_x_offset + (x - src_x_offset);
      ssize_t dst_y = dst_y_offset + (y - src_y_offset);

      if (dst_x >= 0 && dst_x < (ssize_t)dest->w && dst_y >= 0 && dst_y < (ssize_t)dest->h)
      {
        ssize_t src_i = (y * src->w + x) * src_channels;
        ssize_t dst_i = (dst_y * dest->w + dst_x) * src_channels;

        for (int c = 0; c < src_channels; c++)
        {
          dest->data[dst_i + c] = src->data[src_i + c];
        }
      }
    }
  }

  return 0;
}

void
nv_image_enlarge(nv_image_t* dst, const nv_image_t* src, int scale)
{
  size_t new_w = src->w * scale;

  nv_assert(dst->data != NULL);

  uchar*       write = dst->data;
  const uchar* read  = src->data;

  int bpp = nv_format_get_bytes_per_pixel(src->fmt); // bytes per pixel
  for (size_t y = 0; y < src->h; y++)
  {
    for (size_t x = 0; x < src->w; x++)
    {
      size_t src_i = (y * src->w + x) * bpp;
      for (int i = 0; i < scale; i++)
      {
        for (int j = 0; j < scale; j++)
        {
          size_t dst_i = ((y * scale + i) * new_w + (x * scale + j)) * bpp;
          for (int c = 0; c < bpp; c++)
          {
            write[dst_i + c] = read[src_i + c];
          }
        }
      }
    }
  }
}

void
nv_image_bilinear_filter(nv_image_t* dst, const nv_image_t* src, flt_t scale)
{
  const int nchannels = nv_format_get_num_channels(src->fmt);

  dst->w    = src->w / scale;
  dst->h    = src->w / scale;
  dst->fmt  = src->fmt;
  dst->data = nv_calloc(dst->w * dst->h * nv_format_get_bytes_per_pixel(dst->fmt));

  // Calculate the ratios for x and y coordinates
  flt_t x_ratio, y_ratio;
  if (dst->w > 1) { x_ratio = ((flt_t)src->w - 1.0) / ((flt_t)dst->w - 1.0); }
  else { x_ratio = 0; }

  if (dst->h > 1) { y_ratio = ((flt_t)src->h - 1.0) / ((flt_t)dst->h - 1.0); }
  else { y_ratio = 0; }

  for (size_t y = 0; y < dst->h; y++)
  {
    const flt_t ratiod_y = y_ratio * (flt_t)y;
    flt_t       y_l      = floorf(ratiod_y);
    flt_t       y_h      = ceilf(ratiod_y);
    flt_t       y_weight = (ratiod_y)-y_l;

    const size_t y_l_offset = (size_t)y_l * src->w * nchannels;
    const size_t y_h_offset = (size_t)y_h * src->w * nchannels;

    for (size_t x = 0; x < dst->w; x++)
    {
      const flt_t ratiod_x = x_ratio * (flt_t)x;

      flt_t x_l      = floorf(ratiod_x);
      flt_t x_h      = ceilf(ratiod_x);
      flt_t x_weight = (ratiod_x)-x_l;

      const size_t x_l_offset = (size_t)x_l * nchannels;
      const size_t x_h_offset = (size_t)x_h * nchannels;

      uchar* top_left_pixel     = &src->data[y_l_offset + x_l_offset];
      uchar* top_right_pixel    = &src->data[y_l_offset + x_h_offset];
      uchar* bottom_left_pixel  = &src->data[y_h_offset + x_l_offset];
      uchar* bottom_right_pixel = &src->data[y_h_offset + x_h_offset];
      for (int c = 0; c < nchannels; c++)
      {
        flt_t pixel = top_left_pixel[c] * (1.0 - x_weight) * (1.0 - y_weight) + top_right_pixel[c] * x_weight * (1.0 - y_weight)
            + bottom_left_pixel[c] * y_weight * (1.0 - x_weight) + bottom_right_pixel[c] * x_weight * y_weight;

        dst->data[(y * dst->w + x) * nchannels + c] = (unsigned char)NVM_CLAMP(pixel, 0.0f, 255.0f);
      }
    }
  }
}

nv_image_t
nv_image_load_png(const char* path)
{
  nv_image_t texture = nv_zero_init(nv_image_t);

  FILE* f = fopen(path, "rb");
  if (f == NULL) { return texture; }

  png_struct* png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (png == NULL)
  {
    fclose(f);
    return texture;
  }

  png_info* info = png_create_info_struct(png);
  if (info == NULL)
  {
    fclose(f);
    return texture;
  }

  png_init_io(png, f);
  png_read_info(png, info);

  if (setjmp(png_jmpbuf(png))) { nv_assert(0); }

  texture.w           = png_get_image_width(png, info);
  texture.h           = png_get_image_height(png, info);
  png_byte color_type = png_get_color_type(png, info);
  png_byte bit_depth  = png_get_bit_depth(png, info);

  if (texture.w == 0 || texture.h == 0)
  {
    nv_log_error("zero w/h");
    fclose(f);
    return texture;
  }

  if (color_type == PNG_COLOR_TYPE_PALETTE) { png_set_palette_to_rgb(png); }

  // if image has less than 8 bits per pixel, increase it to 8 bpp
  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) { png_set_expand_gray_1_2_4_to_8(png); }

  if (png_get_valid(png, info, PNG_INFO_tRNS)) { png_set_tRNS_to_alpha(png); }

  png_read_update_info(png, info);

  int channels = png_get_channels(png, info);

  switch (channels)
  {
    case 1: texture.fmt = NOVA_FORMAT_R8; break;
    case 2: texture.fmt = NOVA_FORMAT_RG8; break;
    case 3: texture.fmt = NOVA_FORMAT_RGB8; break;
    case 4: texture.fmt = NOVA_FORMAT_RGBA8; break;
    default:
      nv_log_error("unsupported file(png) format: channels = %d", channels);
      fclose(f);
      png_destroy_read_struct(&png, &info, NULL);
      return nv_zero_init(nv_image_t);
      break;
  }

  int rowbytes = png_get_rowbytes(png, info);
  texture.data = (unsigned char*)nv_malloc(rowbytes * texture.h * channels);
  nv_assert(texture.data != NULL);

  u8** row_pointers = nv_malloc(sizeof(u8*) * texture.h);
  for (size_t y = 0; y < texture.h; y++)
  {
    row_pointers[y] = texture.data + y * texture.w * nv_format_get_bytes_per_pixel(texture.fmt);
  }

  png_read_image(png, row_pointers);

  png_destroy_read_struct(&png, &info, NULL);
  fclose(f);
  nv_free(row_pointers);

  return texture;
}

nv_image_t
nv_image_load_jpeg(const char* path)
{
  struct jpeg_decompress_struct cinfo;
  struct jpeg_error_mgr         jerr;
  FILE*                         f   = NULL;
  nv_image_t                    img = nv_zero_init(nv_image_t);

  if (!path)
  {
    nv_log_error("invalid input path (NULL)");
    return img;
  }

  if ((f = fopen(path, "rb")) == NULL)
  {
    nv_log_error("couldn't open file \"%s\". Are you sure that it exists?", path);
    return img;
  }

  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_decompress(&cinfo);

  jpeg_stdio_src(&cinfo, f);
  if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK)
  {
    nv_log_error("failed to read JPEG header from \"%s\"", path);
    jpeg_destroy_decompress(&cinfo);
    fclose(f);
    return img;
  }

  jpeg_start_decompress(&cinfo);

  img.w = cinfo.output_width;
  img.h = cinfo.output_height;

  switch (cinfo.output_components)
  {
    case 1: img.fmt = NOVA_FORMAT_R8; break;
    case 3: img.fmt = NOVA_FORMAT_RGB8; break;
    default:
      nv_log_error("invalid number of channels: %d", cinfo.output_components);
      jpeg_destroy_decompress(&cinfo);
      fclose(f);
      return img;
  }

  const size_t bytes_per_pixel = nv_format_get_bytes_per_pixel(img.fmt);
  if (bytes_per_pixel == 0)
  {
    nv_log_error("invalid bytes per pixel for format.");
    jpeg_destroy_decompress(&cinfo);
    fclose(f);
    return img;
  }

  img.data = (unsigned char*)nv_malloc(img.w * img.h * bytes_per_pixel);
  if (!img.data)
  {
    nv_log_error("malloc for imagedata failed");
    jpeg_destroy_decompress(&cinfo);
    fclose(f);
    return img;
  }

  unsigned char* bufarr[1];
  for (int i = 0; i < (int)cinfo.output_height; i++)
  {
    bufarr[0] = img.data + i * img.w * bytes_per_pixel;
    if (jpeg_read_scanlines(&cinfo, bufarr, 1) != 1)
    {
      nv_log_error("failed to read scanline %d", i);
      nv_free(img.data);
      jpeg_destroy_decompress(&cinfo);
      fclose(f);
      return nv_zero_init(nv_image_t);
    }
  }

  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  fclose(f);

  return img;
}

void
nv_image_write_png(const nv_image_t* tex, const char* path)
{
  if (tex == NULL || path == NULL || tex->data == NULL) { return; }

  FILE* f = fopen(path, "wb");
  if (!f)
  {
    nv_log_error("Failed to open file: %s", path);
    return;
  }

  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png)
  {
    nv_log_error("png error");
    fclose(f);
    return;
  }

  png_infop info = png_create_info_struct(png);
  if (!info)
  {
    nv_log_error("png error");
    png_destroy_write_struct(&png, NULL);
    fclose(f);
    return;
  }

  if (setjmp(png_jmpbuf(png)))
  {
    nv_log_error("setjmp error");
    png_destroy_write_struct(&png, &info);
    fclose(f);
    return;
  }

  png_init_io(png, f);

  const int numc    = nv_format_get_num_channels(tex->fmt);
  int       coltype = -1;
  switch (numc)
  {
    case 1: coltype = PNG_COLOR_TYPE_GRAY; break;
    case 3: coltype = PNG_COLOR_TYPE_RGB; break;
    case 4: coltype = PNG_COLOR_TYPE_RGBA; break;
    default:
      nv_log_error("Unsupported number of channels: %i", numc);
      png_destroy_write_struct(&png, &info);
      fclose(f);
      return;
  }

  const int bytesperpixel = nv_format_get_bytes_per_pixel(tex->fmt);
  if (bytesperpixel <= 0)
  {
    nv_log_error("invalid bytes per pixel: %i", bytesperpixel);
    png_destroy_write_struct(&png, &info);
    fclose(f);
    return;
  }

  png_set_IHDR(png, info, tex->w, tex->h, bytesperpixel * 8, coltype, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

  png_write_info(png, info);

  png_bytep* row_pointers = (png_bytep*)nv_malloc(sizeof(png_bytep) * tex->h);
  if (!row_pointers)
  {
    nv_log_error("malloc row_pointers failed");
    png_destroy_write_struct(&png, &info);
    fclose(f);
    return;
  }

  for (size_t y = 0; y < tex->h; y++)
  {
    row_pointers[y] = tex->data + y * tex->w * bytesperpixel;
  }

  png_write_image(png, row_pointers);
  nv_free(row_pointers);

  png_write_end(png, NULL);

  png_destroy_write_struct(&png, &info);
  fclose(f);
}

// nv_image_t

void
nv_format_to_string(nv_format format, const char** dst)
{
  switch (format)
  {
    case NOVA_FORMAT_UNDEFINED: *dst = "NOVA_FORMAT_UNDEFINED"; return;
    case NOVA_FORMAT_R8: *dst = "NOVA_FORMAT_R8"; return;
    case NOVA_FORMAT_RG8: *dst = "NOVA_FORMAT_RG8"; return;
    case NOVA_FORMAT_RGB8: *dst = "NOVA_FORMAT_RGB8"; return;
    case NOVA_FORMAT_RGBA8: *dst = "NOVA_FORMAT_RGBA8"; return;
    case NOVA_FORMAT_BGR8: *dst = "NOVA_FORMAT_BGR8"; return;
    case NOVA_FORMAT_BGRA8: *dst = "NOVA_FORMAT_BGRA8"; return;
    case NOVA_FORMAT_RGB16: *dst = "NOVA_FORMAT_RGB16"; return;
    case NOVA_FORMAT_RGBA16: *dst = "NOVA_FORMAT_RGBA16"; return;
    case NOVA_FORMAT_RG32: *dst = "NOVA_FORMAT_RG32"; return;
    case NOVA_FORMAT_RGB32: *dst = "NOVA_FORMAT_RGB32"; return;
    case NOVA_FORMAT_RGBA32: *dst = "NOVA_FORMAT_RGBA32"; return;
    case NOVA_FORMAT_R8_SINT: *dst = "NOVA_FORMAT_R8_SINT"; return;
    case NOVA_FORMAT_RG8_SINT: *dst = "NOVA_FORMAT_RG8_SINT"; return;
    case NOVA_FORMAT_RGB8_SINT: *dst = "NOVA_FORMAT_RGB8_SINT"; return;
    case NOVA_FORMAT_RGBA8_SINT: *dst = "NOVA_FORMAT_RGBA8_SINT"; return;
    case NOVA_FORMAT_R8_UINT: *dst = "NOVA_FORMAT_R8_UINT"; return;
    case NOVA_FORMAT_RG8_UINT: *dst = "NOVA_FORMAT_RG8_UINT"; return;
    case NOVA_FORMAT_RGB8_UINT: *dst = "NOVA_FORMAT_RGB8_UINT"; return;
    case NOVA_FORMAT_RGBA8_UINT: *dst = "NOVA_FORMAT_RGBA8_UINT"; return;
    case NOVA_FORMAT_R8_SRGB: *dst = "NOVA_FORMAT_R8_SRGB"; return;
    case NOVA_FORMAT_RG8_SRGB: *dst = "NOVA_FORMAT_RG8_SRGB"; return;
    case NOVA_FORMAT_RGB8_SRGB: *dst = "NOVA_FORMAT_RGB8_SRGB"; return;
    case NOVA_FORMAT_RGBA8_SRGB: *dst = "NOVA_FORMAT_RGBA8_SRGB"; return;
    case NOVA_FORMAT_BGR8_SRGB: *dst = "NOVA_FORMAT_BGR8_SRGB"; return;
    case NOVA_FORMAT_BGRA8_SRGB: *dst = "NOVA_FORMAT_BGRA8_SRGB"; return;
    case NOVA_FORMAT_D16: *dst = "NOVA_FORMAT_D16"; return;
    case NOVA_FORMAT_D24: *dst = "NOVA_FORMAT_D24"; return;
    case NOVA_FORMAT_D32: *dst = "NOVA_FORMAT_D32"; return;
    case NOVA_FORMAT_D24_S8: *dst = "NOVA_FORMAT_D24_S8"; return;
    case NOVA_FORMAT_D32_S8: *dst = "NOVA_FORMAT_D32_S8"; return;
    case NOVA_FORMAT_BC1: *dst = "NOVA_FORMAT_BC1"; return;
    case NOVA_FORMAT_BC3: *dst = "NOVA_FORMAT_BC3"; return;
    case NOVA_FORMAT_BC7: *dst = "NOVA_FORMAT_BC7"; return;
  }
}

bool
nv_format_has_color_channel(nv_format fmt)
{
  switch (fmt)
  {
    case NOVA_FORMAT_D16:
    case NOVA_FORMAT_D24:
    case NOVA_FORMAT_D24_S8:
    case NOVA_FORMAT_D32:
    case NOVA_FORMAT_D32_S8:
    case NOVA_FORMAT_BC1:
    case NOVA_FORMAT_BC3:
    case NOVA_FORMAT_BC7:
    case NOVA_FORMAT_UNDEFINED: return 0;
    default: return 1;
  }
}

// Returns false even for stencil/depth and undefined format
bool
nv_format_has_alpha_channel(nv_format fmt)
{
  switch (fmt)
  {
    case NOVA_FORMAT_RGBA8:
    case NOVA_FORMAT_BGRA8:
    case NOVA_FORMAT_RGBA16:
    case NOVA_FORMAT_RGBA32:
    case NOVA_FORMAT_RGBA8_SINT:
    case NOVA_FORMAT_RGBA8_UINT:
    case NOVA_FORMAT_RGBA8_SRGB:
    case NOVA_FORMAT_BGRA8_SRGB: return 1;
    default: return 0;
  }
}

bool
nv_format_has_depth_channel(nv_format fmt)
{
  switch (fmt)
  {
    case NOVA_FORMAT_D16:
    case NOVA_FORMAT_D24:
    case NOVA_FORMAT_D24_S8:
    case NOVA_FORMAT_D32:
    case NOVA_FORMAT_D32_S8: return 1;

    default: return 0;
  }
}

bool
nv_format_has_stencil_channel(nv_format fmt)
{
  switch (fmt)
  {
    case NOVA_FORMAT_D24_S8:
    case NOVA_FORMAT_D32_S8: return 1;

    default: return 0;
  }
}

int
nv_format_get_bytes_per_channel(nv_format fmt)
{
  switch (fmt)
  {
    case NOVA_FORMAT_R8:
    case NOVA_FORMAT_RG8:
    case NOVA_FORMAT_RGB8:
    case NOVA_FORMAT_RGBA8:
    case NOVA_FORMAT_BGR8:
    case NOVA_FORMAT_BGRA8:
    case NOVA_FORMAT_R8_SINT:
    case NOVA_FORMAT_RG8_SINT:
    case NOVA_FORMAT_RGB8_SINT:
    case NOVA_FORMAT_RGBA8_SINT:
    case NOVA_FORMAT_R8_UINT:
    case NOVA_FORMAT_RG8_UINT:
    case NOVA_FORMAT_RGB8_UINT:
    case NOVA_FORMAT_RGBA8_UINT:
    case NOVA_FORMAT_R8_SRGB:
    case NOVA_FORMAT_RG8_SRGB:
    case NOVA_FORMAT_RGB8_SRGB:
    case NOVA_FORMAT_RGBA8_SRGB:
    case NOVA_FORMAT_BGR8_SRGB:
    case NOVA_FORMAT_BGRA8_SRGB: return 1;

    case NOVA_FORMAT_RGB16:
    case NOVA_FORMAT_RGBA16:
    case NOVA_FORMAT_D16: return 2;

    case NOVA_FORMAT_D24:
    case NOVA_FORMAT_D24_S8: return 3;

    case NOVA_FORMAT_RG32:
    case NOVA_FORMAT_RGB32:
    case NOVA_FORMAT_RGBA32:
    case NOVA_FORMAT_D32:
    case NOVA_FORMAT_D32_S8: return 4;

    default:
    case NOVA_FORMAT_BC1:
    case NOVA_FORMAT_BC3:
    case NOVA_FORMAT_BC7:
    case NOVA_FORMAT_UNDEFINED: return -1;
  }
}

int
nv_format_get_bytes_per_pixel(nv_format fmt)
{
  return nv_format_get_bytes_per_channel(fmt) * nv_format_get_num_channels(fmt);
}

int
nv_format_get_num_channels(nv_format fmt)
{
  switch (fmt)
  {
    case NOVA_FORMAT_R8:
    case NOVA_FORMAT_R8_SINT:
    case NOVA_FORMAT_R8_UINT:
    case NOVA_FORMAT_R8_SRGB:
    case NOVA_FORMAT_D16:
    case NOVA_FORMAT_D24:
    case NOVA_FORMAT_D32: return 1;

    case NOVA_FORMAT_RG8:
    case NOVA_FORMAT_RG32:
    case NOVA_FORMAT_RG8_SINT:
    case NOVA_FORMAT_RG8_UINT:
    case NOVA_FORMAT_RG8_SRGB:
    case NOVA_FORMAT_D24_S8:
    case NOVA_FORMAT_D32_S8: return 2;

    case NOVA_FORMAT_RGB8:
    case NOVA_FORMAT_BGR8:
    case NOVA_FORMAT_RGB16:
    case NOVA_FORMAT_RGB32:
    case NOVA_FORMAT_RGB8_SINT:
    case NOVA_FORMAT_RGB8_UINT:
    case NOVA_FORMAT_RGB8_SRGB:
    case NOVA_FORMAT_BGR8_SRGB: return 3;

    case NOVA_FORMAT_RGBA8:
    case NOVA_FORMAT_BGRA8:
    case NOVA_FORMAT_RGBA16:
    case NOVA_FORMAT_RGBA32:
    case NOVA_FORMAT_RGBA8_SINT:
    case NOVA_FORMAT_RGBA8_UINT:
    case NOVA_FORMAT_RGBA8_SRGB:
    case NOVA_FORMAT_BGRA8_SRGB: return 4;

    case NOVA_FORMAT_BC1:
    case NOVA_FORMAT_BC3:
    case NOVA_FORMAT_BC7:
      // FIXME: Implement
      return 0;

    case NOVA_FORMAT_UNDEFINED:
    default: return 0;
  }
}

void*
nv_memcpy(void* NV_RESTRICT dst, const void* NV_RESTRICT src, size_t sz)
{
  if (dst == NULL || src == NULL || sz == 0) { return NULL; }

#  if defined(__GNUC__) && (NOVA_STR_USE_BUILTIN)
  return __builtin_memcpy(dst, src, sz);
#  endif

  // I saw this optimization trick a long time ago in some big codebase
  // and it has sticken to me
  // do you know how much I just get an ITCH to write memcpy myself?
  if (((uintptr_t)src & 0x3) == 0 && ((uintptr_t)dst & 0x3) == 0)
  {
    const int* read   = (const int*)src;
    int*       writep = (int*)dst;

    const size_t int_count = sz / sizeof(int);
    for (size_t i = 0; i < int_count; i++)
    {
      writep[i] = read[i];
    }

    const uchar* byte_read  = (uchar*)(read + int_count);
    uchar*       byte_write = (uchar*)(writep + int_count);

    sz %= sizeof(int);
    for (size_t i = 0; i < sz; i++)
    {
      byte_write[i] = byte_read[i];
    }
  }
  else
  {
    const uchar* read   = (const uchar*)src;
    uchar*       writep = (uchar*)dst;
    for (size_t i = 0; i < sz; i++)
    {
      writep[i] = read[i];
    }
  }

  return dst;
}

// rewritten memcpy
void*
nv_memset(void* dst, char to, size_t sz)
{
#  if defined(__GNUC__) && (NOVA_STR_USE_BUILTIN)
  return __builtin_memset(dst, to, sz);
#  endif

  nv_assert(dst != NULL);
  nv_assert(sz != 0);

  uintptr_t d            = (uintptr_t)dst;
  size_t    align_offset = d & (sizeof(size_t) - 1);

  // do not ask what the fuck this is.
  // basically, it's used to project a character to a word
  size_t word_to = 0x0101010101010101ULL * (unsigned char)to;

  unsigned char* byte_write = (unsigned char*)dst;
  while (align_offset && sz)
  {
    *byte_write++ = to;
    sz--;
    align_offset = (uintptr_t)byte_write & (sizeof(size_t) - 1);
  }

  size_t* word_write = (size_t*)byte_write;
  while (sz >= sizeof(size_t) * 4)
  {
    word_write[0] = word_to;
    word_write[1] = word_to;
    word_write[2] = word_to;
    word_write[3] = word_to;
    word_write += 4;
    sz -= sizeof(size_t) * 4;
  }

  while (sz >= sizeof(size_t))
  {
    *word_write++ = word_to;
    sz -= sizeof(size_t);
  }

  byte_write = (unsigned char*)word_write;
  while (sz--)
  {
    *byte_write++ = to;
  }

  return dst;
}

void*
nv_memmove(void* dst, const void* src, size_t sz)
{
#  if defined(__GNUC__) && (NOVA_STR_USE_BUILTIN)
  return __builtin_memmove(dst, src, sz);
#  endif

  if (!dst || !src || sz == 0) { return NULL; }

  if (dst > src && dst < (src + sz))
  {
    unsigned char*       d = (unsigned char*)dst + sz;
    const unsigned char* s = (const unsigned char*)src + sz;
    while (sz--)
    {
      *(--d) = *(--s);
    }
  }
  else
  {
    unsigned char*       d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (sz--)
    {
      *(d++) = *(s++);
    }
  }

  return dst;
}

void*
nv_malloc(size_t sz)
{
  void* ptr = malloc(sz);
  nv_assert(ptr != NULL);
  return ptr;
}

void*
nv_calloc(size_t sz)
{
  void* ptr = calloc(1, sz);
  nv_assert(ptr != NULL);
  return ptr;
}

void*
nv_realloc(void* prevblock, size_t new_sz)
{
  void* ptr = realloc(prevblock, new_sz);
  nv_assert(ptr != NULL);
  return ptr;
}

void
nv_free(void* block)
{
  // fuck you
  nv_assert(block != NULL);
  free(block);
}

void*
nv_memchr(const void* p, int chr, size_t psize)
{
#  if defined(__GNUC__) && (NOVA_STR_USE_BUILTIN)
  return __builtin_memchr(p, chr, psize);
#  endif

  if (!p || !psize) { return NULL; }
  const unsigned char* read = (const unsigned char*)p;
  const unsigned char  chk  = chr;
  for (size_t i = 0; i < psize; i++)
  {
    if (read[i] == chk) return (void*)(read + i);
  }
  return NULL;
}

int
nv_memcmp(const void* _p1, const void* _p2, size_t max)
{
#  if defined(__GNUC__) && (NOVA_STR_USE_BUILTIN)
  return __builtin_memcmp(_p1, _p2, max);
#  endif

  if (!_p1 || !_p2 || max == 0) { return -1; }

  const uchar* p1 = (const uchar*)_p1;
  const uchar* p2 = (const uchar*)_p2;

  // move && compare the pointer p1 until we reach alignment
  while (max > 0 && ((uintptr_t)p1 & (sizeof(size_t) - 1)) != 0)
  {
    if (*p1 != *p2) { return *p1 - *p2; }
    p1++;
    p2++;
    max--;
  }

  const size_t* w1         = (const size_t*)p1;
  const size_t* w2         = (const size_t*)p2;
  size_t        word_count = max / sizeof(size_t);

  for (size_t i = 0; i < word_count; i++)
  {
    if (w1[i] != w2[i])
    {
      p1 = (const uchar*)&w1[i];
      p2 = (const uchar*)&w2[i];
      break;
    }
  }

  max %= sizeof(size_t);
  p1 += word_count * sizeof(size_t);
  p2 += word_count * sizeof(size_t);

  while (max--)
  {
    if (*p1 != *p2) { return *p1 - *p2; }
    p1++;
    p2++;
  }

  return 0;
}

size_t
nv_strncpy2(char* dest, const char* src, size_t max)
{
  size_t slen = nv_strlen(src);

  if (!dest) { return NV_MIN(slen, max); }
  if (!src) { return (size_t)-1; }
  if (max == 0) { return 0; } // we have to have this condition because max is subtracted just after which may cause it to underflow

#  if defined(__GNUC__) && (NOVA_STR_USE_BUILTIN)
  __builtin_strncpy(dest, src, max);
  return NV_MIN(slen, max);
#  endif

  max--;

  while (*src && ((uintptr_t)dest & (sizeof(size_t) - 1)) != 0)
  {
    *dest++ = *src++;
    max--;
  }

  size_t*       destp = (size_t*)dest;
  const size_t* srcp  = (const size_t*)src;
  size_t        words = max / sizeof(size_t);

  for (size_t i = 0; i < words; i++)
  {
    destp[i] = srcp[i];
  }

  dest += words * sizeof(size_t);
  src += words * sizeof(size_t);
  max %= sizeof(size_t);

  while (*src && max)
  {
    *dest++ = *src++;
    max--;
  }

  *dest = 0;

  return NV_MIN(slen, max);
}

char*
nv_strcpy(char* dest, const char* src)
{
#  if defined(__GNUC__) && (NOVA_STR_USE_BUILTIN)
  return __builtin_strcpy(dest, src); // NOLINT(clang-analyzer-security.insecureAPI.strcpy)
#  endif

  if (!dest || !src) { return NULL; }
  size_t i = 0;
  // clang-format off
  while (src[i])
  {
    dest[i] = src[i]; i++;
  }
  // clang-format on
  dest[i] = 0;
}

char*
nv_strncpy(char* dest, const char* src, size_t max)
{
  if (!dest || !src) { return NULL; }
  if (max == 0) { return dest; }

#  if defined(__GNUC__) && (NOVA_STR_USE_BUILTIN)
  return __builtin_strncpy(dest, src, max);
#  endif

  if (max == 0 || !dest || !src) { return NULL; }
  max--; // -1 so we can fit the NULL terminator
  size_t i = 0;
  // clang-format off
  while (i < max && src[i])
  {
    dest[i] = src[i]; i++;
  }
  // clang-format on
  dest[i] = 0;
}

char*
nv_strcat(char* dest, const char* src)
{
  while (*dest)
  {
    dest++; // move to end of dest
  }

  while (*src)
  {
    *dest = *src;
    src++;
    dest++;
  }
  *dest = 0;
  return dest;
}

char*
nv_strncat(char* dest, const char* src, size_t max)
{
  while (*dest)
  {
    dest++; // move to end of dest
  }

  size_t i = 0;
  while (*src && i < max)
  {
    *dest = *src;
    i++;
    src++;
    dest++;
  }
  *dest = 0;
  return dest;
}

char*
nv_strcat_max(char* dest, const char* src, size_t dest_size)
{
  while (*dest)
  {
    dest++;
    dest_size--;
    if (dest_size == 1) // null terminator
      return dest;
  }

  size_t i = 0;

  while (*src && i < dest_size - 1)
  {
    *dest = *src;
    i++;
    src++;
    dest++;
  }

  *dest = 0;
  return dest;
}

int
nv_strcmp(const char* s1, const char* s2)
{
#  if defined(__GNUC__) && (NOVA_STR_USE_BUILTIN)
  return __builtin_strcmp(s1, s2);
#  endif

  while (*s1 && *s2 && (*s1 == *s2))
  {
    s1++;
    s2++;
  }
  return *s2 - *s1;
}

char*
nv_strchr(const char* s, int chr)
{
#  if defined(__GNUC__) && (NOVA_STR_USE_BUILTIN)
  return __builtin_strchr(s, chr);
#  endif

  if (!s) { return NULL; }
  while (*s)
  {
    if (*s == chr) return (char*)s;
    s++;
  }
  return NULL;
}

char*
nv_strrchr(const char* s, int chr)
{
#  if defined(__GNUC__) && (NOVA_STR_USE_BUILTIN)
  return __builtin_strrchr(s, chr);
#  endif

  if (!s) return NULL;

  const char* beg = s;
  s += nv_strlen(s) - 1;
  while (s >= beg)
  {
    if (*s == chr) { return (char*)s; }
    s--;
  }
  return NULL;
}

int
nv_strncmp(const char* s1, const char* s2, size_t max)
{
#  if defined(__GNUC__) && (NOVA_STR_USE_BUILTIN)
  return __builtin_strncmp(s1, s2, max);
#  endif

  if (!s1 || !s2 || max == 0) { return -1; }
  size_t i = 0;
  while (*s1 && *s2 && (*s1 == *s2) && i < max)
  {
    s1++;
    s2++;
    i++;
  }
  return (i == max) ? 0 : (*(const unsigned char*)s1 - *(const unsigned char*)s2);
}

int
nv_strcasencmp(const char* s1, const char* s2, size_t max)
{
  size_t i = 0;
  while (*s1 && *s2 && i < max)
  {
    unsigned char c1 = tolower(*(unsigned char*)s1);
    unsigned char c2 = tolower(*(unsigned char*)s2);
    if (c1 != c2) { return c1 - c2; }
    s1++;
    s2++;
    i++;
  }
  return tolower(*(unsigned char*)s1) - tolower(*(unsigned char*)s2);
}

int
nv_strcasecmp(const char* s1, const char* s2)
{
  while ((uintptr_t)*s1 & (sizeof(size_t) - 1))
  {
    unsigned char c1 = tolower(*(unsigned char*)s1);
    unsigned char c2 = tolower(*(unsigned char*)s2);
    if (c1 != c2) { return c1 - c2; }
    s1++;
    s2++;
  }

  while (1) {}
  return tolower(*(unsigned char*)s1) - tolower(*(unsigned char*)s2);
}

size_t
nv_strlen(const char* s)
{
#  if defined(__GNUC__) && (NOVA_STR_USE_BUILTIN)
  return __builtin_strlen(s);
#  endif

  if (!s) return 0;

  const char* start = s;

  while ((uintptr_t)s & (sizeof(size_t) - 1)) // align s to 8 byte boundary so we can check sizeof(size_t) bytes at once
  {
    if (!*s) return s - start;
    s++;
  }

  const uint64_t mask = 0x0101010101010101ULL;
  while (1)
  {
    uint64_t word = *(uint64_t*)s;
    if (((word - mask) & ~word) & (mask << 7)) break;
    s += 8;
  }

  while (*s)
    s++;

  return s - start;
}

char*
nv_strstr(const char* s, const char* sub)
{
#  if defined(__GNUC__) && (NOVA_STR_USE_BUILTIN)
  return __builtin_strstr(s, sub);
#  endif

  if (!s || !sub) return NULL;

  for (; *s; s++)
  {
    const char* s = s;
    const char* p = sub;

    while (*s && *p && *s == *p)
    {
      s++;
      p++;
    }

    if (!*p) { return (char*)s; }
  }

  return NULL;
}

size_t
nv_strcpy2(char* dest, const char* src)
{
  if (!src) { return (size_t)-1; }
  size_t slen = nv_strlen(src);
  if (!dest) { return slen; }

#  if defined(__GNUC__) && (NOVA_STR_USE_BUILTIN)
  return __builtin_strlen(__builtin_strcpy(dest, src)); // NOLINT(clang-analyzer-security.insecureAPI.strcpy)
#  endif
  const char* original_dest = dest;
  while (*src)
  {
    *dest = *src;
    src++;
    dest++;
  }
  *dest = 0;
  return dest - original_dest;
}

size_t
nv_strspn(const char* s, const char* accept)
{
#  if defined(__GNUC__) && (NOVA_STR_USE_BUILTIN)
  return __builtin_strspn(s, accept);
#  endif

  if (!s || !accept) { return 0; }
  size_t i = 0;
  while (*s && *accept && *s == *accept)
  {
    i++;
  }
  return i;
}

size_t
nv_strcspn(const char* s, const char* reject)
{
#  if defined(__GNUC__) && (NOVA_STR_USE_BUILTIN)
  return __builtin_strcspn(s, reject);
#  endif

  if (!s || !reject) { return 0; }

  const char* base = reject;
  size_t      i    = 0;

  while (*s)
  {
    const char* j = base;
    while (*j && *j != *s)
    {
      j++;
    }
    if (*j) { break; }
    i++;
    s++;
  }
  return i;
}

char*
nv_strpbrk(const char* s1, const char* s2)
{
#  if defined(__GNUC__) && (NOVA_STR_USE_BUILTIN)
  return __builtin_strpbrk(s1, s2);
#  endif

  if (!s1 || !s2) return NULL;

  while (*s1)
  {
    const char* j = s2;
    while (*j)
    {
      if (*j == *s1) { return (char*)s1; }
      j++;
    }
    s1++;
  }
  return NULL;
}

char* strtoks = NULL;
char*
nv_strtok(char* s, const char* delim)
{
  if (!s) s = strtoks;
  char* p;

  s += nv_strspn(s, delim);
  if (!s || *s == 0)
  {
    strtoks = s;
    return NULL;
  }

  p = s;
  s = nv_strpbrk(s, delim);

  if (!s)
  {
    strtoks = nv_strchr(s, 0); // get pointer to last char
    return p;
  }
  *s      = 0;
  strtoks = s + 1;
  return p;
}

char*
nv_basename(const char* path)
{
  char* p         = (char*)path; // shut up C compiler
  char* backslash = nv_strrchr(path, '/');
  if (backslash != NULL) { return backslash + 1; }
  return p;
}

char*
nv_strdup(const char* s)
{
#  if defined(__GNUC__) && (NOVA_STR_USE_BUILTIN)
  return __builtin_strdup(s);
#  endif

  size_t slen  = nv_strlen(s);
  char*  new_s = nv_malloc(slen);
  nv_strncpy(new_s, s, slen);
  new_s[slen] = 0;
  return new_s;
}

char*
nv_substr(const char* s, size_t start, size_t len)
{
  size_t slen = nv_strlen(s);
  if (start + len > slen) { return NULL; }

  char* sub = nv_malloc(len + 1);
  nv_strncpy(sub, s + start, len);
  sub[len] = 0;
  return sub;
}

// header of memory block
typedef struct sablock
{
  size_t   size;
  unsigned canary;
} sablock;

void
nv_allocator_stack_init(nv_allocator_stack* allocator, unsigned char* buf, size_t available)
{
  allocator->buf       = buf;
  allocator->bufsiz    = available;
  allocator->bufoffset = 0;
}

void*
sarealloc(nv_allocator_t* parent, void* prevblock, size_t alignment, size_t size)
{
  if (!prevblock)
  {
    nv_log_and_abort("invalid pointer");
    return NULL;
  }
  sablock* prevblockp = (sablock*)prevblock - 1;
  if (prevblockp->size >= size) { return prevblock; }
  if (prevblockp->canary != NOVA_ALLOCATION_CANARY)
  {
    nv_log_and_abort("corrupt memory");
    return NULL;
  }

  void* new_data = saalloc(parent, alignment, size);
  if (new_data == NULL) { return NULL; }
  nv_memset(new_data, 0, size);
  nv_memcpy(new_data, prevblock, prevblockp->size);
  safree(parent, prevblock);

  return new_data;
}

void*
saalloc(nv_allocator_t* parent, size_t alignment, size_t size)
{
  nv_allocator_stack* allocator = (nv_allocator_stack*)parent->context;
  size                          = ALIGN_UP_SIZE(size, alignment);
  if ((allocator->bufoffset + size + sizeof(sablock)) > allocator->bufsiz)
  {
    nv_log_error("oom"); // out of memory
    return NULL;
  }

  sablock* block = ALIGN_UP(allocator->buf + allocator->bufoffset, alignment);
  block->size    = size;
  block->canary  = NOVA_ALLOCATION_CANARY;
  block++; // move past the header, so return is the memory after header
  allocator->bufoffset += size + sizeof(sablock);
  return (void*)block;
}

void*
sacalloc(nv_allocator_t* parent, size_t alignment, size_t size)
{
  void* allocation = saalloc(parent, alignment, size);
  nv_memset(allocation, 0, size);
  return allocation;
}

void
safree(nv_allocator_t* parent, void* block)
{
  nv_allocator_stack* allocator = (nv_allocator_stack*)parent->context;
  sablock*            p         = (sablock*)block;
  p--;
  nv_assert(p->canary == NOVA_ALLOCATION_CANARY);
  void* allocator_last_block = (allocator->buf + allocator->bufoffset - p->size - sizeof(sablock));
  if (block != allocator_last_block) { return; }
  allocator->bufoffset -= p->size + sizeof(sablock);
  return;
}

nv_allocator_t nv_allocator_default = (nv_allocator_t){ .alloc = heapalloc, .calloc = heapcalloc, .realloc = heaprealloc, .free = heapfree, .context = NULL };

void*
heapalloc(nv_allocator_t* parent, size_t alignment, size_t size)
{
  (void)parent;
  nv_assert((alignment & (alignment - 1)) == 0);

  size += alignment - 1 + sizeof(void*);

  void* orig = malloc(size);
  nv_assert(orig != NULL);

  void* p         = (void*)(((uintptr_t)orig + sizeof(void*) + alignment - 1) & ~(alignment - 1));
  ((void**)p)[-1] = orig;

  return p;
}

void*
heapcalloc(nv_allocator_t* parent, size_t alignment, size_t size)
{
  (void)parent;
  nv_assert((alignment & (alignment - 1)) == 0);

  size += alignment - 1 + sizeof(void*);

  void* orig = nv_calloc(size);
  nv_assert(orig != NULL);

  void* p         = (void*)(((uintptr_t)orig + sizeof(void*) + alignment - 1) & ~(alignment - 1));
  ((void**)p)[-1] = orig;

  return p;
}

void*
heaprealloc(nv_allocator_t* parent, void* prevblock, size_t alignment, size_t size)
{
  (void)parent;
  nv_assert((alignment & (alignment - 1)) == 0);

  size += alignment - 1 + sizeof(void*);

  void* orig = realloc(((void**)prevblock)[-1], size);
  nv_assert(orig != NULL);

  void* p         = (void*)(((uintptr_t)orig + sizeof(void*) + alignment - 1) & ~(alignment - 1));
  ((void**)p)[-1] = orig;

  return p;
}

void
heapfree(nv_allocator_t* parent, void* block)
{
  (void)parent;
  if (block) { free(((void**)block)[-1]); }
}

// A pool is moade of many chunks
nv_node_t*
heap_alloc_internal(size_t alignment, size_t size)
{
  nv_assert((alignment & (alignment - 1)) == 0 && alignment > 0);
  nv_assert(size < SIZE_MAX - alignment - sizeof(nv_node_t) - sizeof(unsigned));

  size += sizeof(nv_node_t);

  size_t total_size = ALIGN_UP_SIZE(size, alignment);

  if (total_size <= 0)
  {
    nv_log_error("zero size malloc\n");
    return NULL;
  }

  void* mapping = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (mapping == MAP_FAILED || !mapping)
  {
    nv_log_error("mmap failed: %s\n", strerror(*(__errno_location())));
    return NULL;
  }

  nv_chunk_t* chk   = (nv_chunk_t*)mapping;
  chk->mapping      = mapping;
  chk->mapping_size = total_size;
  chk->available    = total_size;

  nv_node_t* p = (nv_node_t*)((nv_chunk_t*)mapping + 1);
  *p           = (nv_node_t){
              .payload      = ALIGN_UP(p + 1, alignment),
              .mapping      = mapping,
              .mapping_size = total_size,
              .canary       = NOVA_ALLOCATION_CANARY,
              .in_use       = 1,
  };
  chk->root = p;
  return p;
}

void
heap_free_node_internal(nv_node_t* node)
{
  if (!node)
  {
    nv_log_and_abort("invalid ptr\n");
    return;
  }

  if (node->canary != NOVA_ALLOCATION_CANARY)
  {
    nv_log_and_abort("memory is corrupt\n");
    return;
  }

  void*  mapping = node->mapping;
  size_t size    = node->mapping_size;
  *node          = (nv_node_t){};

  if (munmap(mapping, size) == -1)
  {
    nv_log_error("munmap failed: %s\n", strerror(*(__errno_location())));
    return;
  }
}

void
nv_allocator_heap_init(nv_allocator_heap* pool)
{
  nv_freelist_init(0, heap_alloc_internal, heap_free_node_internal, &nv_allocator_default, &pool->freelist);
}

#  if defined(__GNUC__)
#    define RESTRICT __restrict__
#  else
#    define RESTRICT restrict
#  endif

// deadbeef is for losers
#  define CONT_CANARY 0xFEEF

#  define CONT_IS_VALID(cont) ((cont) && ((cont)->m_canary == CONT_CANARY))

// ==============================
// VECTOR
// ==============================

void
nv_dynarray_init(int typesize, size_t init_size, nv_allocator_t* allocator, nv_dynarray_t* vec)
{
  nv_assert(typesize > 0);
  nv_assert(allocator != NULL);

  *vec            = (nv_dynarray_t){};
  vec->m_size     = 0;
  vec->m_typesize = typesize;
  vec->m_canary   = CONT_CANARY;
  vec->m_mutex    = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
  vec->allocator  = *allocator;

  if (init_size > 0)
  {
    pthread_mutex_lock(&vec->m_mutex);
    vec->m_data     = vec->allocator.calloc(&vec->allocator, 1, vec->m_typesize * init_size);
    vec->m_capacity = init_size;
    pthread_mutex_unlock(&vec->m_mutex);
  }
  else { vec->m_data = NULL; }
}

void
nv_dynarray_destroy(nv_dynarray_t* vec)
{
  if (vec)
  {
    pthread_mutex_lock(&vec->m_mutex);
    nv_assert(CONT_IS_VALID(vec));
    if (vec->m_data)
    {
      vec->allocator.free(&vec->allocator, vec->m_data);
      pthread_mutex_unlock(&vec->m_mutex);
      pthread_mutex_destroy(&vec->m_mutex);
    }
  }
}

void
nv_dynarray_clear(nv_dynarray_t* vec)
{
  pthread_mutex_lock(&vec->m_mutex);
  nv_assert(CONT_IS_VALID(vec));
  vec->m_size = 0;
  pthread_mutex_unlock(&vec->m_mutex);
}

size_t
nv_dynarray_size(const nv_dynarray_t* vec)
{
  pthread_mutex_lock((pthread_mutex_t*)&vec->m_mutex);
  nv_assert(CONT_IS_VALID(vec));
  size_t sz = vec->m_size;
  pthread_mutex_unlock((pthread_mutex_t*)&vec->m_mutex);
  return sz;
}

size_t
nv_dynarray_capacity(const nv_dynarray_t* vec)
{
  pthread_mutex_lock((pthread_mutex_t*)&vec->m_mutex);
  nv_assert(CONT_IS_VALID(vec));
  size_t cap = vec->m_capacity;
  pthread_mutex_unlock((pthread_mutex_t*)&vec->m_mutex);
  return cap;
}

int
nv_dynarray_typesize(const nv_dynarray_t* vec)
{
  pthread_mutex_lock((pthread_mutex_t*)&vec->m_mutex);
  nv_assert(CONT_IS_VALID(vec));
  size_t tsize = vec->m_typesize;
  pthread_mutex_unlock((pthread_mutex_t*)&vec->m_mutex);
  return tsize;
}

void*
nv_dynarray_data(const nv_dynarray_t* vec)
{
  pthread_mutex_lock((pthread_mutex_t*)&vec->m_mutex);
  nv_assert(CONT_IS_VALID(vec));
  void* p = vec->m_data;
  pthread_mutex_unlock((pthread_mutex_t*)&vec->m_mutex);
  return p;
}

void*
nv_dynarray_back(nv_dynarray_t* vec)
{
  pthread_mutex_lock((pthread_mutex_t*)&vec->m_mutex);
  nv_assert(CONT_IS_VALID(vec));
  void* p = nv_dynarray_get(vec, NV_MAX(1ULL, vec->m_size) - 1); // stupid but works
  pthread_mutex_unlock((pthread_mutex_t*)&vec->m_mutex);
  return p;
}

void*
nv_dynarray_get(const nv_dynarray_t* vec, size_t i)
{
  pthread_mutex_lock((pthread_mutex_t*)&vec->m_mutex);
  nv_assert(CONT_IS_VALID(vec));
  uchar* data     = vec->m_data;
  size_t typesize = vec->m_typesize;
  pthread_mutex_unlock((pthread_mutex_t*)&vec->m_mutex);
  return data + (typesize * i);
}

void
nv_dynarray_set(nv_dynarray_t* vec, size_t i, void* elem)
{
  pthread_mutex_lock((pthread_mutex_t*)&vec->m_mutex);
  nv_assert(CONT_IS_VALID(vec));
  nv_memcpy((char*)vec + (vec->m_typesize * i), elem, vec->m_typesize);
  pthread_mutex_unlock((pthread_mutex_t*)&vec->m_mutex);
}

void
nv_dynarray_copy_from(const nv_dynarray_t* RESTRICT src, nv_dynarray_t* RESTRICT dst)
{
  pthread_mutex_lock((pthread_mutex_t*)&src->m_mutex);
  pthread_mutex_lock((pthread_mutex_t*)&dst->m_mutex);

  nv_assert(CONT_IS_VALID(src));
  nv_assert(CONT_IS_VALID(dst));

  nv_assert(src->m_typesize == dst->m_typesize);
  if (src->m_size >= dst->m_capacity) { nv_dynarray_resize(dst, src->m_size); }
  dst->m_size = src->m_size;
  nv_memcpy(dst->m_data, src->m_data, src->m_size * src->m_typesize);

  pthread_mutex_unlock((pthread_mutex_t*)&src->m_mutex);
  pthread_mutex_unlock((pthread_mutex_t*)&dst->m_mutex);
}

void
nv_dynarray_move_from(nv_dynarray_t* RESTRICT src, nv_dynarray_t* RESTRICT dst)
{
  pthread_mutex_lock((pthread_mutex_t*)&src->m_mutex);
  pthread_mutex_lock((pthread_mutex_t*)&dst->m_mutex);

  nv_assert(CONT_IS_VALID(src));
  nv_assert(CONT_IS_VALID(dst));

  dst->m_size     = src->m_size;
  dst->m_capacity = src->m_capacity;
  dst->m_data     = src->m_data;

  src->m_size     = 0;
  src->m_capacity = 0;
  src->m_data     = NULL;

  pthread_mutex_unlock((pthread_mutex_t*)&src->m_mutex);
  pthread_mutex_unlock((pthread_mutex_t*)&dst->m_mutex);
}

bool
nv_dynarray_empty(const nv_dynarray_t* vec)
{
  nv_assert(CONT_IS_VALID(vec));
  return (vec->m_size == 0);
}

bool
nv_dynarray_equal(const nv_dynarray_t* vec1, const nv_dynarray_t* vec2)
{
  nv_assert(CONT_IS_VALID(vec1));
  nv_assert(CONT_IS_VALID(vec2));

  pthread_mutex_lock((pthread_mutex_t*)&vec1->m_mutex);
  pthread_mutex_lock((pthread_mutex_t*)&vec2->m_mutex);

  bool eq = 1;
  if (vec1->m_size != vec2->m_size || vec1->m_typesize != vec2->m_typesize) { eq = 0; }

  else if (nv_memcmp(vec1->m_data, vec2->m_data, vec1->m_size * vec1->m_typesize) != 0) { eq = 0; }

  pthread_mutex_unlock((pthread_mutex_t*)&vec1->m_mutex);
  pthread_mutex_unlock((pthread_mutex_t*)&vec2->m_mutex);

  return eq;
}

void
nv_dynarray_resize(nv_dynarray_t* vec, size_t new_size)
{
  nv_assert(CONT_IS_VALID(vec));

  if (vec->m_data) { vec->m_data = vec->allocator.realloc(&vec->allocator, vec->m_data, 1, vec->m_typesize * new_size); }
  else { vec->m_data = vec->allocator.calloc(&vec->allocator, 1, vec->m_typesize * new_size); }
  nv_assert(vec->m_data != NULL);

  vec->m_capacity = new_size;
}

void
nv_dynarray_push_back(nv_dynarray_t* RESTRICT vec, const void* RESTRICT elem)
{
  nv_assert(CONT_IS_VALID(vec));

  pthread_mutex_lock(&vec->m_mutex);

  if (vec->m_size >= vec->m_capacity) { nv_dynarray_resize(vec, NV_MAX(1, vec->m_capacity * 2)); }

  nv_assert(vec->m_data != NULL);
  nv_assert(!(elem >= vec->m_data && elem <= (vec->m_data + vec->m_size))); // breaks restriction rules
  nv_memcpy((uchar*)vec->m_data + (vec->m_size * vec->m_typesize), elem, vec->m_typesize);
  vec->m_size++;

  pthread_mutex_unlock(&vec->m_mutex);
}

void*
nv_dynarray_push_empty(nv_dynarray_t* __restrict vec)
{
  nv_assert(CONT_IS_VALID(vec));

  pthread_mutex_lock(&vec->m_mutex);

  if (vec->m_size >= vec->m_capacity) { nv_dynarray_resize(vec, NV_MAX(1, vec->m_capacity * 2)); }

  nv_assert(vec->m_data != NULL);
  void* p = (uchar*)vec->m_data + (vec->m_size * vec->m_typesize);
  nv_memset(p, 0, vec->m_typesize);
  vec->m_size++;

  pthread_mutex_unlock(&vec->m_mutex);

  return p;
}

void
nv_dynarray_push_set(nv_dynarray_t* RESTRICT vec, const void* RESTRICT arr, size_t count)
{
  nv_assert(CONT_IS_VALID(vec));

  pthread_mutex_lock(&vec->m_mutex);

  size_t required_capacity = vec->m_size + count;
  if (required_capacity >= vec->m_capacity) nv_dynarray_resize(vec, required_capacity);
  nv_memcpy((uchar*)vec->m_data + (vec->m_size * vec->m_typesize), arr, count * vec->m_typesize);
  vec->m_size += count;

  pthread_mutex_unlock(&vec->m_mutex);
}

void
nv_dynarray_pop_back(nv_dynarray_t* vec)
{
  nv_assert(CONT_IS_VALID(vec));

  pthread_mutex_lock(&vec->m_mutex);

  if (vec->m_size > 0) { vec->m_size--; }

  pthread_mutex_unlock(&vec->m_mutex);
}

void
nv_dynarray_pop_front(nv_dynarray_t* vec)
{
  nv_assert(CONT_IS_VALID(vec));

  pthread_mutex_lock(&vec->m_mutex);

  if (vec->m_size > 0)
  {
    vec->m_size--;
    nv_memcpy(vec->m_data, (uchar*)vec->m_data + vec->m_typesize, vec->m_size * vec->m_typesize);
  }

  pthread_mutex_unlock(&vec->m_mutex);
}

void
nv_dynarray_insert(nv_dynarray_t* RESTRICT vec, size_t index, const void* RESTRICT elem)
{
  nv_assert(CONT_IS_VALID(vec));

  pthread_mutex_lock(&vec->m_mutex);

  if (index >= vec->m_capacity) { nv_dynarray_resize(vec, NV_MAX(1, index * 2)); }
  if (index >= vec->m_size) { vec->m_size = index + 1; }
  nv_memcpy((uchar*)vec->m_data + (vec->m_typesize * index), elem, vec->m_typesize);

  pthread_mutex_unlock(&vec->m_mutex);
}

void
nv_dynarray_remove(nv_dynarray_t* vec, size_t index)
{
  nv_assert(CONT_IS_VALID(vec));

  pthread_mutex_lock(&vec->m_mutex);

  if (index >= vec->m_size)
    return;
  else if (vec->m_size - index - 1)
  {
    // please don't ask me what this is
    nv_memcpy((uchar*)vec->m_data + (index * vec->m_typesize), (uchar*)vec->m_data + ((index + 1) * vec->m_typesize), (vec->m_size - index - 1) * vec->m_typesize);
  }
  vec->m_size--;

  pthread_mutex_unlock(&vec->m_mutex);
}

int
nv_dynarray_find(const nv_dynarray_t* RESTRICT vec, const void* RESTRICT elem)
{
  nv_assert(CONT_IS_VALID(vec));

  pthread_mutex_lock((pthread_mutex_t*)&vec->m_mutex);

  for (int i = 0; i < (int)vec->m_size; i++)
  {
    if (nv_memcmp(vec->m_data + (i * vec->m_typesize), elem, vec->m_typesize))
    {
      pthread_mutex_unlock((pthread_mutex_t*)&vec->m_mutex);
      return i;
    }
  }

  pthread_mutex_unlock((pthread_mutex_t*)&vec->m_mutex);
  return -1;
}

void
nv_dynarray_sort(nv_dynarray_t* vec, nv_dynarray_compare_fn compare)
{
  nv_assert(CONT_IS_VALID(vec));

  pthread_mutex_lock(&vec->m_mutex);

  qsort(vec->m_data, vec->m_size, vec->m_typesize, compare);

  pthread_mutex_unlock(&vec->m_mutex);
}

// ==============================
// STRING
// ==============================

#  define _nv_string_alloc(size) str->allocator->alloc(str->allocator, 1, size)
#  define _nv_string_calloc(size) str->allocator->calloc(str->allocator, 1, size)
#  define _nv_string_realloc(prevblock, size) str->allocator->realloc(str->allocator, prevblock, 1, size)
#  define _nv_string_free(size) str->allocator->free(str->allocator, size)

static void
nv_string_resize(nv_string_t* str, int new_capacity)
{
  char* new_data = _nv_string_realloc(str->m_data, new_capacity);
  nv_assert(new_data != NULL);
  str->m_data     = new_data;
  str->m_capacity = new_capacity;
}

nv_string_t
nv_string_init(size_t initial_size, nv_allocator_t* allocator)
{
  nv_string_t str;
  str.allocator  = allocator;
  str.m_capacity = (initial_size > 0) ? initial_size : 1;
  str.m_data     = str.allocator->alloc(str.allocator, 1, str.m_capacity);
  str.m_canary   = CONT_CANARY;
  nv_assert(str.m_data != NULL);

  str.m_data[0] = 0;
  str.m_size    = 0;
  return str;
}

nv_string_t
nv_string_init_str(const char* init, nv_allocator_t* allocator)
{
  nv_assert(init != NULL && nv_strlen(init) > 0);
  nv_string_t str = (nv_string_t){};
  str.allocator   = allocator;
  size_t len      = nv_strlen(init);
  str.m_capacity  = len + 1;
  str.m_data      = str.allocator->alloc(str.allocator, 1, str.m_capacity);
  str.m_canary    = CONT_CANARY;
  nv_assert(str.m_data != NULL);

  nv_strcpy(str.m_data, init);
  str.m_size = len;

  return str;
}

nv_string_t
nv_string_substring(const nv_string_t* str, size_t start, size_t length, nv_allocator_t* new_allocator)
{
  nv_assert(CONT_IS_VALID(str));
  nv_assert(start + length <= str->m_size);

  nv_string_t substr = nv_string_init(length + 1, new_allocator);

  nv_strncpy(substr.m_data, str->m_data + start, length);
  substr.m_data[length] = 0;
  substr.m_size         = length;
  return substr;
}

void
nv_string_destroy(nv_string_t* str)
{
  nv_assert(CONT_IS_VALID(str));
  if (str) { _nv_string_free(str->m_data); }
}

void
nv_string_clear(nv_string_t* str)
{
  nv_assert(CONT_IS_VALID(str));
  if (str)
  {
    str->m_size    = 0;
    str->m_data[0] = 0;
  }
}

size_t
nv_string_length(const nv_string_t* str)
{
  nv_assert(CONT_IS_VALID(str));
  return str->m_size;
}

size_t
nv_string_capacity(const nv_string_t* str)
{
  nv_assert(CONT_IS_VALID(str));
  return str->m_capacity;
}

const char*
nv_string_data(const nv_string_t* str)
{
  nv_assert(CONT_IS_VALID(str));
  return str->m_data;
}

void
nv_string_append(nv_string_t* str, const char* suffix)
{
  nv_assert(CONT_IS_VALID(str));
  nv_assert(suffix != NULL);

  size_t suffix_length = nv_strlen(suffix);
  if (str->m_size + suffix_length + 1 > str->m_capacity) { nv_string_resize(str, str->m_size + suffix_length + 1); }

  nv_strcpy(str->m_data + str->m_size, suffix);
  str->m_size += suffix_length;
}

void
nv_string_append_char(nv_string_t* str, char suffix)
{
  nv_assert(CONT_IS_VALID(str));

  if (str->m_size + 2 > str->m_capacity) { nv_string_resize(str, str->m_size + 2); }

  str->m_data[str->m_size] = suffix;
  str->m_size++;
  str->m_data[str->m_size] = 0;
}

void
nv_string_prepend(nv_string_t* str, const char* prefix)
{
  nv_assert(CONT_IS_VALID(str));
  nv_assert(prefix != NULL);

  size_t prefix_length = nv_strlen(prefix);
  if (str->m_size + prefix_length + 1 > str->m_capacity) { nv_string_resize(str, str->m_size + prefix_length + 1); }

  nv_memmove(str->m_data + prefix_length, str->m_data, str->m_size + 1);
  nv_memcpy(str->m_data, prefix, prefix_length);
  str->m_size += prefix_length;
}

void
nv_string_set(nv_string_t* str, const char* new_str)
{
  nv_assert(CONT_IS_VALID(str));
  nv_assert(new_str != NULL);

  size_t new_length = nv_strlen(new_str);
  if (new_length + 1 > str->m_capacity) { nv_string_resize(str, new_length + 1); }

  nv_strcpy(str->m_data, new_str);
  str->m_size = new_length;
}

size_t
nv_string_find(const nv_string_t* str, const char* substr)
{
  nv_assert(CONT_IS_VALID(str));
  nv_assert(substr != NULL);

  char* pos = nv_strstr(str->m_data, substr);
  return pos ? (size_t)(pos - str->m_data) : (size_t)-1;
}

void
nv_string_remove(nv_string_t* str, size_t index, size_t length)
{
  nv_assert(CONT_IS_VALID(str));
  nv_assert(index < str->m_size);

  if (index + length > str->m_size) { length = str->m_size - index; }

  nv_memmove(str->m_data + index, str->m_data + index + length, str->m_size - index - length + 1);
  str->m_size -= length;
}

void
nv_string_copy_from(const nv_string_t* src, nv_string_t* dst)
{
  nv_assert(CONT_IS_VALID(src));
  nv_assert(CONT_IS_VALID(dst));

  nv_assert(src != NULL);
  nv_assert(dst != NULL);
  nv_string_set(dst, src->m_data);
}

void
nv_string_move_from(nv_string_t* src, nv_string_t* dst)
{
  nv_assert(CONT_IS_VALID(src));
  nv_assert(CONT_IS_VALID(dst));

  nv_assert(src != NULL);
  nv_assert(dst != NULL);
  nv_string_copy_from(src, dst);
  nv_string_destroy(src);
}

// ==============================
// HASHMAP
// ==============================

unsigned
closest_power_of_two(unsigned i)
{
  if (i == 0) { return 1; }
  i--;
  i |= i >> 1;
  i |= i >> 2;
  i |= i >> 4;
  i |= i >> 8;
  i |= i >> 16;
  i++;
  return i;
}

unsigned int
power_of_two_mod(unsigned int x, unsigned int n)
{
  return x & (n - 1);
}

#  define _nv_hashmap_alloc(size) map->allocator->alloc(map->allocator, 1, size)
#  define _nv_hashmap_calloc(size) map->allocator->calloc(map->allocator, 1, size)
#  define _nv_hashmap_free(block) map->allocator->free(map->allocator, block)

void
nv_hashmap_init(int init_size, int keysize, int valuesize, nv_hashmap_hash_fn hash_fn, nv_hashmap_key_equal_fn equal_fn, nv_allocator_t* allocator, nv_hashmap_t* dst)
{
  nv_assert(dst != NULL);
  nv_assert(keysize > 0 && valuesize > 0);

  *dst = (nv_hashmap_t){};

  if (init_size < 0)
  {
    // we do need the root node so just allocate atleast one
    init_size = 1;
  }

  dst->allocator = allocator;
  dst->m_nodes   = (nv_hashmap_node_t**)allocator->calloc(allocator, 8, init_size * sizeof(nv_hashmap_node_t));
  nv_assert(dst->m_nodes != NULL);

  dst->m_hash_fn    = hash_fn ? hash_fn : nv_hashmap_std_hash;
  dst->m_equal_fn   = equal_fn ? equal_fn : nv_hashmap_std_key_eq;
  dst->m_key_size   = keysize;
  dst->m_value_size = valuesize;
  dst->m_entries    = closest_power_of_two(init_size);
  dst->m_size       = 0;
  dst->m_canary     = CONT_CANARY;
}

void
nv_hashmap_destroy(nv_hashmap_t* map)
{
  nv_assert(CONT_IS_VALID(map));
  if (!map->m_nodes) { return; }
  for (size_t i = 0; i < map->m_entries; i++)
  {
    if (map->m_nodes[i]) { _nv_hashmap_free(map->m_nodes[i]); }
  }
  _nv_hashmap_free(map->m_nodes);
}

void
nv_hashmap_resize(nv_hashmap_t* map, int new_size)
{
  nv_assert(CONT_IS_VALID(map));
  nv_hashmap_node_t** old_nodes     = map->m_nodes;
  const size_t        old_m_entries = map->m_entries;

  if (new_size <= 0) { new_size = 1; }

  map->m_entries = closest_power_of_two(new_size);
  map->m_size    = 0;

  map->m_nodes = _nv_hashmap_calloc(new_size * sizeof(nv_hashmap_node_t));
  nv_assert(map->m_nodes != NULL);

  if (old_nodes)
  {
    for (size_t i = 0; i < old_m_entries; i++)
    {
      nv_hashmap_node_t* node = old_nodes[i];
      if (node && node->is_occupied)
      {
        nv_hashmap_insert(map, node->key, node->value);
        _nv_hashmap_free(node);
      }
    }
    _nv_hashmap_free(old_nodes);
  }
}

void
nv_hashmap_clear(nv_hashmap_t* map)
{
  nv_assert(CONT_IS_VALID(map));
  if (!map->m_nodes) { return; }
  for (size_t i = 0; i < map->m_entries; i++)
  {
    if (map->m_nodes[i]) { _nv_hashmap_free(map->m_nodes[i]); }
  }
  _nv_hashmap_free(map->m_nodes);
  map->m_nodes   = NULL;
  map->m_size    = 0;
  map->m_entries = 0;
}

size_t
nv_hashmap_size(const nv_hashmap_t* map)
{
  nv_assert(CONT_IS_VALID(map));
  return map->m_size;
}

size_t
nv_hashmap_capacity(const nv_hashmap_t* map)
{
  nv_assert(CONT_IS_VALID(map));
  return map->m_entries;
}

size_t
nv_hashmap_keysize(const nv_hashmap_t* map)
{
  nv_assert(CONT_IS_VALID(map));
  return map->m_key_size;
}

size_t
nv_hashmap_valuesize(const nv_hashmap_t* map)
{
  nv_assert(CONT_IS_VALID(map));
  return map->m_value_size;
}

nv_hashmap_node_t*
nv_hashmap_iterate(const nv_hashmap_t* map, size_t* __i)
{
  nv_assert(CONT_IS_VALID(map));
  for (; (*__i) < map->m_entries; (*__i)++)
  {
    size_t i = *__i;
    if (map->m_nodes[i] && map->m_nodes[i]->is_occupied)
    {
      (*__i)++;
      return map->m_nodes[i];
    }
  }
  return NULL;
}

nv_hashmap_node_t**
nv_hashmap_root_node(const nv_hashmap_t* map)
{
  nv_assert(CONT_IS_VALID(map));
  return map->m_nodes;
}

void*
nv_hashmap_find(const nv_hashmap_t* NV_RESTRICT map, const void* NV_RESTRICT key)
{
  nv_assert(CONT_IS_VALID(map));
  if (!map->m_nodes) { return NULL; }

  const unsigned begin = (map->m_hash_fn(key, map->m_key_size) % map->m_entries);
  unsigned       i     = begin;
  while (map->m_nodes[i] != NULL && map->m_nodes[i]->is_occupied)
  {
    if (map->m_equal_fn(map->m_nodes[i]->key, key, map->m_key_size)) { return map->m_nodes[i]->value; }
    i = power_of_two_mod((i + 1), map->m_entries);
    if (i == begin) { break; }
  }
  return NULL;
}

void
nv_hashmap_insert(nv_hashmap_t* map, const void* NV_RESTRICT key, const void* NV_RESTRICT value)
{
  nv_assert(CONT_IS_VALID(map));
  // the second check
  if (!map->m_nodes || map->m_size >= (map->m_entries * 3) / 4)
  {
    // The check to whether map->m_entries is greater than 0 is already done in
    // resize();
    nv_hashmap_resize(map, map->m_entries * 2);
  }

  const unsigned begin = power_of_two_mod(map->m_hash_fn(key, map->m_key_size), map->m_entries);
  unsigned       i     = begin;
  while (map->m_nodes[i] && map->m_nodes[i]->is_occupied)
  {
    i = power_of_two_mod((i + 1), map->m_entries);
    if (i == begin) { break; }
  }

  if (!map->m_nodes[i])
  {
    // Batch allocation for the entire node at once.
    void* alloc            = _nv_hashmap_alloc(sizeof(nv_hashmap_node_t) + map->m_key_size + map->m_value_size);
    map->m_nodes[i]        = alloc;
    map->m_nodes[i]->key   = alloc + sizeof(nv_hashmap_node_t);
    map->m_nodes[i]->value = alloc + sizeof(nv_hashmap_node_t) + map->m_key_size;
  }

  nv_memcpy(map->m_nodes[i]->key, key, map->m_key_size);
  nv_memcpy(map->m_nodes[i]->value, value, map->m_value_size);
  map->m_nodes[i]->is_occupied = 1;
  map->m_size++;
}

void
nv_hashmap_insert_or_replace(nv_hashmap_t* map, const void* NV_RESTRICT key, void* NV_RESTRICT value)
{
  nv_assert(CONT_IS_VALID(map));
  if (!map->m_nodes || map->m_size >= (map->m_entries * 3) / 4)
  {
    // The check to whether map->m_entries is greater than 0 is already done in
    // resize();
    nv_hashmap_resize(map, map->m_entries * 2);
  }

  const unsigned begin = power_of_two_mod(map->m_hash_fn(key, map->m_key_size), map->m_entries);
  unsigned       i     = begin;
  while (map->m_nodes[i] && map->m_nodes[i]->is_occupied)
  {
    i = power_of_two_mod((i + 1), map->m_entries);
    if (map->m_equal_fn(map->m_nodes[i]->key, key, map->m_key_size)) { nv_memcpy(map->m_nodes[i]->value, value, map->m_value_size); }
    else if (i == begin) { break; }
  }

  if (!map->m_nodes[i])
  {
    // Batch allocation for the entire node at once.
    void* alloc            = _nv_hashmap_alloc(sizeof(nv_hashmap_node_t) + map->m_key_size + map->m_value_size);
    map->m_nodes[i]        = alloc;
    map->m_nodes[i]->key   = alloc + sizeof(nv_hashmap_node_t);
    map->m_nodes[i]->value = alloc + sizeof(nv_hashmap_node_t) + map->m_key_size;
  }

  nv_memcpy(map->m_nodes[i]->key, key, map->m_key_size);
  nv_memcpy(map->m_nodes[i]->value, value, map->m_value_size);
  map->m_nodes[i]->is_occupied = 1;
  map->m_size++;
}

void
nv_hashmap_serialize(nv_hashmap_t* map, FILE* f)
{
  nv_assert(CONT_IS_VALID(map));
  const int key_size = map->m_key_size;
  const int val_size = map->m_value_size;

  for (size_t i = 0; i < map->m_entries; i++)
  {
    if (map->m_nodes[i] && map->m_nodes[i]->is_occupied)
    {
      void* node_key   = map->m_nodes[i]->key;
      void* node_value = map->m_nodes[i]->value;

      fwrite(node_value, val_size, 1, f);
      fwrite(node_key, key_size, 1, f);
    }
  }
}

void
nv_hashmap_deserialize(nv_hashmap_t* map, FILE* f)
{
  nv_assert(CONT_IS_VALID(map));
  void* key   = nv_malloc(map->m_key_size);
  void* value = nv_malloc(map->m_value_size);

  while (fread(value, map->m_value_size, 1, f) == 1 && fread(key, map->m_key_size, 1, f) == 1)
  {
    nv_hashmap_insert(map, key, value);
  }

  nv_free(key);
  nv_free(value);
}

// ==============================
// ATLAS
// ==============================

void
nv_texture_atlas_init(nv_texture_atlas_t* atlas, size_t width, size_t height, nv_format fmt, int padding)
{
  if (!atlas)
  {
    nv_log_error("Passing null to atlas is not valid");
    return;
  }
  if (width == 0 || height == 0 || nv_format_get_bytes_per_pixel(fmt) == 0)
  {
    nv_log_error("Invalid size/format for atlas");
    return;
  }

  atlas->w       = width;
  atlas->h       = height;
  atlas->fmt     = fmt;
  atlas->padding = padding;
  atlas->data    = (unsigned char*)nv_calloc(width * height * nv_format_get_bytes_per_pixel(atlas->fmt));
  nv_assert(atlas->data != NULL);

  pthread_mutexattr_t attrs = nv_zero_init(pthread_mutexattr_t);
  pthread_mutexattr_init(&attrs);

  pthread_mutex_init(&atlas->mutex, &attrs);
  nv_skyline_bin_init(width, height, &atlas->bin);
}

int
nv_texture_atlas_add(nv_texture_atlas_t* atlas, const nv_image_t* img, size_t* out_x, size_t* out_y)
{
  if (!atlas || !img || img->w <= 0 || img->h <= 0) { return 0; }

  pthread_mutex_lock(&atlas->mutex);

  nv_skyline_rect_t rect = { .w = img->w + 2 * atlas->padding, .h = img->h + 2 * atlas->padding };

  size_t x, y;
  bool   packed = nv_skyline_bin_find_best_placement(&atlas->bin, &rect, &x, &y);

  while (!packed)
  {
    pthread_mutex_unlock(&atlas->mutex);
    nv_texture_atlas_resize(atlas, 2);
    pthread_mutex_lock(&atlas->mutex);

    packed = nv_skyline_bin_find_best_placement(&atlas->bin, &rect, &x, &y);
  }

  nv_skyline_bin_place_rect(&atlas->bin, &rect, x, y);
  *out_x = x + atlas->padding;
  *out_y = y + atlas->padding;

  nv_image_t dst = { .w = atlas->w, .h = atlas->h, .fmt = NOVA_FORMAT_R8, .data = atlas->data };
  nv_image_overlay(&dst, img, *out_x, *out_y, 0, 0);

  pthread_mutex_unlock(&atlas->mutex);
  return 1;
}

void
nv_texture_atlas_resize(nv_texture_atlas_t* atlas, int scale)
{
  pthread_mutex_lock(&atlas->mutex);

  if (atlas->w == 0 || atlas->h == 0)
  {
    nv_log_error("zero size atlas? possible corruption");
    pthread_mutex_unlock(&atlas->mutex);
    return;
  }

  size_t old_w    = atlas->w;
  size_t old_h    = atlas->h;
  size_t new_w    = atlas->w * scale;
  size_t new_h    = atlas->h * scale;
  size_t channels = nv_format_get_bytes_per_pixel(atlas->fmt);

  unsigned char* new_data = nv_calloc(new_w * new_h * channels);
  nv_assert(new_data != NULL);

  if (atlas->data)
  {
    for (size_t y = 0; y < old_h; y++)
    {
      size_t src_offset = y * old_w * channels;
      size_t dst_offset = y * new_w * channels;
      nv_memcpy(&new_data[dst_offset], &atlas->data[src_offset], old_w * channels);
    }
  }

  nv_free(atlas->data);
  atlas->data = new_data;
  atlas->w    = new_w;
  atlas->h    = new_h;

  nv_skyline_bin_resize(&atlas->bin, new_w, new_h);

  pthread_mutex_unlock(&atlas->mutex);
}

int
nv_texture_atlas_finish(nv_texture_atlas_t* atlas)
{
  if (!atlas) return 0;

  pthread_mutex_lock(&atlas->mutex);

  size_t max_w = 0, max_h = 0;
  for (size_t i = 0; i < atlas->bin.nrects; i++)
  {
    nv_skyline_rect_t* r = &atlas->bin.rects[i];
    max_w                = NV_MAX(max_w, r->x + r->w);
    max_h                = NV_MAX(max_h, r->y + r->h);
  }

  size_t optimal_w = max_w, optimal_h = max_h;

  if (optimal_w == atlas->w && optimal_h == atlas->h)
  {
    pthread_mutex_unlock(&atlas->mutex);
    return 0;
  }
  else if (optimal_w == 0 || optimal_h == 0)
  {
    pthread_mutex_unlock(&atlas->mutex);
    return 0;
  }

  if (atlas->w > optimal_w || atlas->h > optimal_h)
  {
    if (max_w == 0 || max_h == 0)
    {
      nv_log_error("0 optimal w/h??");
      return -1;
    }
    size_t channels = nv_format_get_bytes_per_pixel(atlas->fmt);
    if (channels == 0)
    {
      nv_log_error("invalid format?");
      return -1;
    }
    unsigned char* new_data = (unsigned char*)nv_calloc(max_w * max_h * channels);
    if (new_data)
    {
      for (size_t y = 0; y < max_h; y++)
      {
        nv_memcpy(new_data + y * max_w * channels, atlas->data + y * atlas->w * channels, max_w * channels);
      }
      nv_free(atlas->data);
      atlas->data = new_data;
      atlas->w    = max_w;
      atlas->h    = max_h;
    }
  }

  pthread_mutex_unlock(&atlas->mutex);
  return -1;
}

void
nv_texture_atlas_destroy(nv_texture_atlas_t* atlas)
{
  if (!atlas) { return; }

  pthread_mutex_lock(&atlas->mutex);
  if (atlas->data) { nv_free(atlas->data); }
  nv_skyline_bin_destroy(&atlas->bin);
  pthread_mutex_unlock(&atlas->mutex);

  pthread_mutex_destroy(&atlas->mutex);
}

// ==============================
// RECTPACK
// ==============================

void
nv_skyline_bin_init(size_t w, size_t h, nv_skyline_bin_t* bin)
{
  if (!bin) return;

  *bin              = (nv_skyline_bin_t){};
  bin->w            = w;
  bin->h            = h;
  bin->skyline      = (size_t*)nv_calloc(w * sizeof(size_t));
  bin->rects        = NULL;
  bin->nrects       = 0;
  bin->allocd_rects = 0;
}

void
nv_skyline_bin_destroy(nv_skyline_bin_t* bin)
{
  if (!bin) { return; }
  if (bin->rects) { nv_free(bin->rects); }
  if (bin->skyline) { nv_free(bin->skyline); }
}

size_t
nv_skyline_bin_max_height(const nv_skyline_bin_t* bin, size_t x, size_t w)
{
  size_t max_h = 0;
  for (size_t i = x; i < x + w && i < bin->w; i++)
  {
    if (bin->skyline[i] > max_h) {max_h = bin->skyline[i];}
  }
  return max_h;
}

int
nv_skyline_bin_find_best_placement(const nv_skyline_bin_t* bin, const nv_skyline_rect_t* rect, size_t* best_x, size_t* best_y)
{
  size_t min_y = SIZE_MAX;
  *best_x      = SIZE_MAX;
  *best_y      = SIZE_MAX;

  if (rect->w > bin->w) { return -1; }

  size_t max_x = bin->w - rect->w;
  for (size_t x = 0; x <= max_x; x++)
  {
    size_t y = nv_skyline_bin_max_height(bin, x, rect->w);
    if (y + rect->h <= bin->h && y < min_y)
    {
      min_y   = y;
      *best_x = x;
      *best_y = y;
    }
  }
  return (*best_x != SIZE_MAX);
}

void
nv_skyline_bin_place_rect(nv_skyline_bin_t* bin, const nv_skyline_rect_t* rect, size_t x, size_t y)
{
  if (bin->nrects >= bin->allocd_rects)
  {
    size_t new_alloc = (bin->allocd_rects == 0) ? 2 : bin->allocd_rects * 2;

    if (bin->rects) { bin->rects = nv_realloc(bin->rects, new_alloc * sizeof(nv_skyline_rect_t)); }
    else { bin->rects = nv_calloc(new_alloc * sizeof(nv_skyline_rect_t)); }
    bin->allocd_rects = new_alloc;
  }

  bin->rects[bin->nrects++] = (nv_skyline_rect_t){ rect->w, rect->h, x, y };

  for (size_t i = x; i < x + rect->w && i < bin->w; i++)
  {
    bin->skyline[i] = y + rect->h;
  }
}

static int
_nv_skyline_compare_rect(const void* a, const void* b)
{
  return ((const nv_skyline_rect_t*)b)->h - ((const nv_skyline_rect_t*)a)->h;
}

void
nv_skyline_bin_pack_rects(nv_skyline_bin_t* bin, nv_skyline_rect_t* rects, size_t nrects)
{
  qsort(rects, nrects, sizeof(nv_skyline_rect_t), _nv_skyline_compare_rect);
  for (size_t i = 0; i < nrects; i++)
  {
    size_t x, y;
    if (nv_skyline_bin_find_best_placement(bin, &rects[i], &x, &y))
    {
      nv_skyline_bin_place_rect(bin, &rects[i], x, y);
      rects[i].x = x;
      rects[i].y = y;
    }
    else { nv_log_error("failed to pack rect %d", (int)i); }
  }
}

// Please do not look at this.
// Please
// This is stupid and I can't (just don't) want to find a work around
void
nv_skyline_bin_resize(nv_skyline_bin_t* bin, size_t new_w, size_t new_h)
{
  if (!bin) return;

  nv_skyline_rect_t* valid_rects   = NULL;
  size_t             num_valid     = 0;
  nv_skyline_rect_t* invalid_rects = NULL;
  size_t             num_invalid   = 0;

  for (size_t i = 0; i < bin->nrects; i++)
  {
    nv_skyline_rect_t rect = bin->rects[i];
    if (rect.x + rect.w > new_w || rect.y + rect.h > new_h)
    {
      nv_skyline_rect_t* tmp = NULL;
      if (!invalid_rects) { tmp = (nv_skyline_rect_t*)nv_calloc(sizeof(nv_skyline_rect_t)); }
      else { tmp = (nv_skyline_rect_t*)nv_realloc(invalid_rects, (num_invalid + 1) * sizeof(nv_skyline_rect_t)); }
      if (!tmp)
      {
        nv_free(valid_rects);
        nv_free(invalid_rects);
        return;
      }
      invalid_rects                = tmp;
      invalid_rects[num_invalid++] = rect;
    }
    else
    {
      nv_skyline_rect_t* tmp = (nv_skyline_rect_t*)nv_realloc(valid_rects, (num_valid + 1) * sizeof(nv_skyline_rect_t));
      if (!tmp)
      {
        nv_free(valid_rects);
        nv_free(invalid_rects);
        return;
      }
      valid_rects              = tmp;
      valid_rects[num_valid++] = rect;
    }
  }

  if (new_w != bin->w)
  {
    size_t* new_skyline = (size_t*)nv_realloc(bin->skyline, new_w * sizeof(size_t));
    if (!new_skyline)
    {
      nv_log_error("Memory allocation failed for bin->skyline in nv_skyline_bin_resize");
      nv_free(valid_rects);
      nv_free(invalid_rects);
      return;
    }
    // if it's bigger horizontally, clear the new entries
    if (new_w > bin->w)
    {
      for (size_t i = bin->w; i < new_w; i++)
      {
        new_skyline[i] = 0;
      }
    }
    bin->skyline = new_skyline;
  }

  for (size_t i = 0; i < new_w; i++)
  {
    if (bin->skyline[i] > new_h) bin->skyline[i] = new_h;
  }

  for (size_t i = 0; i < num_valid; i++)
  {
    nv_skyline_rect_t rect = valid_rects[i];
    for (size_t x = rect.x; x < rect.x + rect.w && x < new_w; x++)
    {
      if (bin->skyline[x] < rect.y + rect.h) bin->skyline[x] = rect.y + rect.h;
    }
  }

  if (bin->rects) nv_free(bin->rects);
  bin->rects        = valid_rects;
  bin->nrects       = num_valid;
  bin->allocd_rects = num_valid;

  for (size_t i = 0; i < num_invalid; i++)
  {
    size_t x, y;
    if (nv_skyline_bin_find_best_placement(bin, &invalid_rects[i], &x, &y)) { nv_skyline_bin_place_rect(bin, &invalid_rects[i], x, y); }
    else { nv_log_error("failed to repack rect %lu after resize", i); }
  }

  if (invalid_rects) nv_free(invalid_rects);

  bin->w = new_w;
  bin->h = new_h;
}

// ==============================
// BITSET
// ==============================

void
nv_bitset_init(int init_capacity, nv_allocator_t* allocator, nv_bitset_t* set)
{
  if (init_capacity > 0)
  {
    init_capacity  = (init_capacity + 7) / 8;
    set->size      = init_capacity;
    set->allocator = allocator;
    set->data      = set->allocator->calloc(set->allocator, 1, init_capacity * sizeof(uint8_t));
  }
  else { set->size = 0; }
}

void
nv_bitset_set_bit(nv_bitset_t* set, int bitindex)
{
  set->data[bitindex / 8] |= (1 << (bitindex % 8));
}

void
nv_bitset_set_bit_to(nv_bitset_t* set, int bitindex, nv_bitset_bit to)
{
  to ? nv_bitset_set_bit(set, bitindex) : nv_bitset_clear_bit(set, bitindex);
}

void
nv_bitset_clear_bit(nv_bitset_t* set, int bitindex)
{
  set->data[bitindex / 8] &= ~(1 << (bitindex % 8));
}

void
nv_bitset_toggle_bit(nv_bitset_t* set, int bitindex)
{
  set->data[bitindex / 8] ^= (1 << (bitindex % 8));
}

nv_bitset_bit
nv_bitset_access_bit(nv_bitset_t* set, int bitindex)
{
  return (set->data[bitindex / 8] & (1 << (bitindex % 8))) != 0;
}

void
nv_bitset_copy_from(nv_bitset_t* dst, const nv_bitset_t* src)
{
  if (!src->data) { return; }
  if (src->size != dst->size && dst->data)
  {
    dst->allocator->free(dst->allocator, dst->data);
    dst->data = src->allocator->alloc(src->allocator, 1, src->size);
    dst->size = src->size;
  }
  if (dst->data && src->data) { nv_memcpy(dst->data, src->data, src->size); }
}

void
nv_bitset_destroy(nv_bitset_t* set)
{
  set->allocator->free(set->allocator, set->data);
}

void
nv_freelist_check_circle(const nv_freelist_t* list)
{
#  ifndef NDEBUG
  nv_node_t* node = list->m_root;
  nv_node_t *slow = node, *fast = node;

  while (fast && fast->next)
  {
    slow = slow->next;
    fast = fast->next->next;
    if (fast) nv_assert(fast->canary == NOVA_ALLOCATION_CANARY);
    if (slow) nv_assert(slow->canary == NOVA_ALLOCATION_CANARY);

    if (slow == fast)
    {
      nv_log_and_abort("circular freelist");
      return;
    }
  }
#  endif
}

nv_node_t*
nv_freelist_mknode(const nv_freelist_t* list, size_t alignment, size_t size)
{
  nv_assert(CONT_IS_VALID(list));
  nv_freelist_check_circle(list);

  nv_node_t* node = list->m_alloc_fn(alignment, size);
  nv_assert(node != NULL);
  return node;
}

void
nv_freelist_init(size_t init_size, nv_freelist_alloc_fn alloc_fn, nv_freelist_free_fn free_fn, nv_allocator_t* allocator, nv_freelist_t* list)
{
  list->m_alloc_fn = alloc_fn;
  list->m_free_fn  = free_fn;
  if (init_size > 0)
  {
    list->m_root       = nv_freelist_mknode(list, 1, init_size);
    list->m_root->size = init_size;
  }
  else { list->m_root = NULL; }

  list->m_canary = CONT_CANARY;
  (void)allocator;
  nv_freelist_check_circle(list);
}

void
nv_freelist_destroy(nv_freelist_t* list)
{
  nv_assert(CONT_IS_VALID(list));
  nv_freelist_check_circle(list);
  if (!list || !list->m_root) return;

  nv_node_t* node = list->m_root;
  while (node)
  {
    nv_node_t* next = node->next;
    if (node->in_use) { nv_freelist_free(list, node->payload); }
    node = next;
  }
  nv_freelist_check_circle(list);
}

void*
nv_freelist_alloc(nv_freelist_t* list, size_t alignment, size_t size)
{
  nv_assert(CONT_IS_VALID(list));
  nv_freelist_check_circle(list);

  nv_node_t* node = list->m_root;
  while (node)
  {
    size_t aligned_node_size = ALIGN_UP_SIZE(node->mapping_size, alignment);
    if (!node->in_use && aligned_node_size >= size)
    {
      node->in_use  = 1;
      node->payload = ALIGN_UP(node->payload, alignment);
      nv_assert(((uintptr_t)node->payload % alignment) == 0);
      nv_freelist_check_circle(list);
      return node->payload;
    }
    node = node->next;
  }

  node         = nv_freelist_expand(list, alignment, size);
  node->in_use = 1;
  nv_freelist_check_circle(list);
  nv_assert(((uintptr_t)node->payload % alignment) == 0);
  nv_assert(node->payload != NULL);
  return node->payload;
}

nv_node_t*
nv_freelist_expand(nv_freelist_t* list, size_t alignment, size_t expand_by)
{
  nv_assert(CONT_IS_VALID(list));
  nv_freelist_check_circle(list);

  if (!list->m_root)
  {
    list->m_root       = nv_freelist_mknode(list, alignment, expand_by);
    list->m_root->size = expand_by;
    return list->m_root;
  }

  nv_node_t* last_node = list->m_root;
  while (last_node->next)
  {
    last_node = last_node->next;
  }
  // now we have last_node

  nv_node_t* new_node = nv_freelist_mknode(list, alignment, expand_by);
  last_node->next     = new_node;
  new_node->size      = expand_by;
  nv_freelist_check_circle(list);
  return new_node;
}

void
nv_freelist_free(nv_freelist_t* list, void* block)
{
  nv_assert(CONT_IS_VALID(list));
  nv_freelist_check_circle(list);

  if (!block)
  {
    nv_log_info("invalid block");
    return;
  }

  bool       found = 0;
  nv_node_t* node  = list->m_root;
  nv_node_t* prev  = NULL;

  while (node)
  {
    if (block == node->payload)
    {
      found = 1;
      break;
    }
    prev = node;
    node = node->next;
  }

  if (!found)
  {
    nv_log_error("no block found");
    return;
  }

  if (!node->in_use)
  {
    nv_log_error("real_t free");
    return;
  }

  node->in_use = 0;

  if (prev) { prev->next = node->next; }
  else { list->m_root = node->next; }

  list->m_free_fn(node);

  nv_freelist_check_circle(list);
}

nv_node_t*
nv_freelist_find(nv_freelist_t* list, void* alloc)
{
  nv_assert(CONT_IS_VALID(list));
  nv_freelist_check_circle(list);

  nv_node_t* node = list->m_root;
  while (node)
  {
    if (node->payload == alloc) { return node; }
    node = node->next;
  }
  nv_freelist_check_circle(list);
  return NULL;
}

static inline const nv_option_t*
nv_option_find(const nv_option_t* options, int noptions, const char* short_name, const char* long_name)
{
  nv_assert(noptions >= 0);

  size_t i = 0;
  for (const nv_option_t* opt = options; i < (size_t)noptions; opt++, i++)
  {
    if (short_name && opt->short_name && nv_strcmp(opt->short_name, short_name) == 0) { return opt; }
    if (long_name && opt->long_name && nv_strcmp(opt->long_name, long_name) == 0) { return opt; }
  }
  return NULL;
}

static inline const char*
nv_props_get_tp_name(nv_option_type tp)
{
  switch (tp)
  {
    case NV_OP_TYPE_BOOL: return "bool";
    case NV_OP_TYPE_STRING: return "string";
    case NV_OP_TYPE_INT: return "int";
    case NV_OP_TYPE_FLOAT: return "float";
    case NV_OP_TYPE_DOUBLE: return "double";
    default: return "unknown";
  }
}

void
nv_props_gen_help(const nv_option_t* options, int noptions, char* buf, size_t buf_size)
{
  nv_assert(options != NULL);
  nv_assert(noptions > 0);
  nv_assert(buf != NULL);
  nv_assert(buf_size > 0);

  size_t available = buf_size;
  size_t written   = nv_snprintf(buf, 256, "Options: %i\n", noptions);
  nv_assert(available > written);

  available -= written;
  buf += written;

  for (int i = 0; i < noptions; i++)
  {
    const nv_option_t* opt = &options[i];

    const char* short_name = opt->short_name ? opt->short_name : "<empty>";
    const char* long_name  = opt->long_name ? opt->long_name : "<empty>";

    written = nv_snprintf(buf, available, "\t-%s, --%s <%s>\n", short_name, long_name, nv_props_get_tp_name(opt->type));
    buf += written;
    if (written > available) { break; }
    available -= written;
  }
}

static inline int
_nv_props_parse_arg(int argc, char* argv[], const nv_option_t* options, int noptions, char* error, size_t error_size, int* i)
{
  nv_assert(argc != 0);
  nv_assert(argv != NULL);
  nv_assert(options != NULL);
  nv_assert(noptions > 0);
  nv_assert(error != NULL);
  nv_assert(error_size > 0);
  nv_assert(i != NULL);

  char* arg     = argv[*i];
  bool  is_long = false;
  char* name    = NULL;
  char* value   = NULL;

  // not option?
  if (arg[0] != '-') { return 0; }

  if (arg[1] == '-')
  {
    is_long  = 1;
    name     = arg + 2;
    char* eq = nv_strchr(name, '=');
    if (eq)
    {
      *eq   = '\0';
      value = eq + 1;
    }
  }
  else { name = arg + 1; }

  const nv_option_t* opt = is_long ? nv_option_find(options, noptions, NULL, name) : nv_option_find(options, noptions, name, NULL);
  if (!opt)
  {
    nv_snprintf(error, error_size, "unknown option: %s%s", is_long ? "--" : "-", name);
    (*i)++;
    return -1;
  }

  if (opt->type == NV_OP_TYPE_BOOL)
  {
    bool flag_value = true;
    if (is_long && value) { flag_value = nv_atobool(value); }
    if (opt->value) *(bool*)opt->value = flag_value;
    (*i)++;
    return 0;
  }

  if (!value)
  {
    if (!is_long)
    {
      size_t opt_name_len = nv_strlen(opt->short_name);
      size_t arg_name_len = nv_strlen(name);
      if (arg_name_len > opt_name_len) { value = name + opt_name_len; }
      else if ((*i) + 1 < argc && argv[*i + 1][0] != '-') { value = argv[++(*i)]; }
    }
    else
    {
      (*i)++;
      if ((*i) >= argc || argv[*i][0] == '-')
      {
        nv_snprintf(error, error_size, "option --%s requires a value", name);
        return -1;
      }
      value = argv[*i];
    }
  }

  switch (opt->type)
  {
    case NV_OP_TYPE_STRING:
      if (opt->value)
      {
        nv_strncpy((char*)opt->value, value, opt->buffer_size);
        ((char*)opt->value)[opt->buffer_size - 1] = '\0';
      }
      break;
    case NV_OP_TYPE_INT:
      if (opt->value) *(int*)opt->value = nv_atoi(value);
      break;
    case NV_OP_TYPE_FLOAT:
      if (opt->value) *(flt_t*)opt->value = (flt_t)nv_atof(value);
      break;
    case NV_OP_TYPE_DOUBLE:
      if (opt->value) *(real_t*)opt->value = nv_atof(value);
      break;
    default: break;
  }

  (*i)++;
  return 0;
}

int
nv_props_parse(int argc, char* argv[], const nv_option_t* options, int noptions, char* error, size_t error_size)
{
  nv_assert(argc != 0);
  nv_assert(argv != NULL);
  nv_assert(options != NULL);
  nv_assert(noptions > 0);
  nv_assert(error != NULL);
  nv_assert(error_size > 0);

  int  i       = 1; // program name is argv[0]
  bool success = true;

  while (i < argc)
  {
    char* arg = argv[i];
    if (arg[0] == '-')
    {
      int result = _nv_props_parse_arg(argc, argv, options, noptions, error, error_size, &i);
      if (result != 0) { success = false; }
    }
    else { break; }
  }
  return success ? 0 : -1;
}

void*
_nv_async_call_function(void* _task)
{
  nv_async_task_t* task = _task;
  nv_assert(task->canary == 0xDEADBEEF);
  task->result = task->func(task->user_data);

  __atomic_store_n(&task->_completed, 1, __ATOMIC_RELEASE);
  return NULL;
}

int
nv_async_task_launch(nv_async_task_t* NV_RESTRICT task, nv_async_task_fn func, void* NV_RESTRICT user_data)
{
  task->func       = func;
  task->user_data  = user_data;
  task->result     = NULL;
  task->_completed = 0;
  task->canary     = 0xDEADBEEF;

  pthread_mutexattr_t mut_attrs;
  pthread_mutexattr_init(&mut_attrs);
  pthread_mutex_init(&task->lock, &mut_attrs);

  pthread_mutexattr_destroy(&mut_attrs);

  pthread_attr_t attrs;
  pthread_attr_init(&attrs);
  int res = pthread_create(&task->thread, &attrs, _nv_async_call_function, task);

  pthread_attr_destroy(&attrs);
  nv_assert(res == 0);

  return 0;
}

void
nv_async_task_destroy(nv_async_task_t* task)
{
  if (!task) return;
  pthread_mutex_destroy(&task->lock);
  pthread_join(task->thread, NULL);
  nv_memset(task, 0, sizeof(nv_async_task_t));
}

bool
nv_async_is_task_complete(nv_async_task_t* task)
{
  return __atomic_load_n(&task->_completed, __ATOMIC_ACQUIRE);
}

void*
nv_async_get_task_return(nv_async_task_t* task)
{
  return task->result;
}

#endif