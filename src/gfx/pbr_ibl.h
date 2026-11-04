#ifndef PBR_IBL_H
#define PBR_IBL_H

#include "gl/gl_loader.h"

typedef struct {
  GLuint env_cubemap;
  GLuint irradiance;
  GLuint prefilter;
  GLuint brdf_lut;
  int    ok;
} IblContext;

float *ibl_env_procedural(int *out_w, int *out_h);
float *ibl_env_load_hdr(const char *path, int *out_w, int *out_h);
int    ibl_build(IblContext *ibl, const float *hdr, int w, int h);
void   ibl_draw_background(const IblContext *ibl, const float inv_view_proj[16],
                           const float cam_pos[3], float exposure);
void   ibl_free(IblContext *ibl);

#endif