#ifndef __NOVA_STR_H__
#define __NOVA_STR_H__

#include "stdafx.h"
#include <stddef.h>
#include <stdint.h>

NOVA_HEADER_START;

// Whether to use the __builtin functions provided by GCC
// They are generally faster, so no reason not to?
#ifndef NOVA_STR_USE_BUILTIN
#define NOVA_STR_USE_BUILTIN 1
#endif

/// @param sz The number of bytes to copy
/// @return Returns NULL on error and dst on success
extern void* nv_memcpy(void* nv_RESTRICT dst, const void* nv_RESTRICT src, size_t sz);

/// @brief Sets 'sz' bytes of 'dst' to 'to'
/// @return Returns NULL on error and dst for success
extern void* nv_memset(void* dst, char to, size_t sz);

// Copy memory from src to dst and clear the copied memory in src
extern void* nv_memmove(void* dst, const void* src, size_t sz);

// Returns a pointer to the first occurance of chr in p
// Searches at most psize bytes of p
extern void* nv_memchr(const void* p, int chr, size_t psize);

// Returns non zero if p1 is not equal to p2
extern int nv_memcmp(const void* p1, const void* p2, size_t max);

// god is dead and I killed him
extern void* nv_malloc(size_t sz);

extern void  nv_free(void* block);

// Uses zlib to compress and decompress the buffer
// this works just as you'd expect on images
// output should be an allocation of output_size (or bigger)
extern int nv_bufcompress(const void* nv_RESTRICT input, size_t input_size, void* nv_RESTRICT output, size_t* nv_RESTRICT output_size);

// o_buf must be allocated with atleast o_buz_sz bytes of memory
extern int nv_bufdecompress(const void* nv_RESTRICT compressed_data, size_t compressed_size, void* nv_RESTRICT o_buf, size_t o_buf_sz);

// Get the size of the string
// The size is determined by the position of the NULL terminator.
extern size_t nv_strlen(const char* s);

// Copy from src to dest, stopping once it hits the NULL terminator in src
extern char* nv_strcpy(char* dest, const char* src);

// Copies min(strlen(dest), min(strlen(src), max)) chars.
// ie. the least of the lengths and the max chars
extern char* nv_strncpy(char* dest, const char* src, size_t max);

// Concatenate src to dest
extern char *nv_strcat(char *dest, const char *src);

// Concatenate src to dest
extern char *nv_strncat(char *dest, const char *src, size_t max);

// Return the number of characters copied.
extern size_t nv_strncpy2(char* dest, const char* src, size_t max);

// Compare two strings, stopping at either s1 or s2's NULL terminator or at max.
// It will stop when it reaches the NULL terminator, no segv
extern int nv_strncmp(const char* s1, const char* s2, size_t max);

// Find the first occurence of a character in a string
extern char* nv_strchr(const char* s, int chr);

// Find the last occurence of a character in a string
// Use nv_strstr if you want to find earliest occurence of a string in a string
extern char* nv_strrchr(const char* s, int chr);

// strchr() but string
// Find the earlier occurence of a (sub)string in a string.
// eg. For s Baller and sub ll
// returns a pointer to the first l
extern char* nv_strstr(const char* s, const char* sub);

// Copy two strings, ensuring NULL termination
extern size_t nv_strcpy2(char* dest, const char* src);

// @return returns 0 when they are equal,
// positive number if the lc (last character) of s1 is greater than lc of s2
// negative number if the lc (last character) of s1 is less than lc of s2
extern int nv_strcmp(const char* s1, const char* s2);

// Return the number of characters after which 's' contains a character in 'reject'
// For example,
// s is Balling, reject is Hello
// so, strcspn will return 2 because after B and a,
// l is in both s and reject.
extern size_t nv_strcspn(const char* s, const char* reject);

// Return the number of characters after which s does NOT contain a character in accept
// ie. the number of similar characters they have.
// For example,
// s is Balling and accept is Ball
// strspn will return 4 because Ball is found in both s and accept and it is of 4 characters.
extern size_t nv_strspn(const char* s, const char* accept);

extern char*  nv_strpbrk(const char* s1, const char* s2);

// Splits 's' by a delimiter
// warning: modifies s directly.
// so if you have obama-is-good-gamer,
// It'll first return to you 'obama', then 'is' them 'good' then 'gamer'
// You should pass NULL instead of the string for chaining calls
// like:
/*
  char buf[] = "obama-care-gaming";
  char *ptr = strtok(buf, '-'); // this will return 'obama'
  while (ptr != NULL) {
    ptr = strtok(NULL, '-'); // this will first return care, and in second iteration, return gaming
  }
*/
extern char* nv_strtok(char* s, const char* delim);

// Returns the NAME of the file
// basically, ../../pdf/nuclearlaunchcodes.pdf would give you nuclearlaunchcodes.pdf in return.
extern char* nv_basename(const char* path);

// Duplicate a string (using nv_malloc)
// and return it
extern char* nv_strdup(const char* s);

// Make a substring of the string s
// The returned string is malloc'd and must be freed by the caller.
extern char* nv_substr(const char* s, size_t start, size_t len);

NOVA_HEADER_END;

#endif //__NOVA_STR_H__
