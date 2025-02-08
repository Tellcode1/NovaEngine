#ifndef __NOVA_ERR_H__
#define __NOVA_ERR_H__

#include "../std/stdafx.h"

NOVA_HEADER_START;

typedef enum lerrc {
  NOVA_ERR_UNKOWN = -1,

  NOVA_ERR_NONE = 0,

  // An invalid parameter has been passed to a function
  NOVA_ERR_INVALID_PARAM,

  // Function returned invalid 
  NOVA_ERR_INVALID_RET,

  // Internal state error, most likely not the user's (developer's) fault
  NOVA_ERR_INTERNAL,

  // A memory allocation has failed. The OS is likely out of memory
  NOVA_ERR_MALLOC,

  // memory has been written to at invalid positions.
  // a buffer overflow basically
  NOVA_ERR_INVALID_MEMORY,
} lerrc;

NOVA_HEADER_END;

#endif //__NOVA_ERR_H__