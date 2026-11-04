#include "app/config.h"
#include "core/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void trim(char *s) {
  size_t n = strlen(s);
  while (n > 0 && (s[n-1]=='\n'||s[n-1]=='\r'||s[n-1]==' '||s[n-1]=='\t'))
    s[--n] = 0;
}

void config_defaults(ViewerConfig *c) {
  memset(c, 0, sizeof(*c));
  c->exposure    = 1.0f;
  c->use_ibl     = 1;
  c->vignette    = 0.35f;
  c->sun_dir[0]  = 0.5f;
  c->sun_dir[1]  = 1.0f;
  c->sun_dir[2]  = 0.3f;
  glm_vec3_normalize(c->sun_dir);
  c->sun_color[0] = 3.0f;
  c->sun_color[1] = 2.85f;
  c->sun_color[2] = 2.55f;
}

int config_load(ViewerConfig *c, const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) return 0;
  char line[256];
  while (fgets(line, sizeof(line), f)) {
    trim(line);
    if (line[0]=='#' || line[0]==';' || line[0]=='[' || line[0]==0) continue;
    char *eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    char *k = line, *v = eq + 1;
    while (*k==' '||*k=='\t') k++;
    while (*v==' '||*v=='\t') v++;
    trim(k);
    if      (strcmp(k,"exposure")    == 0) c->exposure    = (float)atof(v);
    else if (strcmp(k,"wireframe")   == 0) c->wireframe   = atoi(v);
    else if (strcmp(k,"auto_rotate") == 0) c->auto_rotate = atoi(v);
    else if (strcmp(k,"use_ibl")     == 0) c->use_ibl     = atoi(v);
    else if (strcmp(k,"debug_mode")  == 0) c->debug_mode  = atoi(v);
    else if (strcmp(k,"vignette")    == 0) c->vignette    = (float)atof(v);
    else if (strcmp(k,"sun_dir_x")   == 0) c->sun_dir[0]  = (float)atof(v);
    else if (strcmp(k,"sun_dir_y")   == 0) c->sun_dir[1]  = (float)atof(v);
    else if (strcmp(k,"sun_dir_z")   == 0) c->sun_dir[2]  = (float)atof(v);
    else if (strcmp(k,"sun_color_r") == 0) c->sun_color[0]= (float)atof(v);
    else if (strcmp(k,"sun_color_g") == 0) c->sun_color[1]= (float)atof(v);
    else if (strcmp(k,"sun_color_b") == 0) c->sun_color[2]= (float)atof(v);
  }
  fclose(f);
  glm_vec3_normalize(c->sun_dir);
  return 1;
}

int config_save(const ViewerConfig *c, const char *path) {
  FILE *f = fopen(path, "w");
  if (!f) return 0;
  fprintf(f, "# GLB Viewer config\n");
  fprintf(f, "exposure = %.4f\n",     c->exposure);
  fprintf(f, "wireframe = %d\n",      c->wireframe);
  fprintf(f, "auto_rotate = %d\n",    c->auto_rotate);
  fprintf(f, "use_ibl = %d\n",        c->use_ibl);
  fprintf(f, "debug_mode = %d\n",     c->debug_mode);
  fprintf(f, "vignette = %.4f\n",     c->vignette);
  fprintf(f, "sun_dir_x = %.4f\n",    c->sun_dir[0]);
  fprintf(f, "sun_dir_y = %.4f\n",    c->sun_dir[1]);
  fprintf(f, "sun_dir_z = %.4f\n",    c->sun_dir[2]);
  fprintf(f, "sun_color_r = %.4f\n",  c->sun_color[0]);
  fprintf(f, "sun_color_g = %.4f\n",  c->sun_color[1]);
  fprintf(f, "sun_color_b = %.4f\n",  c->sun_color[2]);
  fclose(f);
  return 1;
}