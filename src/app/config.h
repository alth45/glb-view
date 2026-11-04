#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <cglm/cglm.h>

typedef struct {
  float exposure;
  int   wireframe;
  int   auto_rotate;
  int   use_ibl;
  int   debug_mode;
  float vignette;
  vec3  sun_dir;
  vec3  sun_color;
} ViewerConfig;

void config_defaults(ViewerConfig *c);
int  config_load(ViewerConfig *c, const char *path);
int  config_save(const ViewerConfig *c, const char *path);

#endif