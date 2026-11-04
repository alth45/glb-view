#ifndef GLTF_MODEL_H
#define GLTF_MODEL_H

#include "gl/gl_loader.h"
#include <cglm/cglm.h>
#include <stddef.h>

typedef struct {
  GLuint vao, vbo, ebo;
  GLsizei index_count;
  int material_index;
} Mesh;

typedef struct {
  vec4 base_color;
  float metallic, roughness;
  vec3 emissive;
  float normal_scale;
  float occlusion_str;
  int alpha_mode;
  float alpha_cutoff;
  int uv_albedo, uv_mr, uv_normal, uv_occlusion, uv_emissive;
  GLuint albedo_tex, mr_tex, normal_tex, occlusion_tex, emissive_tex;
} Material;

typedef struct {
  Mesh *meshes; size_t mesh_count;
  Material *materials; size_t material_count;
  vec3 center; float radius;
} Model;

int  model_load(Model *model, const char *path);
void model_free(Model *model);

#endif