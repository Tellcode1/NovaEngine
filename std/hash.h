#ifndef __NOVA_HASH_H__
#define __NOVA_HASH_H__

#include "stdafx.h"
#include "string.h"
#include <stdio.h>

NOVA_HEADER_START

/**
 * Return: the hash computed from the input
 * A modulus will be performed if needed on the returned hash, you need not concern yourself with that
 */
typedef u32 (*nv_hash_fn)(const void* input, size_t input_size, void* user_data);

typedef int (*nv_compare_fn)(const void* key1, const void* key2, size_t size, void* user_data);

static inline u32
nv_hash_fnv1a(const void* input, size_t input_size, void* user_data)
{
  (void)user_data;

  const u32 FNV_PRIME    = 16777619;
  const u32 OFFSET_BASIS = 2166136261;

  const unsigned char* read = (unsigned char*)input;

  u32 hash = OFFSET_BASIS;

  for (size_t byte = 0; byte < input_size; byte++)
  {
    hash ^= read[byte]; // xor
    hash *= FNV_PRIME;
  }
  return hash;
}

static inline u32
nv_hash_murmur3(const void* input, size_t input_size, void* user_data)
{
  (void)user_data;

  const u8*  data    = (const u8*)input;
  const long nblocks = (long)input_size / 4;

  u32 hash = 0;

  const u32 constant1 = 0xcc9e2d51;
  const u32 constant2 = 0x1b873593;
  const u32 constant3 = 0xe6546b64;

  // nblocks may hahve been floored so we still use that
  const u32* blocks = (const u32*)(data + (nblocks * 4));

  for (long i = -nblocks; i; i++)
  {
    u32 k1 = blocks[i];
    k1 *= constant1;
    k1 = (k1 << 15U) | (k1 >> 17U);
    k1 *= constant2;
    hash ^= k1;
    hash = (hash << 13U) | (hash >> 19U);
    hash = hash * 5 + constant3;
  }

  const u8* tail = data + (nblocks * 4);
  u32       k1   = 0;
  switch (input_size & 3U)
  {
    case 3: k1 ^= tail[2] << 16U; NV_FALLTHROUGH;
    case 2: k1 ^= tail[1] << 8U; NV_FALLTHROUGH;
    case 1:
      k1 ^= tail[0];
      k1 *= constant1;
      k1 = (k1 << 15U) | (k1 >> 17U);
      k1 *= constant2;
      hash ^= k1;
  }

  hash ^= input_size;
  hash ^= hash >> 16U;
  hash *= 0x85ebca6b;
  hash ^= hash >> 13U;
  hash *= 0xc2b2ae35;
  hash ^= hash >> 16U;
  return hash;
}
static inline int
nv_compare_default(const void* key1, const void* key2, size_t size, void* user_data)
{
  (void)user_data;
  return nv_memcmp(key1, key2, size);
}

static inline int
nv_compare_string(const void* key1, const void* key2, size_t size, void* user_data)
{
  (void)user_data;
  (void)size;
  return nv_strcmp(key1, key2);
}

NOVA_HEADER_END

#endif //__NOVA_HASH_H__
