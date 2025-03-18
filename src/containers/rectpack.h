#ifndef __NOVA_RECT_PACK_H__
#define __NOVA_RECT_PACK_H__

#include "../std/stdafx.h"
#include <SDL2/SDL_mutex.h>

NOVA_HEADER_START

typedef struct nv_skyline_bin_t  nv_skyline_bin_t;
typedef struct nv_skyline_rect_t nv_skyline_rect_t;

struct nv_skyline_rect_t
{
  size_t m_width, m_height, m_posx, m_posy;
};

struct nv_skyline_bin_t
{
  unsigned           m_canary;
  size_t*            m_skyline;
  nv_skyline_rect_t* m_rects;
  size_t             m_width, m_height;
  size_t             m_allocated_rect_count;
  size_t             m_num_rects;
  SDL_mutex*         m_mutex;
};

extern void   nv_skyline_bin_init(size_t w, size_t h, nv_skyline_bin_t* bin);
extern void   nv_skyline_bin_destroy(nv_skyline_bin_t* bin);
extern size_t nv_skyline_bin_max_height(const nv_skyline_bin_t* bin, size_t x, size_t w);
extern int    nv_skyline_bin_find_best_placement(const nv_skyline_bin_t* bin, const nv_skyline_rect_t* rect, size_t* best_x, size_t* best_y);
extern void   nv_skyline_bin_place_rect(nv_skyline_bin_t* bin, const nv_skyline_rect_t* rect, size_t x, size_t y);
extern void   nv_skyline_bin_pack_rects(nv_skyline_bin_t* bin, nv_skyline_rect_t* rects, size_t nrects);
extern void   nv_skyline_bin_resize(nv_skyline_bin_t* bin, size_t new_w, size_t new_h);

NOVA_HEADER_END

#endif //__NOVA_RECT_PACK_H__
