#ifndef __SSL_H__
#define __SSL_H__

#include <stdint.h>

typedef enum ss_tok_type_t
{
  SS_TOK_TYPE_UNKNOWN    = 0,
  SS_TOK_TYPE_KEYWORD    = 1,
  SS_TOK_TYPE_IDENTIFIER = 2,
  SS_TOK_TYPE_INT        = 3,
  SS_TOK_TYPE_FLOAT      = 4,
  SS_TOK_TYPE_STRING     = 5,
  SS_TOK_TYPE_OPERATOR   = 6,
  SS_TOK_TYPE_SYMBOL     = 7,
  SS_TOK_TYPE_TYPE       = 8,
} ss_tok_type_t;

typedef union ss_tok_value_t
{
  double m_d;
  int    m_i;
  char*  m_s;
  char   m_op;
} ss_tok_value_t;

typedef struct ss_tok_t
{
  ss_tok_type_t  m_type;
  ss_tok_value_t m_value;
} ss_tok_t;

extern int ssl(char* in);

#endif //__SSL_H__
