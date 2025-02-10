#ifndef __NOVA_ASYNC_H__
#define __NOVA_ASYNC_H__

#include "stdafx.h"
#include <pthread.h>

#warning "UNFINISHED"

NOVA_HEADER_START;

typedef struct nv_async_task_t nv_async_task_t;

/**
 * @brief Return whatever you want the result of the task to be
 */
typedef void* (*nv_async_task_fn)(void* user_data);

struct nv_async_task_t
{
  unsigned         canary; // = 0xDEADBEEF
  pthread_t        thread;
  nv_async_task_fn func;
  void*            user_data; // Argument to function fn
  void*            result;
  bool             _completed; // MUST NOT BE ACCESSED DIRECTLY
  pthread_mutex_t  lock;
};

/**
 *@brief launch a task (launch an asynchronous function) with parameter user_data
 *@param task an object (typically on the stack) which hosts the task info
 *@param func the function to be called asynchronously
 *@param user_data a parameter passed to the function
 *@return 0 on success and -1 on errors
 */
extern int nv_async_task_launch(nv_async_task_t* NV_RESTRICT task, nv_async_task_fn func, void* NV_RESTRICT user_data);

/**
 *@brief Destroy a task that has been launched. The task MUST have completed
 */
extern void nv_async_task_destroy(nv_async_task_t* task);

extern bool nv_async_is_task_complete(nv_async_task_t* task);

NOVA_HEADER_END;

#endif //__NOVA_ASYNC_H__