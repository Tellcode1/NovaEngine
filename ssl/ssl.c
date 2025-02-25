#include "ssl.h"
#include "../common/containers/dynarray.h"
#include "../std/print.h"
#include "../std/strconv.h"
#include "../std/string.h"
#include <ctype.h>

static inline bool
isquote(char chr)
{
  return chr == '"';
}

bool
isfloatchar(char chr)
{
  return isdigit(chr) || chr == '.' || chr == 'E' || chr == 'e' || chr == 'F' || chr == 'f';
}

bool
ischar(char chr)
{
  return isalpha(chr) || chr == '_';
}

bool
isident(char chr, bool first)
{
  return first ? ischar(chr) : (ischar(chr) || isdigit(chr));
}

bool
iskeyword(const char* word, size_t len)
{
  const char* keywords[] = {
    "for", "while", "do", "struct", "gaming", "typedef",
  };

  for (size_t i = 0; i < nv_arrlen(keywords); i++)
  {
    // nv_printf("compare %s %*.s\n", keywords[i], len, word);
    size_t keylen = nv_strlen(keywords[i]);
    if (len == keylen && nv_strncmp(keywords[i], word, len) == 0)
    {
      nv_printf("keyword: %.*s\n", (int)len, word);
      return 1;
    }
  }

  return 0;
}

int
ssl(char* input)
{
  char*       iter          = nv_strdup(input);
  char* const in_allocation = iter;
  if (!iter)
  {
    return 0;
  }

  while (*iter)
  {
    if (isfloatchar(*iter))
    {
      char* start = iter;
      while (isfloatchar(*iter))
      {
        iter++;
      }

      char temp    = *iter;
      *iter        = 0;
      double value = nv_atof(start);
      *iter        = temp;

      if ((double)((intmax_t)value) == value)
      {
        nv_printf("int %i\n", (intmax_t)value);
      }
      else
      {
        nv_printf("float %f\n", value);
      }
    }
    else if (isquote(*iter))
    {
      iter++;
      char* start = iter;
      while (*iter && !isquote(*iter))
      {
        iter++;
      }

      if (*iter)
      {
        *iter = 0;
        iter++;
      }
      nv_printf("quote string %s\n", start);
    }
    else if (ischar(*iter))
    {
      char* start = iter;
      bool  first = true;
      while (isident(*iter, first))
      {
        first = false;
        iter++;
      }

      if (!iskeyword(start, iter - start))
      {
        nv_printf("identifier %.*s\n", (int)(iter - start), start);
      }
    }
    else
    {
      iter++;
    }
  }

  nv_free(in_allocation);
  return 0;
}
