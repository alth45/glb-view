#include "model/gltf_model.h"
#include "core/log.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "stb_image.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef struct {
  vec3 position;
  vec3 normal;
  vec2 texcoord;
  vec2 texcoord1; /* TEXCOORD_1 (0 kalau tidak ada) */
} Vertex;

/* ------------------------------------------------------------------ */
/*  Util                                                              */
/* ------------------------------------------------------------------ */
static void get_base_dir(const char *path, char *out, size_t out_size) {
  const char *slash = strrchr(path, '/');
  const char *bslash = strrchr(path, '\\');
  const char *p = slash;
  if (!p || (bslash && bslash > p))
    p = bslash;

  if (p) {
    size_t n = (size_t)(p - path) + 1;
    if (n >= out_size)
      n = out_size - 1;
    memcpy(out, path, n);
    out[n] = '\0';
  } else {
    out[0] = '\0';
  }
}

/* ------------------------------------------------------------------ */
/*  Texture                                                           */
/* ------------------------------------------------------------------ */
static GLuint load_texture_from_image(const cgltf_image *image,
                                      const char *base_dir, int srgb) {
  int w = 0, h = 0, comp = 0;
  stbi_uc *pixels = NULL;

  if (image->buffer_view) {
    /* GLB: gambar tertanam di buffer */
    const unsigned char *data =
        (const unsigned char *)image->buffer_view->buffer->data +
        image->buffer_view->offset;
    pixels = stbi_load_from_memory(data, (int)image->buffer_view->size, &w, &h,
                                   &comp, 4);
  } else if (image->uri && strncmp(image->uri, "data:", 5) != 0) {
    /* .gltf dengan file gambar eksternal */
    char full[2048];
    snprintf(full, sizeof(full), "%s%s", base_dir, image->uri);
    pixels = stbi_load(full, &w, &h, &comp, 4);
  }

  if (!pixels)
    return 0;

  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  /* Albedo & emissive dikode sRGB di glTF -> upload sebagai sRGB agar
   * sampling otomatis menghasilkan warna linear (linier workflow). */
  glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, w, h, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D, 0);

  stbi_image_free(pixels);
  return tex;
}

/* Muat tekstur dari tampilan texture_view; return 0 kalau kosong. */
static GLuint load_view(const cgltf_texture_view *view, const char *base_dir,
                        int srgb) {
  if (!view || !view->texture || !view->texture->image)
    return 0;
  return load_texture_from_image(view->texture->image, base_dir, srgb);
}

static void build_material(Material *mat, const cgltf_material *src,
                           const char *base_dir) {
  memset(mat, 0, sizeof(*mat));

  /* Defaults glTF */
  mat->base_color[0] = 1.0f;
  mat->base_color[1] = 1.0f;
  mat->base_color[2] = 1.0f;
  mat->base_color[3] = 1.0f;
  mat->metallic = 1.0f;
  mat->roughness = 1.0f;
  mat->normal_scale = 1.0f;
  mat->occlusion_str = 1.0f;
  mat->alpha_mode = 0;
  mat->alpha_cutoff = 0.5f;

  if (!src)
    return;

  if (src->has_pbr_metallic_roughness) {
    const cgltf_pbr_metallic_roughness *pbr = &src->pbr_metallic_roughness;
    mat->base_color[0] = pbr->base_color_factor[0];
    mat->base_color[1] = pbr->base_color_factor[1];
    mat->base_color[2] = pbr->base_color_factor[2];
    mat->base_color[3] = pbr->base_color_factor[3];
    mat->metallic = pbr->metallic_factor;
    mat->roughness = pbr->roughness_factor;

    mat->albedo_tex =
        load_view(&pbr->base_color_texture, base_dir, 1 /* sRGB */);
    mat->uv_albedo = (int)pbr->base_color_texture.texcoord;

    mat->mr_tex = load_view(&pbr->metallic_roughness_texture, base_dir,
                            0 /* data linear */);
    mat->uv_mr = (int)pbr->metallic_roughness_texture.texcoord;
  }

  if (src->normal_texture.texture) {
    mat->normal_tex = load_view(&src->normal_texture, base_dir, 0);
    mat->uv_normal = (int)src->normal_texture.texcoord;
    if (src->normal_texture.has_transform)
      mat->normal_scale = 1.0f; /* KHR_texture_transform tidak didukung */
    if (src->normal_texture.scale > 0.0f)
      mat->normal_scale = src->normal_texture.scale;
  }

  if (src->occlusion_texture.texture) {
    mat->occlusion_tex = load_view(&src->occlusion_texture, base_dir, 0);
    mat->uv_occlusion = (int)src->occlusion_texture.texcoord;
    if (src->occlusion_texture.scale > 0.0f)
      mat->occlusion_str = src->occlusion_texture.scale;
  }

  if (src->emissive_texture.texture) {
    mat->emissive_tex = load_view(&src->emissive_texture, base_dir,
                                  1 /* sRGB */);
    mat->uv_emissive = (int)src->emissive_texture.texcoord;
  }
  mat->emissive[0] = src->emissive_factor[0];
  mat->emissive[1] = src->emissive_factor[1];
  mat->emissive[2] = src->emissive_factor[2];

  mat->alpha_mode = (int)src->alpha_mode;
  if (mat->alpha_mode == 1 && src->alpha_cutoff > 0.0f)
    mat->alpha_cutoff = src->alpha_cutoff;
}

