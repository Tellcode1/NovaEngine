#include "ssl.h"
#include "../src/containers/list.h"
#include "../src/std/chrclass.h"
#include "../src/std/print.h"
#include "../src/std/strconv.h"
#include "../src/std/string.h"

static inline bool
isquote(char chr)
{
  return chr == '"';
}

bool
isfloatchar(char chr)
{
  return nv_chr_isdigit(chr) || chr == '.' || chr == 'E' || chr == 'e' || chr == 'F' || chr == 'f';
}

bool
ischar(char chr)
{
  return nv_chr_isalpha(chr) || chr == '_';
}

bool
isident(char chr, bool first)
{
  return first ? ischar(chr) : (ischar(chr) || nv_chr_isdigit(chr));
}

bool
is_in_char_array(const char* word, const char* array[], size_t array_len)
{
  size_t len = nv_strlen(word);
  for (size_t i = 0; i < array_len; i++)
  {
    size_t keylen = nv_strlen(array[i]);
    if (len == keylen && nv_strncmp(array[i], word, len) == 0)
    {
      return 1;
    }
  }

  return 0;
}

bool
iskeyword(const char* word, size_t len)
{
  const char* keywords[] = {
    "for", "while", "do", "struct", "gaming", "typedef",
  };

  for (size_t i = 0; i < nv_arrlen(keywords); i++)
  {
    size_t keylen = nv_strlen(keywords[i]);
    if (len == keylen && nv_strncmp(keywords[i], word, len) == 0)
    {
      return 1;
    }
  }

  return 0;
}

#define PEEK() (*iter)
#define PEEK_NEXT() (*iter ? *(iter + 1) : 0)

#define ADVANCE()                                                                                                                                                             \
  do                                                                                                                                                                          \
  {                                                                                                                                                                           \
    if (*iter == '\n')                                                                                                                                                        \
    {                                                                                                                                                                         \
      line++;                                                                                                                                                                 \
      column = 1;                                                                                                                                                             \
    }                                                                                                                                                                         \
    else                                                                                                                                                                      \
    {                                                                                                                                                                         \
      column++;                                                                                                                                                               \
    }                                                                                                                                                                         \
    /* This may cause an invalid read on the while loop so we must check if we are at the null terminator */                                                                  \
    if (*iter)                                                                                                                                                                \
    {                                                                                                                                                                         \
      iter++;                                                                                                                                                                 \
    }                                                                                                                                                                         \
  } while (0)

