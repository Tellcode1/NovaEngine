#include "../../include/sets/sets.h"
#include "../../include/sets/runtime.h"
#include "../../include/std/include/chrclass.h"
#include "../../include/std/include/print.h"
#include "../../include/std/include/strconv.h"
#include "../../include/std/include/string.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

nv_hashmap_t sets_name_variable_pairs_;

#define skip_whitespace(s)                                                                                                                                                    \
  while (*(s) == ' ' || *(s) == '\t')                                                                                                                                         \
  {                                                                                                                                                                           \
    (s)++;                                                                                                                                                                    \
    column++;                                                                                                                                                                 \
  }

static inline nv_error
readfile(const char* fname, char** data, size_t* data_size)
{
  nv_assert_else_return(fname != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(data != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(data_size != NULL, NV_ERROR_INVALID_ARG);

  FILE* f = fopen(fname, "rb");
  if (f == NULL)
  {
    return NV_ERROR_IO_ERROR;
  }

  fseek(f, 0, SEEK_END);
  long const size = ftell(f);
  if (size < 0)
  {
    fclose(f);
    return NV_ERROR_IO_ERROR;
  }
  if (fseek(f, 0, SEEK_SET) != 0)
  {
    fclose(f);
    return NV_ERROR_IO_ERROR;
  }

  *data = (char*)nv_zmalloc(size + 1);
  if (*data == NULL)
  {
    fclose(f);
    return NV_ERROR_MALLOC_FAILED;
  }

  size_t const read_size = fread(*data, 1, size, f);
  fclose(f);

  if (read_size != (size_t)size)
  {
    nv_free(*data);
    *data = NULL;
    return NV_ERROR_IO_ERROR;
  }

  *data_size          = read_size;
  (*data)[*data_size] = '\0';
  return NV_SUCCESS;
}

// returns the column
static size_t
parse_line(const char* line, sets_var_t* dst, sets_error* errcode)
{
  size_t column = 1;

  const char* beg = line;
  skip_whitespace(beg);
  if (*beg == 0 || *beg == '\n')
  {
    /**
     * Return -1 to indicate we didn't do anything this line
     */
    return (size_t)-1;
  }

  if (nv_strncmp(beg, "//", 2) == 0)
  {
    return (size_t)-1;
  }

  if (!nv_isalpha(*beg) && *beg != '_')
  {
    *errcode = SETS_INVALID_VARIABLE_NAME;
    return column;
  }

  const char* end = beg;
  if (!nv_isalnum(*end) && *end != '_')
  {
    *errcode = SETS_INVALID_VARIABLE_NAME;
    return column;
  }

  while (nv_isalnum(*end) || *end == '_')
  {
    end++;
    column++;
  }

  if (*end == '\n')
  {
    *errcode = SETS_EXPECTED_EQUALS;
    return column;
  }
  else if (!(*end == ' ' || *end == '\t' || *end == '='))
  {
    *errcode = SETS_INVALID_VARIABLE_NAME;
    return column;
  }

  size_t variable_name_size = (end - beg) + 1;

  char* name = nv_zmalloc(variable_name_size);
  nv_strlcpy(name, beg, variable_name_size);
  dst->name = name;

  skip_whitespace(end);
  if (*end != '=')
  {
    *errcode = SETS_EXPECTED_EQUALS;
    return column;
  }
  end++; // past =

  skip_whitespace(end);
  if (*end == '\n')
  {
    *errcode = SETS_EXPECTED_INITIALIZER;
    return column;
  }

  beg = end;

  if (nv_isdigit(*end))
  {
    while (nv_isdigit(*end) || *end == '.')
    {
      end++;
      column++;
    }

    if (*end != '\n' && *end != ' ' && *end != '\t' && *end != 0)
    {
      *errcode = SETS_INVALID_INITIALIZER;
      return column;
    }

    // Decimal point in number => double
    if (nv_strnchr(beg, '.', end - beg))
    {
      dst->type      = SETS_VAR_TYPE_DOUBLE;
      dst->value.dbl = nv_atof2(beg, end - beg, NULL);
    }
    else
    {
      dst->type      = SETS_VAR_TYPE_INT;
      dst->value.num = (int)nv_atoi2(beg, end - beg, NULL);
    }
  }
  else if (*end == '\"')
  {
    beg++; // move past "
    end++;
    while (*end != '\"')
    {
      if (*end == '\n')
      {
        *errcode = SETS_ENDING_QUOTE_MISSING;
        return column;
      }
      if (*end == '\\')
      {
        end++;
      }
      end++;
      column++;
    }

    size_t string_size = (end - beg) + 1;
    char*  string      = nv_zmalloc(string_size);

    const char* src_string = beg;
    char*       dst_string = string;
    while (src_string != end)
    {
      if (NV_UNLIKELY(*src_string == '\\'))
      {
        src_string++;
        switch (*src_string)
        {
          case '\a': *dst_string = '\a'; break;
          case '\b': *dst_string = '\b'; break;
          case '\f': *dst_string = '\f'; break;
          case '\n': *dst_string = '\n'; break;
          case '\r': *dst_string = '\r'; break;
          case '\t': *dst_string = '\t'; break;
          case '\v': *dst_string = '\v'; break;
          case '\\': *dst_string = '\\'; break;
          case '\'': *dst_string = '\''; break;
          case '\"': *dst_string = '\"'; break;
          case '\?': *dst_string = '\?'; break;
          default: break;
        }
        dst_string++;
      }
      else
      {
        *dst_string++ = *src_string++;
      }
    }

    dst->type      = SETS_VAR_TYPE_STR;
    dst->value.str = string;

    end++; // move past ending quote
  }
  // true/false
  // extra logic is for later code to allow for more complex stuff like variable reference etc.
  else if (nv_isalpha(*end))
  {
    while (nv_isalnum(*end) || *end == '_')
    {
      end++;
      column++;
    }

    size_t string_size = end - beg;
    if (nv_strncmp(beg, "true", string_size) == 0)
    {
      dst->type       = SETS_VAR_TYPE_BOOL;
      dst->value.flag = true;
    }
    else if (nv_strncmp(beg, "false", string_size) == 0)
    {
      dst->type       = SETS_VAR_TYPE_BOOL;
      dst->value.flag = false;
    }
    else
    {
      *errcode = SETS_UNRECOGNIZED_INITIALIZER;
      return column;
    }
  }
  else
  {
    *errcode = SETS_UNRECOGNIZED_INITIALIZER;
    return column;
  }

  beg = end;

  skip_whitespace(beg);

  if (nv_strncmp(beg, "//", 2) == 0)
  {
    return column;
  }

  while (*beg)
  {
    beg++;
  }

  return column;
}

nv_error
sets_parse(const char* input, sets_var_t** resultarrp, size_t* resultarrsize)
{
  char* dup = nv_strdup(input);

  const char* line_start  = dup;
  const char* newline_pos = nv_strchr(line_start, '\n');

  size_t      allocated_vars  = 16;
  size_t      num_parsed_vars = 0;
  sets_var_t* parsed_vars     = nv_zmalloc(sizeof(sets_var_t) * allocated_vars);

  size_t line = 1;
  while (*line_start != 0 && newline_pos != NULL)
  {
    newline_pos = nv_strchr(line_start, '\n');

    sets_var_t var;
    sets_error error  = SETS_SUCCESS;
    size_t     column = 0;

    column = parse_line(line_start, &var, &error);

    if (newline_pos)
    {
      line_start = newline_pos + 1;
    }

    if (error != SETS_SUCCESS)
    {
      nv_log_error("[%zu:%zu] %s\n", line, column, sets_error_to_string(error));
      line++;
      continue;
    }

    // The column returned was equal to -1, indicated we need to move off this line as it's useless.
    if (column == (size_t)-1)
    {
      line++;
      if (newline_pos)
      {
        line_start = newline_pos + 1;
        continue;
      }
      continue;
    }

    if (num_parsed_vars >= allocated_vars)
    {
      allocated_vars *= 2;
      parsed_vars = nv_realloc(parsed_vars, sizeof(sets_var_t) * allocated_vars);
      if (!parsed_vars)
      {
        return NV_ERROR_MALLOC_FAILED;
      }
    }
    parsed_vars[num_parsed_vars++] = var;

    line++;
  }

  if (resultarrp)
  {
    *resultarrp = parsed_vars;
  }
  if (resultarrsize)
  {
    *resultarrsize = num_parsed_vars;
  }

  return NV_SUCCESS;
}

nv_error
sets_parse_file(const char* fpath, sets_var_t** resultarrp, size_t* resultarrsize)
{
  char*    data      = NULL;
  size_t   data_size = 0;
  nv_error retcode   = sets_parse_includes_to_buffer(fpath, &data, &data_size);
  if (retcode != NV_SUCCESS)
    return retcode;

  retcode = sets_parse(data, resultarrp, resultarrsize);
  if (retcode != NV_SUCCESS)
    return retcode;

  // Free file data
  nv_free(data);

  return retcode;
}

nv_error
sets_parse_includes_to_file(const char* file, FILE* out)
{
  nv_assert_else_return(file != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(out != NULL, NV_ERROR_INVALID_ARG);

  char*  contents      = NULL;
  size_t contents_size = 0;

  nv_error const code = readfile(file, &contents, &contents_size);
  if (code != NV_SUCCESS)
  {
    return code;
  }

  char* ctx  = NULL;
  char* line = nv_strtok_r(contents, "\n", &ctx);

  while (line != NULL)
  {
    char* p = line;
    while (*p == ' ' || *p == '\t')
    {
      p++;
    }

    if (*p != '#')
    {
      fputs(line, out);
      fputc('\n', out);
      line = nv_strtok_r(NULL, "\n", &ctx);
      continue;
    }

    p++;

    while (*p == ' ' || *p == '\t')
    {
      p++;
    }

    if (nv_strncmp(p, "include", 7) == 0 && (p[7] == ' ' || p[7] == '\t' || p[7] == '\"'))
    {
      p += sizeof("include") - 1;

      while (*p == ' ' || *p == '\t')
      {
        p++;
      }

      if (*p != '\"')
      {
        fputs(line, out);
        fputc('\n', out);
        line = nv_strtok_r(NULL, "\n", &ctx);
        continue;
      }

      const char* start = p + 1;
      const char* end   = nv_strchr(start, '\"');

      if ((end == NULL) || end <= start)
      {
        fputs(line, out);
        fputc('\n', out);
        line = nv_strtok_r(NULL, "\n", &ctx);
        continue;
      }

      size_t const name_len = end - start;

      char tmp[1] = { 0 };

      /* 1) figure out directory of `file` */
      char*       base = NULL;
      const char* s1   = nv_strrchr(file, '/');
      const char* s2   = nv_strrchr(file, '\\');
      const char* sep  = s1 > s2 ? s1 : s2;
      if (sep != NULL)
      {
        size_t const dir_len = sep - file + 1; /* include the slash */
        base                 = (char*)nv_zmalloc(dir_len + 1);
        nv_memcpy(base, file, dir_len);
        base[dir_len] = '\0';
      }
      else
      {
        /**
         * Point base to a 0 byte.
         * This effectively sets the string to 0 length
         */
        base = tmp;
      }

      size_t const full_len = nv_strlen(base) + name_len;

      char* fullpath = (char*)nv_zmalloc(full_len + 1);
      nv_snprintf(fullpath, full_len + 1, "%s%.*s", base, (int)name_len, start);
      fullpath[full_len] = 0;

      nv_error const sub = sets_parse_includes_to_file(fullpath, out);
      if (sub != NV_SUCCESS)
      {
        return sub;
      }

      fputc('\n', out);
      line = nv_strtok_r(NULL, "\n", &ctx);

      /**
       * We may point base to a stack
       * buffer. We dont' want to free that.
       */
      if (base != tmp)
      {
        nv_free(base);
      }
      nv_free(fullpath);
      continue;
    }

    fputs(line, out);
    fputc('\n', out);
    line = nv_strtok_r(NULL, "\n", &ctx);
  }

  nv_free(contents);
  return NV_SUCCESS;
}

nv_error
sets_parse_includes_to_buffer(const char* file, char** buffer, size_t* buffer_size)
{
  nv_assert_else_return(file != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(buffer != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(buffer_size != NULL, NV_ERROR_INVALID_ARG);

  char*  contents      = NULL;
  size_t contents_size = 0;

  nv_error const code = readfile(file, &contents, &contents_size);
  if (code != NV_SUCCESS)
  {
    return code;
  }

  if ((*buffer == NULL) || *buffer_size == 0)
  {
    const size_t buffer_start_size = 4096;

    *buffer      = (char*)nv_zmalloc(buffer_start_size);
    *buffer_size = buffer_start_size;
  }

  const size_t buflen = nv_strlen(*buffer);
  if ((*buffer_size - buflen) <= contents_size)
  {
    const size_t new_buffer_size = NV_MAX(*buffer_size * 2, buflen + contents_size);
    *buffer                      = (char*)nv_realloc(*buffer, new_buffer_size);
    *buffer_size                 = new_buffer_size;
  }

  char* ctx  = NULL;
  char* line = nv_strtok_r(contents, "\n", &ctx);

  while (line != NULL)
  {
    char* p = line;
    while (*p == ' ' || *p == '\t')
    {
      p++;
    }

    if (*p != '#')
    {
      nv_strlcat(*buffer, line, *buffer_size);
      nv_strlcat(*buffer, "\n", *buffer_size);
      line = nv_strtok_r(NULL, "\n", &ctx);
      continue;
    }

    p++;

    while (*p == ' ' || *p == '\t')
    {
      p++;
    }

    if (nv_strncmp(p, "include", 7) == 0 && (p[7] == ' ' || p[7] == '\t' || p[7] == '\"'))
    {
      p += sizeof("include") - 1;

      while (*p == ' ' || *p == '\t')
      {
        p++;
      }

      if (*p != '\"')
      {
        nv_strlcat(*buffer, line, *buffer_size);
        nv_strlcat(*buffer, "\n", *buffer_size);
        line = nv_strtok_r(NULL, "\n", &ctx);
        continue;
      }

      const char* start = p + 1;
      const char* end   = nv_strchr(start, '\"');

      if ((end == NULL) || end <= start)
      {
        nv_strlcat(*buffer, line, *buffer_size);
        nv_strlcat(*buffer, "\n", *buffer_size);
        line = nv_strtok_r(NULL, "\n", &ctx);
        continue;
      }

      size_t const name_len = end - start;

      char tmp[1] = { 0 };

      /* 1) figure out directory of `file` */
      char*       base = NULL;
      const char* s1   = nv_strrchr(file, '/');
      const char* s2   = nv_strrchr(file, '\\');
      const char* sep  = s1 > s2 ? s1 : s2;
      if (sep != NULL)
      {
        size_t const dir_len = sep - file + 1; /* include the slash */
        base                 = (char*)nv_zmalloc(dir_len + 1);
        nv_memcpy(base, file, dir_len);
        base[dir_len] = '\0';
      }
      else
      {
        /**
         * Point base to a 0 byte.
         * This effectively sets the string to 0 length
         */
        base = tmp;
      }

      size_t const full_len = nv_strlen(base) + name_len;

      char* fullpath = (char*)nv_zmalloc(full_len + 1);
      nv_snprintf(fullpath, full_len + 1, "%s%.*s", base, (int)name_len, start);
      fullpath[full_len] = 0;

      nv_strlcat(*buffer, "\n", *buffer_size);

      nv_error const sub = sets_parse_includes_to_buffer(fullpath, buffer, buffer_size);
      if (sub != NV_SUCCESS)
      {
        return sub;
      }

      nv_strlcat(*buffer, "\n", *buffer_size);

      line = nv_strtok_r(NULL, "\n", &ctx);

      /**
       * We may point base to a stack
       * buffer. We dont' want to free that.
       */
      if (base != tmp)
      {
        nv_free(base);
      }
      nv_free(fullpath);
      continue;
    }

    nv_strlcat(*buffer, line, *buffer_size);
    nv_strlcat(*buffer, "\n", *buffer_size);
    line = nv_strtok_r(NULL, "\n", &ctx);
  }

  nv_free(contents);
  return NV_SUCCESS;
}