/* ------------------------------------------------------------------ */
/*  Primitive -> GPU                                                  */
/* ------------------------------------------------------------------ */
static void build_primitive(const cgltf_data *data, const cgltf_primitive *prim,
                            mat4 world, Mesh *out, vec3 bb_min, vec3 bb_max) {
  out->vao = out->vbo = out->ebo = 0;
  out->index_count = 0;
  out->material_index = -1;

  const cgltf_accessor *pos = NULL, *norm = NULL, *uv = NULL, *uv1 = NULL;
  for (cgltf_size i = 0; i < prim->attributes_count; i++) {
    const cgltf_attribute *a = &prim->attributes[i];
    if (a->type == cgltf_attribute_type_position)
      pos = a->data;
    else if (a->type == cgltf_attribute_type_normal)
      norm = a->data;
    else if (a->type == cgltf_attribute_type_texcoord && a->index == 0)
      uv = a->data;
    else if (a->type == cgltf_attribute_type_texcoord && a->index == 1)
      uv1 = a->data;
  }
  if (!pos)
    return;

  const cgltf_size vcount = pos->count;
  Vertex *verts = (Vertex *)calloc(vcount, sizeof(Vertex));
  if (!verts)
    return;

  /* Normal matrix = transpose(inverse(world)) */
  mat4 m_inv;
  glm_mat4_inv(world, m_inv);
  glm_mat4_transpose(m_inv);
  mat3 nmat;
  glm_mat4_pick3(m_inv, nmat);

  for (cgltf_size i = 0; i < vcount; i++) {
    Vertex *v = &verts[i];

    cgltf_accessor_read_float(pos, i, v->position, 3);

    if (norm)
      cgltf_accessor_read_float(norm, i, v->normal, 3);
    else {
      v->normal[0] = 0;
      v->normal[1] = 1;
      v->normal[2] = 0;
    }

    if (uv)
      cgltf_accessor_read_float(uv, i, v->texcoord, 2);
    if (uv1)
      cgltf_accessor_read_float(uv1, i, v->texcoord1, 2);

    /* Posisi -> world space (dibake langsung agar model matrix = identity) */
    vec4 p = {v->position[0], v->position[1], v->position[2], 1.0f};
    vec4 wp;
    glm_mat4_mulv(world, p, wp);
    v->position[0] = wp[0];
    v->position[1] = wp[1];
    v->position[2] = wp[2];

    /* Normal -> world space */
    vec3 n = {v->normal[0], v->normal[1], v->normal[2]};
    vec3 wn;
    glm_mat3_mulv(nmat, n, wn);
    glm_vec3_normalize(wn);
    glm_vec3_copy(wn, v->normal);

    /* Bounding box */
    for (int k = 0; k < 3; k++) {
      if (v->position[k] < bb_min[k])
        bb_min[k] = v->position[k];
      if (v->position[k] > bb_max[k])
        bb_max[k] = v->position[k];
    }
  }

  /* Indices */
  const cgltf_size icount = prim->indices ? prim->indices->count : vcount;
  GLuint *indices = (GLuint *)malloc(sizeof(GLuint) * icount);
  if (!indices) {
    free(verts);
    return;
  }

  if (prim->indices) {
    for (cgltf_size i = 0; i < icount; i++)
      indices[i] = (GLuint)cgltf_accessor_read_index(prim->indices, i);
  } else {
    for (cgltf_size i = 0; i < icount; i++)
      indices[i] = (GLuint)i;
  }

  glGenVertexArrays(1, &out->vao);
  glBindVertexArray(out->vao);

  glGenBuffers(1, &out->vbo);
  glBindBuffer(GL_ARRAY_BUFFER, out->vbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(Vertex) * vcount), verts,
               GL_STATIC_DRAW);

  glGenBuffers(1, &out->ebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, out->ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(sizeof(GLuint) * icount),
               indices, GL_STATIC_DRAW);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void *)offsetof(Vertex, position));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void *)offsetof(Vertex, normal));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void *)offsetof(Vertex, texcoord));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void *)offsetof(Vertex, texcoord1));

  glBindVertexArray(0);

  out->index_count = (GLsizei)icount;

  if (prim->material && data->materials)
    out->material_index = (int)(prim->material - data->materials);

  free(verts);
  free(indices);
}

/* ------------------------------------------------------------------ */
/*  Traversal node                                                    */
/* ------------------------------------------------------------------ */
typedef struct {
  Mesh *meshes;
  size_t count;
  size_t capacity;
  vec3 bb_min;
  vec3 bb_max;
  const cgltf_data *data;
} BuildCtx;

