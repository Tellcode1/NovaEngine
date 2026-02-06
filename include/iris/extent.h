#ifndef IRIS_EXTENT_H
#define IRIS_EXTENT_H

#include <stddef.h>

typedef struct nv_extent2
{
  size_t width;
  size_t height;
} nv_extent2;

typedef struct nv_extent3
{
  size_t width;
  size_t height;
  size_t depth;
} nv_extent3;

#endif // IRIS_EXTENT_H
