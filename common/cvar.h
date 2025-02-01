#ifndef __CVARS_H__
#define __CVARS_H__

#include "stdafx.h"
#include "string.h"

NOVA_HEADER_START;

// do I wanna go the cvar way?
// Personally, that's more suited to C++ so... no
// Main reason is dynamic initialization and access
// this was a test, don't forget to remove this
// oh you'll absolutely not remove this will you?

// oh correction, cvar's are absolutely doable in C
// but I am unsure of their usefulness...

typedef struct cvar cvar;

typedef void (*cvar_voidfn)(cvar *var);

typedef enum cvar_tp { CVAR_TP_INT = 0, CVAR_TP_FLOAT = 1, CVAR_TP_STR = 2, CVAR_TP_UNKNOWN = 0x7FFFFFFF } cvar_tp;

typedef union cvar_value {
  long i;
  double d;
  const char *s;
} cvar_value;

struct cvar {
  char name[64];
  cvar_voidfn write_fn;
  cvar_tp tp;
  cvar_value val;
};

extern cvar *g_vars;
extern int g_nvars;

inline int __cvar_compare(const void *a, const void *b) {
  return ((cvar *)a)->val.d - ((cvar *)b)->val.d;
}

static inline void cvarinit(const cvar *src) {
  cvar var = {.write_fn = src->write_fn, .tp = src->tp, .val = src->val};
  nv_strncpy(var.name, src->name, 64);

  if (!g_nvars || !g_vars) {
    g_vars = (cvar *)malloc(sizeof(cvar));
  } else {
    nv_assert(g_nvars > 0);
    g_vars = (cvar *)realloc(g_vars, g_nvars * sizeof(cvar));
  }
  g_vars[g_nvars] = var;
  g_nvars++;
}

static inline cvar_value cvarread(const char *name) {
  for (int i = 0; i < g_nvars; i++) {
    if (nv_strncmp(name, g_vars[i].name, 63) == 0) {
      return g_vars[i].val;
    }
  }
  nv_assert(0);
  return (cvar_value){-1};
}

static inline void cvarset(const char *name, const cvar_value value) {
  for (int i = 0; i < g_nvars; i++) {
    if (nv_strncmp(name, g_vars[i].name, 63) == 0) {
      cvar *var = &g_vars[i];
      var->val  = value;
      if (var->write_fn) {
        var->write_fn(var);
      }
      break;
    }
  }
}

NOVA_HEADER_END;

#endif //__CVARS_H__