static void process_node(BuildCtx *ctx, const cgltf_node *node, mat4 parent) {
  cgltf_float local_f[16];
  cgltf_node_transform_local(node, local_f);

  mat4 local, world;
  /* local_f adalah float[16] linear; glm_mat4_copy butuh float (*)[4]. */
  memcpy(local, local_f, sizeof(local));
  glm_mat4_mul(parent, local, world);

  if (node->mesh) {
    for (cgltf_size i = 0; i < node->mesh->primitives_count; i++) {
      if (ctx->count >= ctx->capacity)
        break;
      build_primitive(ctx->data, &node->mesh->primitives[i], world,
                      &ctx->meshes[ctx->count], ctx->bb_min, ctx->bb_max);
      ctx->count++;
    }
  }

  for (cgltf_size i = 0; i < node->children_count; i++)
    process_node(ctx, node->children[i], world);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */
int model_load(Model *model, const char *path) {
  memset(model, 0, sizeof(*model));

  cgltf_options options = {0};
  cgltf_data *data = NULL;

  if (cgltf_parse_file(&options, path, &data) != cgltf_result_success) {
    log_error("[gltf] cgltf_parse_file gagal: %s\n", path);
    return 0;
  }
  if (cgltf_load_buffers(&options, data, path) != cgltf_result_success) {
    log_error("[gltf] cgltf_load_buffers gagal\n");
    cgltf_free(data);
    return 0;
  }

  char base_dir[1024];
  get_base_dir(path, base_dir, sizeof(base_dir));

  /* Material */
  model->material_count = data->materials_count;
  model->materials = (Material *)calloc(
      data->materials_count ? data->materials_count : 1, sizeof(Material));
  for (cgltf_size i = 0; i < data->materials_count; i++)
    build_material(&model->materials[i], &data->materials[i], base_dir);

  /* Hitung total primitive */
  size_t total = 0;
  for (cgltf_size i = 0; i < data->nodes_count; i++)
    if (data->nodes[i].mesh)
      total += data->nodes[i].mesh->primitives_count;

  model->meshes = (Mesh *)calloc(total ? total : 1, sizeof(Mesh));

  BuildCtx ctx = {
      .meshes = model->meshes,
      .count = 0,
      .capacity = total,
      .bb_min = {INFINITY, INFINITY, INFINITY},
      .bb_max = {-INFINITY, -INFINITY, -INFINITY},
      .data = data,
  };

  mat4 identity;
  glm_mat4_identity(identity);

  if (data->scenes_count > 0) {
    for (cgltf_size s = 0; s < data->scenes_count; s++) {
      const cgltf_scene *scene = &data->scenes[s];
      for (cgltf_size n = 0; n < scene->nodes_count; n++)
        process_node(&ctx, scene->nodes[n], identity);
    }
  } else {
    /* Fallback: root nodes */
    for (cgltf_size i = 0; i < data->nodes_count; i++)
      if (!data->nodes[i].parent)
        process_node(&ctx, &data->nodes[i], identity);
  }

  model->mesh_count = ctx.count;

  /* Bounding sphere */
  if (isinf(ctx.bb_min[0])) {
    glm_vec3_zero(model->center);
    model->radius = 1.0f;
  } else {
    glm_vec3_add(ctx.bb_min, ctx.bb_max, model->center);
    glm_vec3_scale(model->center, 0.5f, model->center);

    vec3 ext;
    glm_vec3_sub(ctx.bb_max, ctx.bb_min, ext);
    model->radius =
        0.5f * sqrtf(ext[0] * ext[0] + ext[1] * ext[1] + ext[2] * ext[2]);
    if (model->radius < 0.001f)
      model->radius = 1.0f;
  }

  cgltf_free(data);
  return 1;
}

void model_free(Model *model) {
  if (!model)
    return;

  for (size_t i = 0; i < model->mesh_count; i++) {
    Mesh *m = &model->meshes[i];
    if (m->ebo)
      glDeleteBuffers(1, &m->ebo);
    if (m->vbo)
      glDeleteBuffers(1, &m->vbo);
    if (m->vao)
      glDeleteVertexArrays(1, &m->vao);
  }
  for (size_t i = 0; i < model->material_count; i++) {
    Material *m = &model->materials[i];
    if (m->albedo_tex)
      glDeleteTextures(1, &m->albedo_tex);
    if (m->mr_tex)
      glDeleteTextures(1, &m->mr_tex);
    if (m->normal_tex)
      glDeleteTextures(1, &m->normal_tex);
    if (m->occlusion_tex)
      glDeleteTextures(1, &m->occlusion_tex);
    if (m->emissive_tex)
      glDeleteTextures(1, &m->emissive_tex);
  }

  free(model->meshes);
  free(model->materials);
  memset(model, 0, sizeof(*model));
}