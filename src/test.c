#include "../std/string.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  /*nv_memcpy*/
  {
    char  src[]   = "Hello, world!";
    char  dst[20] = { 0 };
    void* ret     = nv_memcpy(dst, src, strlen(src) + 1);
    nv_assert(ret == dst);
    nv_assert(strcmp(dst, src) == 0);
  }

  /*nv_memset*/
  {
    char  buffer[10];
    void* ret = nv_memset(buffer, 'A', sizeof(buffer));
    nv_assert(ret == buffer);
    for (size_t i = 0; i < sizeof(buffer); ++i)
    {
      nv_assert(buffer[i] == 'A');
    }
  }

  /*nv_memmove*/
  {
    char  src2[20] = "Test memmove";
    char  dst2[20] = { 0 };
    void* ret      = memmove(dst2, src2, strlen(src2) + 1);
    nv_assert(ret == dst2);
    nv_assert(strcmp(dst2, "Test memmove") == 0);
  }

  /*nv_memchr*/
  {
    const char* str = "abcdef";
    char*       pos = (char*)nv_memchr(str, 'd', 6);
    nv_assert(pos && *pos == 'd');
  }

  /*nv_memcmp*/
  {
    int cmp = nv_memcmp("abc", "abc", 3);
    nv_assert(cmp == 0);
    cmp = nv_memcmp("abc", "abd", 3);
    nv_assert(cmp != 0);
  }

  /*nv_malloc*/
  {
    char* p = (char*)nv_malloc(10);
    nv_assert(p != NULL);
    nv_memset(p, 'X', 10);
    nv_free(p);

    p = (char*)nv_calloc(10);
    for (int i = 0; i < 10; i++)
    {
      nv_assert(p[i] == 0);
    }
    char* p2 = (char*)nv_realloc(p, 20);
    nv_assert(p2 != NULL);
    nv_free(p2);
  }

  /*nv_bufcompress*/
  {
    const char* original      = "This is some test data for compression";
    size_t      original_size = strlen(original) + 1;
    size_t      comp_buf_size = original_size * 2; // allocate enough room
    void*       comp_buf      = nv_malloc(comp_buf_size);
    int         comp_result   = nv_bufcompress(original, original_size, comp_buf, &comp_buf_size);
    nv_assert(comp_result == 0);

    void* decomp_buf    = nv_malloc(original_size);
    int   decomp_result = nv_bufdecompress(comp_buf, comp_buf_size, decomp_buf, original_size);
    nv_assert((size_t)decomp_result == original_size);
    nv_assert(strcmp(original, (char*)decomp_buf) == 0);

    nv_free(comp_buf);
    nv_free(decomp_buf);
  }

  /*nv_strlen*/
  {
    size_t len = nv_strlen("Hello");
    nv_assert(len == 5);
  }

  /*nv_strcpy*/
  {
    char  dest[50] = { 0 };
    char* ret      = nv_strcpy(dest, "Copy this");
    nv_assert(ret == dest);
    nv_assert(strcmp(dest, "Copy this") == 0);
  }

  /*nv_strncpy*/
  {
    char dest[50] = { 0 };
    nv_strncpy(dest, "HelloWorld", 5);
    dest[5] = '\0'; // ensure termination
    nv_assert(strcmp(dest, "Hello") == 0);
  }

  /*nv_strcat*/
  {
    char  buffer[50] = "Hello";
    char* ret        = nv_strcat(buffer, " World");
    nv_assert(ret == buffer);
    nv_assert(strcmp(buffer, "Hello World") == 0);
  }

  /*nv_strncat*/
  {
    char  buffer[50] = "Foo";
    char* ret        = nv_strncat(buffer, "BarBaz", 3);
    nv_assert(ret == buffer);
    nv_assert(strcmp(buffer, "FooBar") == 0);
  }

  /*nv_strcat_max*/
  {
    char  buffer[10] = "12345";
    char* ret        = nv_strcat_max(buffer, "6789", sizeof(buffer));
    nv_assert(ret == buffer);
    nv_assert(strcmp(buffer, "123456789") == 0);

    // If dest is already too large, nothing should change.
    char buffer2[10] = "12345678";
    ret              = nv_strcat_max(buffer2, "9", sizeof(buffer2));
    nv_assert(ret == buffer2);
    nv_assert(strcmp(buffer2, "123456789") == 0);
  }

  /*nv_strncpy2*/
  {
    char   dest[50] = { 0 };
    size_t copied   = nv_strncpy2(dest, "abcdef", 4);
    dest[copied]    = '\0';
    nv_assert(copied == 4);
    nv_assert(strcmp(dest, "abcd") == 0);
  }

  /*nv_strncmp*/
  {
    int cmp = nv_strncmp("Hello", "Helium", 3);
    nv_assert(cmp == 0);
    cmp = nv_strncmp("Hello", "Helium", 5);
    nv_assert(cmp != 0);
  }

  /*nv_strcasencmp*/
  {
    int cmp = nv_strcasencmp("abcD", "ABcd", 4);
    nv_assert(cmp == 0);
    cmp = nv_strcasencmp("abcD", "ABce", 4);
    nv_assert(cmp != 0);
  }

  /*nv_strcasecmp*/
  {
    int cmp = nv_strcasecmp("Test", "test");
    nv_assert(cmp == 0);
    cmp = nv_strcasecmp("Test", "toast");
    nv_assert(cmp != 0);
  }

  /*nv_strchr*/
  {
    char* p = nv_strchr("abcdef", 'c');
    nv_assert(p && *p == 'c');
  }

  /*nv_strrchr*/
  {
    char* p = nv_strrchr("abccba", 'c');
    nv_assert(p && *p == 'c');
    /*"abccba*/
    nv_assert(p - "abccba" == 3);
  }

  /*nv_strstr*/
  {
    char* p = nv_strstr("find the needle in the haystack", "needle");
    nv_assert(p && strcmp(p, "needle in the haystack") == 0);
  }

  /*nv_strcpy2*/
  {
    char   dest[50] = { 0 };
    size_t copied   = nv_strcpy2(dest, "Hello again");
    nv_assert(copied == nv_strlen("Hello again"));
    nv_assert(strcmp(dest, "Hello again") == 0);
  }

  /*nv_strcmp*/
  {
    int cmp = nv_strcmp("apple", "apple");
    nv_assert(cmp == 0);
    cmp = nv_strcmp("apple", "banana");
    nv_assert(cmp != 0);
  }

  /*nv_strcspn*/
  {
    size_t span = nv_strcspn("balling", "hello");
    nv_assert(span == 2);
  }

  /*nv_strspn*/
  {
    size_t span = nv_strspn("balling", "ball");
    nv_assert(span == 4);
  }

  /*nv_strpbrk*/
  {
    char* p = nv_strpbrk("abcdef", "xcz");
    nv_assert(p && *p == 'c');
  }

  /*nv_strtok*/
  {
    char buf[50];
    strcpy(buf, "obama-care-gaming");
    char* token = nv_strtok(buf, "-");
    nv_assert(token && strcmp(token, "obama") == 0);
    token = nv_strtok(NULL, "-");
    nv_assert(token && strcmp(token, "care") == 0);
    token = nv_strtok(NULL, "-");
    nv_assert(token && strcmp(token, "gaming") == 0);
    token = nv_strtok(NULL, "-");
    nv_assert(token == NULL);
  }

  /*nv_basename*/
  {
    char* base = nv_basename("/path/to/file.txt");
    nv_assert(base && strcmp(base, "file.txt") == 0);
  }

  /*nv_strdup*/
  {
    char* dup = nv_strdup("duplicate me");
    nv_assert(dup && strcmp(dup, "duplicate me") == 0);
    nv_free(dup);
  }

  /*nv_substr*/
  {
    char* sub = nv_substr("Hello, world!", 7, 5);
    nv_assert(sub && strcmp(sub, "world") == 0);
    nv_free(sub);
  }

  printf("All tests passed.\n");
  return 0;
}
