#ifndef IRIS_MESH_H
#define IRIS_MESH_H

#include "../iris/buffer.h"

typedef struct iris_mesh iris_mesh_t;

struct iris_mesh
{
  iris_buffer_t  vertex_buffer;
  iris_buffer_t* index_buffer;        /**<  An optional dedicated index buffer */
  iris_size_t    index_buffer_offset; /**<  If the index buffer is dedicated, must be 0. */
};

#endif // IRIS_MESH_H
