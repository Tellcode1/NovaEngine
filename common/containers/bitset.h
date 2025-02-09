#ifndef __NOVA_BITSET_H__
#define __NOVA_BITSET_H__

#include "../../std/stdafx.h"
#include "../mem.h"

typedef struct nv_bitset_t nv_bitset_t;
typedef unsigned char      nv_bitset_bit;

void          nv_bitset_init(int init_capacity, nv_allocator_t* allocator, nv_bitset_t* set);
void          nv_bitset_set_bit(nv_bitset_t* set, int bitindex);
void          nv_bitset_set_bit_to(nv_bitset_t* set, int bitindex, nv_bitset_bit to);
void          nv_bitset_clear_bit(nv_bitset_t* set, int bitindex);
void          nv_bitset_toggle_bit(nv_bitset_t* set, int bitindex);
nv_bitset_bit nv_bitset_access_bit(nv_bitset_t* set, int bitindex);
void          nv_bitset_copy_from(nv_bitset_t* dst, const nv_bitset_t* src);
void          nv_bitset_destroy(nv_bitset_t* set);

struct nv_bitset_t
{
  u8*             data;
  size_t          size;
  nv_allocator_t* allocator;
};

#endif //__NOVA_BITSET_H__