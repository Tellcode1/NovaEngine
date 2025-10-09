#ifndef IRIS_RESOURCE_DESTRUCT_QUEUE_H
#define IRIS_RESOURCE_DESTRUCT_QUEUE_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "../../external/volk/volk.h"
#include <stddef.h>

  typedef enum iris_resource_type
  {
    IRIS_RESOURCE_MEMORY  = 0,
    IRIS_RESOURCE_BUFFER  = 1,
    IRIS_RESOURCE_TEXTURE = 2,
    IRIS_RESOURCE_SHADER  = 3,
  } iris_resource_type;

  typedef union iris_resource_handle
  {
    VkDeviceMemory memory;
    VkBuffer       buffer;
    VkImage        texture;
    VkShaderModule shader;
  } iris_resource_handle;

  typedef struct iris_resource
  {
    iris_resource_type   type;
    iris_resource_handle handle;
  } iris_resource_t;

  typedef struct iris_destruct_queue
  {
    iris_resource_t* queue;
    size_t           queue_count;
    size_t           queue_capacity;
  } iris_destruct_queue_t;

  // Function declerations are in driver.h

#ifdef __cplusplus
}
#endif

#endif // IRIS_RESOURCE_DESTRUCT_QUEUE_H
