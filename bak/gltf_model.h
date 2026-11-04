#ifndef GLTF_MODEL_H
#define GLTF_MODEL_H

#include "gl_loader.h"
#include <cglm/cglm.h>
#include <stddef.h>

typedef struct {
  GLuint vao;
  GLuint vbo;
  GLuint ebo;
  GLsizei index_count;
  int material_index; /* -1 kalau tidak ada material */
} Mesh;

typedef struct {
  /* PBR metallic-roughness */
  vec4 base_color;      /* baseColorFactor (linear) */
  float metallic;
  float roughness;
  vec3 emissive;        /* emissiveFactor (linear) */
  float normal_scale;   /* normalTexture.scale */
  float occlusion_str;  /* occlusionTexture.strength */

  /* Alpha: 0=OPAQUE, 1=MASK, 2=BLEND */
  int alpha_mode;
  float alpha_cutoff;

  /* Indeks set UV (0 = TEXCOORD_0, 1 = TEXCOORD_1) per tekstur */
  int uv_albedo;
  int uv_mr;
  int uv_normal;
  int uv_occlusion;
  int uv_emissive;

  /* Tekstur (0 kalau tidak ada) */
  GLuint albedo_tex;
  GLuint mr_tex;        /* metallic-roughness (G=roughness, B=metallic) */
  GLuint normal_tex;
  GLuint occlusion_tex;
  GLuint emissive_tex;
} Material;

typedef struct {
  Mesh *meshes;
  size_t mesh_count;
  Material *materials;
  size_t material_count;

  /* Bounding sphere untuk auto-fit kamera */
  vec3 center;
  float radius;
} Model;

int model_load(Model *model, const char *path);
void model_free(Model *model);

#endif