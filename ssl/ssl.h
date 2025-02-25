#ifndef __SSL_H__
#define __SSL_H__

#include <stdint.h>

typedef enum ss_tok_type_t
{
  SS_TOK_TYPE_UNKNOWN = 0,
  SS_TOK_TYPE_INT     = 1,
  SS_TOK_TYPE_FLOAT   = 2,
  SS_TOK_TYPE_STRING  = 3,
} ss_tok_type_t;

typedef union ss_tok_value_t
{
  double   d;
  intmax_t i;
  char*    s;
} ss_tok_value_t;

typedef struct ss_tok_t
{
  ss_tok_type_t  type;
  ss_tok_value_t value;
} ss_tok_t;

extern int ssl(char* in);

#endif //__SSL_H__