int
ssl(char* input)
{
  char*       iter          = nv_strdup(input);
  char* const in_allocation = iter;
  if (!iter)
  {
    return 0;
  }

  const char* operators = "+-=><!";
  const char* symbols   = "(){}[],;:";
  const char* types[]   = {
    "int",
    "float",
    "double",
    "string",
  };

  nv_list_t toks;
  nv_list_init(sizeof(ss_tok_t), 16, nv_allocator_get_default(), &toks);

  size_t line   = 1;
  size_t column = 1;

  // ORDER MATTERS!!!

  while (PEEK())
  {
    while (PEEK() && nv_chr_isspace(PEEK()))
    {
      ADVANCE();
    }

    // comments aer handled so stupidly omg I should be killded
    if (PEEK() == '/' && PEEK_NEXT() == '/')
    {
      while (PEEK() && PEEK() != '\n')
      {
        ADVANCE();
      }
      continue;
    }
    else if (PEEK() == '/' && PEEK_NEXT() == '*')
    {
      iter += 2;
      while (PEEK() && !(PEEK() == '*' && PEEK_NEXT() == '/'))
      {
        ADVANCE();
      }
      iter += 2; // closing */
      continue;
    }

    if (nv_strchr(symbols, PEEK()))
    {
      ss_tok_t tok;
      tok.type     = SS_TOK_TYPE_SYMBOL;
      tok.value.op = PEEK();
      nv_list_push_back(&toks, &tok);
      ADVANCE();
      continue;
    }

    if (nv_strchr(operators, PEEK()))
    {
      ss_tok_t tok;
      tok.type     = SS_TOK_TYPE_OPERATOR;
      tok.value.op = PEEK();
      nv_list_push_back(&toks, &tok);
      ADVANCE();
      continue;
    }
    else if (isquote(PEEK()))
    {
      ADVANCE();
      char*        start        = iter;
      const size_t start_line   = line;
      const size_t start_column = column;

      while (PEEK() && (PEEK() != '"' || (*(iter - 1) == '\\')))
      {
        ADVANCE();
      }

      if (PEEK() != '"')
      {
        nv_printf("Unterminated string at %i:%i\n", start_line, start_column);
        continue;
      }

      ss_tok_t tok;
      tok.type    = SS_TOK_TYPE_STRING;
      tok.value.s = nv_substr(start, 0, iter - start);
      nv_list_push_back(&toks, &tok);

      ADVANCE();

      continue;
    }
    else if (ischar(PEEK()))
    {
      char* start = iter;
      bool  first = true;
      while (PEEK() && isident(PEEK(), first))
      {
        first = false;
        ADVANCE();
      }

      char temp = PEEK();
      PEEK()    = 0;

      ss_tok_t tok;
      if (is_in_char_array(start, types, nv_arrlen(types)))
      {
        tok.type = SS_TOK_TYPE_TYPE;
      }
      else if (iskeyword(start, iter - start))
      {
        tok.type = SS_TOK_TYPE_KEYWORD;
      }
      else
      {
        tok.type = SS_TOK_TYPE_IDENTIFIER;
      }

      PEEK() = temp;

      tok.value.s = nv_substr(start, 0, iter - start);
      nv_list_push_back(&toks, &tok);

      continue;
    }
    else if (PEEK() && isfloatchar(PEEK()))
    {
      char* start = iter;
      while (PEEK() && isfloatchar(PEEK()))
      {
        ADVANCE();
      }

      double value = nv_atof(start, iter - start);

      ss_tok_t tok;

      if ((double)((int)value) == value)
      {
        tok.type    = SS_TOK_TYPE_INT;
        tok.value.i = (int)value;
      }
      else
      {
        tok.type    = SS_TOK_TYPE_FLOAT;
        tok.value.d = value;
      }

      nv_list_push_back(&toks, &tok);
      continue;
    }
    else
    {
      ADVANCE();
      continue;
    }
  }

  for (size_t i = 0; i < nv_list_size(&toks); i++)
  {
    ss_tok_t* tk = (ss_tok_t*)nv_list_get(&toks, i);

    switch (tk->type)
    {
      case SS_TOK_TYPE_UNKNOWN: nv_printf("UNKNOWN??\n"); break;
      case SS_TOK_TYPE_KEYWORD: nv_printf("KEYWORD %s\n", tk->value.s); break;
      case SS_TOK_TYPE_IDENTIFIER: nv_printf("IDENTIFIER %s\n", tk->value.s); break;
      case SS_TOK_TYPE_INT: nv_printf("INT %i\n", tk->value.i); break;
      case SS_TOK_TYPE_FLOAT: nv_printf("FLOAT %f\n", tk->value.d); break;
      case SS_TOK_TYPE_STRING: nv_printf("STRING %s\n", tk->value.s); break;
      case SS_TOK_TYPE_OPERATOR: nv_printf("OPERATOR %c\n", tk->value.op); break;
      case SS_TOK_TYPE_SYMBOL: nv_printf("SYMBOL %c\n", tk->value.op); break;
      case SS_TOK_TYPE_TYPE: nv_printf("TYPE %s\n", tk->value.s); break;
    }
  }

  for (size_t i = 0; i < nv_list_size(&toks); i++)
  {
    ss_tok_t* tk = (ss_tok_t*)nv_list_get(&toks, i);
    if (tk->type == SS_TOK_TYPE_STRING || tk->type == SS_TOK_TYPE_KEYWORD || tk->type == SS_TOK_TYPE_IDENTIFIER || tk->type == SS_TOK_TYPE_TYPE)
    {
      nv_free(tk->value.s);
    }
  }

  nv_free(in_allocation);
  nv_list_destroy(&toks);

  return 0;
}
