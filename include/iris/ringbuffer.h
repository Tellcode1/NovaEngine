#ifndef IRIS_RING_BUFFER_HPP
#define IRIS_RING_BUFFER_HPP

#include "buffer.h"
#include "types.h"
#include "vkstdafx.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct iris_ring_buffer
  {
    iris_buffer_t backing;
    size_t        frames_in_flight;
    size_t        frame_index;
    size_t        slice_size;
  } iris_ring_buffer_t;

  static inline nv_error
  iris_ring_buffer_init(
      struct iris_driver* driver, iris_size_t slice_size, size_t alignment, iris_buffer_extra_create_info_t* extra_info, iris_buffer_flags flags, iris_ring_buffer_t* dst)
  {
    dst->slice_size       = align_up_size(slice_size, alignment);
    dst->frames_in_flight = extra_info->multibuffering_frames;
    dst->frame_index      = 0;

    size_t total_size = slice_size * extra_info->multibuffering_frames;

    return iris_buffer_init(driver, total_size, alignment, extra_info, flags, &dst->backing);
  }

  static inline void
  iris_ring_buffer_destroy(iris_ring_buffer_t* buffer)
  {
    iris_buffer_destroy(&buffer->backing);
  }

  static inline nv_error
  iris_ring_buffer_resize(iris_ring_buffer_t* buffer, size_t new_slice_size, size_t new_alignment, size_t new_frame_count, bool copy_old_data)
  {
    buffer->slice_size       = align_up_size(new_slice_size, new_alignment);
    buffer->frames_in_flight = new_frame_count;
    buffer->frame_index      = 0; // Not setting it to 0 sometimes causes the value to be out of bounds for the next frame, caushing a SEGV

    size_t total_size = buffer->slice_size * buffer->frames_in_flight;

    return iris_buffer_resize(&buffer->backing, total_size, new_alignment, copy_old_data);
  }

  static inline void
  iris_ring_buffer_next(iris_ring_buffer_t* rb)
  {
    rb->frame_index = (rb->frame_index + 1) % rb->frames_in_flight;
  }

  static inline size_t
  iris_ring_buffer_offset(const iris_ring_buffer_t* rb)
  {
    return rb->frame_index * rb->slice_size;
  }

#ifdef __cplusplus
}
#endif

#endif // IRIS_RING_BUFFER_HPP
