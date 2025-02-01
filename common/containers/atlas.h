#ifndef __NOVA_ATLAS_H__
#define __NOVA_ATLAS_H__

#include "../stdafx.h"
#include "../mem.h"

NOVA_HEADER_START;

typedef struct nv_atlas_t {
    int width, height, next_x, next_y, current_row_height;
    unsigned char *data;
    nv_allocator *allocator;
} nv_atlas_t;

extern nv_atlas_t nv_atlas_init(int init_w, int init_h, nv_allocator *allocator);
extern bool nv_atlas_add_image(nv_atlas_t *__restrict__ atlas, int w, int h, const unsigned char *__restrict__ data, int *__restrict__ x, int *__restrict__ y);

NOVA_HEADER_END;

#endif//__NOVA_ATLAS_H__