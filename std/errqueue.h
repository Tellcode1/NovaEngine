#ifndef __NOVA_ERROR_QUEUE_H__
#define __NOVA_ERROR_QUEUE_H__

#include "print.h"
#include "stdafx.h"
#include "string.h"

NOVA_HEADER_START

#ifndef NV_MAX_ERRORS
#  define NV_MAX_ERRORS 16
#endif

#ifndef NV_ERROR_LENGTH
#  define NV_ERROR_LENGTH 128
#endif

typedef char nv_error_t[NV_ERROR_LENGTH];

typedef struct nv_error_queue_t
{
  int        front;
  int        back;
  nv_error_t errors[NV_MAX_ERRORS];
} nv_error_queue_t;

void        nv_error_queue_init(nv_error_queue_t* dst);
void        nv_error_queue_destroy(nv_error_queue_t* dst);
char*       nv_error_queue_push(nv_error_queue_t* queue);
const char* nv_error_queue_pop(nv_error_queue_t* queue);

inline void
nv_error_queue_init(nv_error_queue_t* dst)
{
  dst->front = -1;
  dst->back  = -1;
  nv_memset(dst->errors, 0, sizeof(dst->errors));
}

inline void
nv_error_queue_destroy(nv_error_queue_t* dst)
{
}

inline char*
nv_error_queue_push(nv_error_queue_t* queue)
{
  if ((queue->back + 1) % NV_MAX_ERRORS == queue->front)
  {
    const char* overwritten_error = nv_error_queue_pop(queue);
    if (overwritten_error) { nv_log_error("queue full: poppd '%s'\n", overwritten_error); }
  }

  if (queue->front == -1) { queue->front = 0; }
  queue->back = (queue->back + 1) % NV_MAX_ERRORS;

  nv_memset(queue->errors[queue->back], 0, NV_ERROR_LENGTH);
  return queue->errors[queue->back];
}

inline const char*
nv_error_queue_pop(nv_error_queue_t* queue)
{
  if (queue->front == -1) { return NULL; }

  const char* value = queue->errors[queue->front];

  if (queue->front == queue->back) { queue->front = queue->back = -1; }
  else { queue->front = (queue->front + 1) % NV_MAX_ERRORS; }

  return value; // Return the popped value
}

NOVA_HEADER_END

#endif //__NOVA_ERROR_QUEUE_H__