#ifndef __NOVA_STR_H__
#define __NOVA_STR_H__

// implementation: core.c

#include "stdafx.h"
#include <stddef.h>
#include <stdint.h>

NOVA_HEADER_START

// Whether to use the __builtin functions provided by GCC
// They are generally faster, so no reason not to?
// Can only work if the program is compiling with GNU C
#ifndef NOVA_STR_USE_BUILTIN
#  define NOVA_STR_USE_BUILTIN 1
#endif

/*
 *  @param sz the number of bytes to copy
 *  @return returns null on error and dst on success
 */
extern void* nv_memcpy(void* NV_RESTRICT dst, const void* NV_RESTRICT src, size_t sz);

/*
 *  @brief sets 'sz' bytes of 'dst' to 'to'
 *  @return returns null on error and dst for success
 */
extern void* nv_memset(void* dst, char to, size_t sz);

/*
 *  copy memory from src to dst
 */
extern void* nv_memmove(void* dst, const void* src, size_t sz);

/*
 *  returns a pointer to the first occurance of chr in p
 *  searches at most psize bytes of p
 */
extern void* nv_memchr(const void* p, int chr, size_t psize);

/*
 *  returns non zero if p1 is not equal to p2
 */
extern int nv_memcmp(const void* p1, const void* p2, size_t max);

// god is dead and i killed him
extern void* nv_malloc(size_t sz);

extern void* nv_calloc(size_t sz);

extern void* nv_realloc(void* prevblock, size_t new_sz);

extern void nv_free(void* block);

/*
 *  uses zlib to compress and decompress the buffer
 *  this works just as you'd expect on images
 *  output should be an allocation of output_size (or bigger)
 */
extern int nv_bufcompress(const void* NV_RESTRICT input, size_t input_size, void* NV_RESTRICT output, size_t* NV_RESTRICT output_size);

/*
 *  o_buf must be allocated with atleast o_buz_sz bytes of memory
 *  @return The number of bytes decompressed
 */
extern unsigned long nv_bufdecompress(const void* NV_RESTRICT compressed_data, size_t compressed_size, void* NV_RESTRICT o_buf, size_t o_buf_sz);

/*
 *  get the size of the string
 *  the size is determined by the position of the null terminator.
 */
extern size_t nv_strlen(const char* s);

/*
 *  copy from src to dest, stopping once it hits the null terminator in src
 */
extern char* nv_strcpy(char* dest, const char* src);

/*
 *  copies min(strlen(dest), min(strlen(src), max)) chars.
 *  ie. the least of the lengths and the max chars
 */
extern char* nv_strncpy(char* dest, const char* src, size_t max);

/*
 *  concatenate src to dest
 */
extern char* nv_strcat(char* dest, const char* src);

/*
 *  concatenate src to dest
 */
extern char* nv_strncat(char* dest, const char* src, size_t max);

/*
 *  copy from src to dest while ensuring that there are no more than dest_size characters in dest
 *  if nv_strlen(dest) > dest_size, this function will return and do nothing.
 */
extern char* nv_strcat_max(char* dest, const char* src, size_t dest_size);

/*
 *  return the number of characters copied.
 */
extern size_t nv_strncpy2(char* dest, const char* src, size_t max);

/*
 *  compare two strings, stopping at either s1 or s2's null terminator or at max.
 *  it will stop when it reaches the null terminator, no segv
 */
extern int nv_strncmp(const char* s1, const char* s2, size_t max);

/*
 *  compare two strings case insensitively
 *  does not care whether s1 or s2 has 'a' or 'a', they're the same thing
 */
extern int nv_strcasencmp(const char* s1, const char* s2, size_t max);

/*
 *  case insensitively compare no more than max chars
 */
extern int nv_strcasecmp(const char* s1, const char* s2);

/*
 *  find the first occurence of a character in a string
 */
extern char* nv_strchr(const char* s, int chr);

/*
 *  find the last occurence of a character in a string
 *  use nv_strstr if you want to find earliest occurence of a string in a string
 */
extern char* nv_strrchr(const char* s, int chr);

/*
 *  strchr() but string
 *  find the earlier occurence of a (sub)string in a string.
 *  eg. for s baller and sub ll
 *  returns a pointer to the first l
 */
extern char* nv_strstr(const char* s, const char* sub);

/*
 *  copy two strings, ensuring null termination
 */
extern size_t nv_strcpy2(char* dest, const char* src);

/*
 *  @return returns 0 when they are equal,
 *  positive number if the lc (last character) of s1 is greater than lc of s2
 *  negative number if the lc (last character) of s1 is less than lc of s2
 */
extern int nv_strcmp(const char* s1, const char* s2);

/*
 *  return the number of characters after which 's' contains a character in 'reject'
 *  for example,
 *  s is balling, reject is hello
 *  so, strcspn will return 2 because after b and a,
 *  l is in both s and reject.
 */
extern size_t nv_strcspn(const char* s, const char* reject);

/*
 *  return the number of characters after which s does not contain a character in accept
 *  ie. the number of similar characters they have.
 *  for example,
 *  s is balling and accept is ball
 *  strspn will return 4 because ball is found in both s and accept and it is of 4 characters.
 */
extern size_t nv_strspn(const char* s, const char* accept);

/*
 * return a pointer ot the first character in s1 that is in both s1 and s2.
 *
 */
extern char* nv_strpbrk(const char* s1, const char* s2);

/*
 *  warning: modifies s directly.
 *  splits 's' by a delimiter
 *  so if you have obama-is-good-gamer,
 *  it'll first return to you 'obama', then 'is' them 'good' then 'gamer'
 *  you should pass null instead of the string for chaining calls
 *  like:
 *  char buf[] = "obama-care-gaming";
 *  char *ptr = strtok(buf, '-'); * this will return 'obama'
 *  while (ptr != null) {
 *    ptr = strtok(null, '-'); * this will first return care, and in second iteration, return gaming
 *  }
 */
extern char* nv_strtok(char* s, const char* delim);

/*
 *  returns the name of the file
 *  basically, ../../pdf/nuclearlaunchcodes.pdf would give you nuclearlaunchcodes.pdf in return.
 */
extern char* nv_basename(const char* path);

/*
 *  duplicate a string (using nv_malloc)
 *  and return it
 */
extern char* nv_strdup(const char* s);

/*
 *  make a substring of the string s
 *  the returned string is malloc'd and must be freed by the caller.
 */
extern char* nv_substr(const char* s, size_t start, size_t len);

NOVA_HEADER_END

#endif //__NOVA_STR_H__
