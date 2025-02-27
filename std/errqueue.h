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
#  define NV_ERROR_LENGTH 256
#endif

typedef char nv_error_t[NV_ERROR_LENGTH];

typedef struct nv_error_queue_t
{
  int        m_front;
  int        m_back;
  nv_error_t m_errors[NV_MAX_ERRORS];
} nv_error_queue_t;

void        nv_error_queue_init(nv_error_queue_t* dst);
void        nv_error_queue_destroy(nv_error_queue_t* dst);
char*       nv_error_queue_push(nv_error_queue_t* queue);
const char* nv_error_queue_pop(nv_error_queue_t* queue);

inline void
nv_error_queue_init(nv_error_queue_t* dst)
{
  dst->m_front = -1;
  dst->m_back  = -1;
}

inline void
nv_error_queue_destroy(nv_error_queue_t* dst)
{
  (void)dst;
}

inline char*
nv_error_queue_push(nv_error_queue_t* queue)
{
  if ((queue->m_back + 1) % NV_MAX_ERRORS == queue->m_front)
  {
    const char* overwritten_error = nv_error_queue_pop(queue);
    if (overwritten_error)
    {
      nv_printf("queue full: poppd error '%s'\n", overwritten_error);
    }
  }

  if (queue->m_front == -1)
  {
    queue->m_front = 0;
  }
  queue->m_back = (queue->m_back + 1) % NV_MAX_ERRORS;

  char* ret = queue->m_errors[queue->m_back];
  nv_memset(ret, 0, NV_ERROR_LENGTH);
  return ret;
}

inline const char*
nv_error_queue_pop(nv_error_queue_t* queue)
{
  if (queue->m_front == -1)
  {
    return NULL;
  }

  const char* value = queue->m_errors[queue->m_front];

  if (queue->m_front == queue->m_back)
  {
    queue->m_front = queue->m_back = -1;
  }
  else
  {
    queue->m_front = (queue->m_front + 1) % NV_MAX_ERRORS;
  }

  return value; // return the popped value
}

NOVA_HEADER_END

#endif //__NOVA_ERROR_QUEUE_H__
