#include "gl_loader.h"
#include <stdio.h>

#define GL_DEF(ret, name, args) PFN_##name name = NULL;
GL_FUNCTIONS(GL_DEF)
#undef GL_DEF

int gl_load(void *(*get_proc)(const char *)) {
  int ok = 1;
#define GL_LOAD(ret, name, args)                                               \
  name = (PFN_##name)get_proc(#name);                                          \
  if (!name) {                                                                 \
    fprintf(stderr, "[gl] missing: %s\n", #name);                              \
    ok = 0;                                                                    \
  }
  GL_FUNCTIONS(GL_LOAD)
#undef GL_LOAD
  return ok;
}