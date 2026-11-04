#include "gfx/pbr_ibl.h"
#include "gfx/shader.h"
#include "core/log.h"

#include <cglm/cglm.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_image.h"



#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define ENV_SIZE 512   /* resolusi base env cubemap */
#define IRR_SIZE 32    /* irradiance (diffuse) */
#define PREF_SIZE 256  /* prefilter specular base */
#define PREF_MIPS 5    /* 256,128,64,32,16 */
#define LUT_SIZE 512   /* BRDF LUT */

/* ================================================================== */
/*  Shader bake (GLSL 330 core)                                       */
/* ================================================================== */

/* Utility bersama: Hammersley tanpa uint (kompatibel GLSL 3.30),
 * importance sampling GGX, distribusi GGX, dan Schlick-GGX. */
static const char *GLSL_COMMON =
    "#define PI2 3.14159265359\n"
    "float radicalInverseVdC(int n) {\n"
    "  float f = 1.0, r = 0.0;\n"
    "  for (int i = 0; i < 16; i++) {\n"
    "    f *= 0.5;\n"
    "    r += f * float(n - 2 * (n / 2));\n"
    "    n /= 2;\n"
    "  }\n"
    "  return r;\n"
    "}\n"
    "vec2 hammersley(int i, int N) {\n"
    "  return vec2(float(i) / float(N), radicalInverseVdC(i));\n"
    "}\n"
    "vec3 importanceSampleGGX(vec2 Xi, vec3 N, float roughness) {\n"
    "  float a = roughness * roughness;\n"
    "  float phi = 2.0 * PI2 * Xi.x;\n"
    "  float cosT = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));\n"
    "  float sinT = sqrt(max(0.0, 1.0 - cosT * cosT));\n"
    "  vec3 H = vec3(cos(phi) * sinT, sin(phi) * sinT, cosT);\n"
    "  vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);\n"
    "  vec3 T = normalize(cross(up, N));\n"
    "  vec3 B = cross(N, T);\n"
    "  return normalize(T * H.x + B * H.y + N * H.z);\n"
    "}\n"
    "float distributionGGX(vec3 N, vec3 H, float roughness) {\n"
    "  float a = roughness * roughness;\n"
    "  float a2 = a * a;\n"
    "  float NdotH = max(dot(N, H), 0.0);\n"
    "  float d = NdotH * NdotH * (a2 - 1.0) + 1.0;\n"
    "  return a2 / (PI2 * d * d + 1e-6);\n"
    "}\n"
    "float geomSchlick(float NdotX, float k) {\n"
    "  return NdotX / (NdotX * (1.0 - k) + k);\n"
    "}\n";

/* Unit cube (36 vertex) — dipakai semua pass bake; arah = posisi. */
static const char *CUBE_VS =
    "#version 330 core\n"
    "layout(location = 0) in vec3 aPos;\n"
    "uniform mat4 uProj;\n"
    "uniform mat4 uView;\n"
    "out vec3 vDir;\n"
    "void main() {\n"
    "  vDir = aPos;\n"
    "  gl_Position = uProj * uView * vec4(aPos, 1.0);\n"
    "}\n";

/* Equirectangular -> cubemap. */
static const char *CONVERT_FS =
    "#version 330 core\n"
    "#define PI2 3.14159265359\n"
    "in vec3 vDir;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D uEquirect;\n"
    "void main() {\n"
    "  vec3 d = normalize(vDir);\n"
    "  vec2 uv = vec2(atan(d.z, d.x) / (2.0 * PI2) + 0.5,\n"
    "                 asin(clamp(d.y, -1.0, 1.0)) / PI2 + 0.5);\n"
    "  FragColor = vec4(texture(uEquirect, uv).rgb, 1.0);\n"
    "}\n";

/* Iradiansi diffuse: kosinus-weighted hemisphere integration. */
static const char *IRRADIANCE_FS =
    "#version 330 core\n"
    "in vec3 vDir;\n"
    "out vec4 FragColor;\n"
    "uniform samplerCube uEnv;\n"
    "@COMMON@"
    "void main() {\n"
    "  vec3 N = normalize(vDir);\n"
    "  vec3 right0 = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);\n"
    "  vec3 right = normalize(cross(right0, N));\n"
    "  vec3 up = cross(N, right);\n"
    "  vec3 sum = vec3(0.0);\n"
    "  const int N_SAMPLES = 1024;\n"
    "  for (int i = 0; i < N_SAMPLES; i++) {\n"
    "    vec2 Xi = hammersley(i, N_SAMPLES);\n"
    "    float phi = 2.0 * PI2 * Xi.x;\n"
    "    float cosT = sqrt(1.0 - Xi.y);\n"
    "    float sinT = sqrt(max(0.0, 1.0 - cosT * cosT));\n"
    "    vec3 L = normalize(right * (cos(phi) * sinT) +\n"
    "                       up * (sin(phi) * sinT) + N * cosT);\n"
    "    sum += textureLod(uEnv, L, 0.0).rgb;\n"
    "  }\n"
    "  FragColor = vec4(PI2 * sum / float(N_SAMPLES), 1.0);\n"
    "}\n";

/* Prefilter specular: importance sampling GGX per roughness/mip,
 * sampling env memakai mip sesuai pdf (Karis). */
static const char *PREFILTER_FS =
    "#version 330 core\n"
    "in vec3 vDir;\n"
    "out vec4 FragColor;\n"
    "uniform samplerCube uEnv;\n"
    "uniform float uRoughness;\n"
    "uniform float uEnvSize;\n"
    "@COMMON@"
    "void main() {\n"
    "  vec3 N = normalize(vDir);\n"
    "  vec3 V = N;\n"
    "  vec3 sum = vec3(0.0);\n"
    "  float total = 0.0;\n"
    "  const int N_SAMPLES = 256;\n"
    "  for (int i = 0; i < N_SAMPLES; i++) {\n"
    "    vec2 Xi = hammersley(i, N_SAMPLES);\n"
    "    vec3 H = importanceSampleGGX(Xi, N, uRoughness);\n"
    "    vec3 L = reflect(-V, H);\n"
    "    float NdotL = max(dot(N, L), 0.0);\n"
    "    if (NdotL > 0.0) {\n"
    "      float NdotH = max(dot(N, H), 0.0);\n"
    "      float HdotV = max(dot(H, V), 0.0);\n"
    "      float D = distributionGGX(N, H, uRoughness);\n"
    "      float pdf = D * NdotH / (4.0 * HdotV) + 1e-4;\n"
    "      float saTexel = 4.0 * PI2 / (6.0 * uEnvSize * uEnvSize);\n"
    "      float saSample = 1.0 / (float(N_SAMPLES) * pdf + 1e-4);\n"
    "      float mip = 0.5 * log2(saSample / saTexel);\n"
    "      sum += textureLod(uEnv, L, mip).rgb * NdotL;\n"
    "      total += NdotL;\n"
    "    }\n"
    "  }\n"
    "  FragColor = vec4(sum / max(total, 1e-4), 1.0);\n"
    "}\n";

/* Fullscreen triangle (tanpa UV attrib — pakai posisi). */
static const char *QUAD_VS =
    "#version 330 core\n"
    "layout(location = 0) in vec2 aPos;\n"
    "out vec2 vUV;\n"
    "void main() {\n"
    "  vUV = aPos * 0.5 + 0.5;\n"
    "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "}\n";

/* Integrasi BRDF (split-sum Karis): output = (A, B) di RG. */
static const char *BRDF_FS =
    "#version 330 core\n"
    "in vec2 vUV;\n"
    "out vec4 FragColor;\n"
    "@COMMON@"
    "void main() {\n"
    "  float NdotV = max(vUV.y, 0.005);\n"
    "  float roughness = max(vUV.x, 0.01);\n"
    "  vec3 N = vec3(0.0, 0.0, 1.0);\n"
    "  vec3 V = vec3(sqrt(max(0.0, 1.0 - NdotV * NdotV)), 0.0, NdotV);\n"
    "  float A = 0.0, B = 0.0;\n"
    "  const int N_SAMPLES = 512;\n"
    "  float k = (roughness * roughness) * 0.5; /* k IBL (a^2/2) */\n"
    "  for (int i = 0; i < N_SAMPLES; i++) {\n"
    "    vec2 Xi = hammersley(i, N_SAMPLES);\n"
    "    vec3 H = importanceSampleGGX(Xi, N, roughness);\n"
    "    vec3 L = reflect(-V, H);\n"
    "    float NdotL = max(L.z, 0.0);\n"
    "    if (NdotL > 0.0) {\n"
    "      float NdotH = max(H.z, 0.0);\n"
    "      float HdotV = max(dot(H, V), 0.0);\n"
    "      float D = distributionGGX(N, H, roughness);\n"
    "      float G = geomSchlick(NdotV, k) * geomSchlick(NdotL, k);\n"
    "      float GVis = (G * HdotV) / (max(NdotH, 1e-4) * NdotV);\n"
    "      float Fc = pow(1.0 - HdotV, 5.0);\n"
    "      A += (1.0 - Fc) * GVis;\n"
    "      B += Fc * GVis;\n"
    "    }\n"
    "  }\n"
    "  A /= float(N_SAMPLES);\n"
    "  B /= float(N_SAMPLES);\n"
    "  FragColor = vec4(A, B, 0.0, 1.0);\n"
    "}\n";

/* Latar belakang: rekonstruksi ray dari inverse view-projection. */
static const char *BG_VS =
    "#version 330 core\n"
    "layout(location = 0) in vec2 aPos;\n"
    "uniform mat4 uInvVP;\n"
    "uniform vec3 uCamPos;\n"
    "out vec3 vDir;\n"
    "void main() {\n"
    "  vec4 far = uInvVP * vec4(aPos, 1.0, 1.0);\n"
    "  vDir = far.xyz / far.w - uCamPos;\n"
    "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "}\n";

static const char *BG_FS =
    "#version 330 core\n"
    "in vec3 vDir;\n"
    "out vec4 FragColor;\n"
    "uniform samplerCube uEnv;\n"
    "uniform float uExposure;\n"
    "vec3 aces(vec3 x) {\n"
    "  const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;\n"
    "  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);\n"
    "}\n"
    "void main() {\n"
    "  vec3 col = textureLod(uEnv, normalize(vDir), 0.0).rgb * uExposure;\n"
    "  col = aces(col);\n"
    "  col = pow(col, vec3(1.0 / 2.2));\n"
    "  FragColor = vec4(col, 1.0);\n"
    "}\n";

/* ================================================================== */
/*  State internal                                                     */
/* ================================================================== */
static struct {
  GLuint prog_convert, prog_irr, prog_pre, prog_brdf, prog_bg;
  GLuint cube_vao, cube_vbo;
  GLuint quad_vao, quad_vbo;
  GLuint fbo;
} G;

/* Orientasi face cubemap (konvensi Khronos: lihat ke arah face, up sesuai). */
static float FACE_DIRS[6][3] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
static float FACE_UPS[6][3] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1},
                               {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};

static const float CUBE_VERTS[108] = {
    /* +X */ 1, -1, -1, 1, 1, -1, 1, 1, 1, 1, -1, -1, 1, 1, 1, 1, -1, 1,
    /* -X */ -1, -1, -1, -1, -1, 1, -1, 1, 1, -1, -1, -1, -1, 1, 1, -1, 1, -1,
    /* +Y */ -1, 1, -1, -1, 1, 1, 1, 1, 1, -1, 1, -1, 1, 1, 1, 1, 1, -1,
    /* -Y */ -1, -1, -1, 1, -1, -1, 1, -1, 1, -1, -1, -1, 1, -1, 1, -1, -1, 1,
    /* +Z */ -1, -1, 1, 1, -1, 1, 1, 1, 1, -1, -1, 1, 1, 1, 1, -1, 1, 1,
    /* -Z */ 1, -1, -1, -1, -1, -1, -1, 1, -1, 1, -1, -1, -1, 1, -1, 1, 1, -1};

static const float QUAD_VERTS[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};

/* Gabung GLSL_COMMON ke sumber yang mengandung penanda @COMMON@. */
static char *glsl_compose(const char *tmpl) {
  const char *mark = strstr(tmpl, "@COMMON@");
  if (!mark) {
    char *copy = (char *)malloc(strlen(tmpl) + 1);
    if (copy)
      memcpy(copy, tmpl, strlen(tmpl) + 1);
    return copy;
  }
  size_t head = (size_t)(mark - tmpl);
  size_t tail = strlen(mark + 8);
  char *out = (char *)malloc(head + strlen(GLSL_COMMON) + tail + 1);
  if (!out)
    return NULL;
  memcpy(out, tmpl, head);
  memcpy(out + head, GLSL_COMMON, strlen(GLSL_COMMON));
  memcpy(out + head + strlen(GLSL_COMMON), mark + 8, tail + 1);
  return out;
}

static GLuint build_templated(const char *vs, const char *fs_tmpl) {
  char *fs = glsl_compose(fs_tmpl);
  if (!fs)
    return 0;
  GLuint prog = shader_build(vs, fs);
  free(fs);
  return prog;
}

static void draw_cube_face(GLuint prog, int face) {
  mat4 proj, view;
  glm_perspective(glm_rad(90.0f), 1.0f, 0.1f, 10.0f, proj);
  vec3 eye = {0.0f, 0.0f, 0.0f}, center, up;
  glm_vec3_copy(FACE_DIRS[face], center);
  glm_vec3_copy(FACE_UPS[face], up);
  glm_lookat(eye, center, up, view);
  glUniformMatrix4fv(glGetUniformLocation(prog, "uProj"), 1, GL_FALSE,
                     (const GLfloat *)proj);
  glUniformMatrix4fv(glGetUniformLocation(prog, "uView"), 1, GL_FALSE,
                     (const GLfloat *)view);
  glDrawArrays(GL_TRIANGLES, 0, 36);
}

static int check_fbo(void) {
  GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (st != GL_FRAMEBUFFER_COMPLETE) {
    log_error("[ibl] status FBO = 0x%X\n", (unsigned)st);
    return 0;
  }
  return 1;
}

/* Attach face cubemap + diagnosa error GL.
 * glFramebufferTextureLayer TIDAK menerima TEXTURE_CUBE_MAP (hanya array/3D),
 * jadi pakai glFramebufferTexture2D dengan target POSITIVE_X+face. */
static int attach_layer(GLuint tex, int level, int face) {
  GLenum err;
  while ((err = glGetError()) != 0)
    log_error("[ibl] error GL lama=0x%X\n", (unsigned)err);

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, tex, level);
  err = glGetError();
  GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  int ok = (err == 0 && st == GL_FRAMEBUFFER_COMPLETE);
  if (!ok)
    log_error("[ibl] attach tex=%u lvl=%d face=%d glErr=0x%X st=0x%X\n",
            (unsigned)tex, level, face, (unsigned)err, (unsigned)st);
  return ok;
}

/* Alokasi 6 face cubemap: alloc_levels level dialokasikan sejak awal
 * (wajib utk render-ke-mip seperti prefilter; 0x8CD6 kalau level belum ada).
 * fmt: RGBA16F = HDR ideal; RGBA8 = fallback LDR utk driver lawas. */
static GLuint make_cubemap(int size, int alloc_levels, GLenum fmt,
                           int mipmap_filter) {
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
  for (int f = 0; f < 6; f++) {
    for (int lvl = 0; lvl < alloc_levels; lvl++) {
      int s = size >> lvl;
      if (s < 1)
        s = 1;
      glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, lvl, fmt, s, s, 0,
                   GL_RGBA, GL_FLOAT, NULL);
    }
  }
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                  mipmap_filter ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
  if (alloc_levels > 1)
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL,
                    alloc_levels - 1);
  return tex;
}

/* ================================================================== */
/*  Environment data                                                   */
/* ================================================================== */
float *ibl_env_procedural(int *out_w, int *out_h) {
  const int w = 256, h = 128;
  float *d = (float *)malloc((size_t)w * h * 4 * sizeof(float));
  if (!d)
    return NULL;

  const float zenith[3] = {0.10f, 0.22f, 0.50f};
  const float horizon[3] = {0.55f, 0.62f, 0.78f};
  const float ground[3] = {0.14f, 0.12f, 0.10f};

  for (int y = 0; y < h; y++) {
    float v = (y + 0.5f) / (float)h; /* row 0 = bawah (v ~ 0) */
    float lat = (v - 0.5f) * (float)M_PI;
    float dy = sinf(lat);
    float cy = cosf(lat);
    for (int x = 0; x < w; x++) {
      float u = (x + 0.5f) / (float)w;
      float theta = (u - 0.5f) * 2.0f * (float)M_PI;
      (void)cy;
      (void)theta; /* warna hanya tergantung ketinggian -> azimuth bebas */

      float col[3];
      if (dy >= 0.0f) {
        float t = powf(dy, 0.45f);
        for (int c = 0; c < 3; c++)
          col[c] = horizon[c] + (zenith[c] - horizon[c]) * t;
      } else {
        float t = powf(-dy, 0.5f);
        for (int c = 0; c < 3; c++)
          col[c] = horizon[c] * 0.85f + (ground[c] - horizon[c] * 0.85f) * t;
      }

      float *p = d + ((size_t)y * w + x) * 4;
      p[0] = col[0];
      p[1] = col[1];
      p[2] = col[2];
      p[3] = 1.0f;
    }
  }
  *out_w = w;
  *out_h = h;
  return d;
}

float *ibl_env_load_hdr(const char *path, int *out_w, int *out_h) {
  if (!path || !stbi_is_hdr(path))
    return NULL;
  stbi_set_flip_vertically_on_load(1); /* file .hdr: row 0 = atas */
  int w = 0, h = 0, n = 0;
  float *px = stbi_loadf(path, &w, &h, &n, 4);
  stbi_set_flip_vertically_on_load(0);
  if (!px)
    return NULL;
  *out_w = w;
  *out_h = h;
  return px;
}

/* ================================================================== */
/*  Bake pipeline                                                      */
/* ================================================================== */
static void free_ibl_textures(IblContext *ibl) {
  if (!ibl)
    return;
  if (ibl->env_cubemap)
    glDeleteTextures(1, &ibl->env_cubemap);
  if (ibl->irradiance)
    glDeleteTextures(1, &ibl->irradiance);
  if (ibl->prefilter)
    glDeleteTextures(1, &ibl->prefilter);
  if (ibl->brdf_lut)
    glDeleteTextures(1, &ibl->brdf_lut);
  ibl->env_cubemap = ibl->irradiance = 0;
  ibl->prefilter = ibl->brdf_lut = 0;
  ibl->ok = 0;
}

static int bake_chain(IblContext *ibl, const float *hdr, int w, int h,
                      GLenum fmt);

int ibl_build(IblContext *ibl, const float *hdr, int w, int h) {
  memset(ibl, 0, sizeof(*ibl));
  if (!hdr || w <= 0 || h <= 0)
    return 0;

  /* --- Program --- */
  G.prog_convert = build_templated(CUBE_VS, CONVERT_FS);
  G.prog_irr = build_templated(CUBE_VS, IRRADIANCE_FS);
  G.prog_pre = build_templated(CUBE_VS, PREFILTER_FS);
  G.prog_brdf = build_templated(QUAD_VS, BRDF_FS);
  G.prog_bg = shader_build(BG_VS, BG_FS);
  if (!G.prog_convert || !G.prog_irr || !G.prog_pre || !G.prog_brdf ||
      !G.prog_bg) {
    log_error("[ibl] gagal membuat program bake\n");
    ibl_free(ibl);
    return 0;
  }

  /* --- Geometry --- */
  glGenVertexArrays(1, &G.cube_vao);
  glBindVertexArray(G.cube_vao);
  glGenBuffers(1, &G.cube_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, G.cube_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(CUBE_VERTS), CUBE_VERTS, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);

  glGenVertexArrays(1, &G.quad_vao);
  glBindVertexArray(G.quad_vao);
  glGenBuffers(1, &G.quad_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, G.quad_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD_VERTS), QUAD_VERTS, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
  glBindVertexArray(0);

  /* --- FBO --- */
  glGenFramebuffers(1, &G.fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, G.fbo);

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glActiveTexture(GL_TEXTURE0);

  /* Coba HDR dulu; kalau float RT tidak didukung driver -> fallback LDR. */
  const GLenum fmt_try[2] = {GL_RGBA16F, GL_RGBA8};
  for (int i = 0; i < 2; i++) {
    if (bake_chain(ibl, hdr, w, h, fmt_try[i])) {
      log_info("[ibl] bake selesai (%s): env=%d irradiance=%d "
             "prefilter=%dx%d lut=%d\n",
             i == 0 ? "RGBA16F HDR" : "RGBA8 LDR", ENV_SIZE, IRR_SIZE,
             PREF_SIZE, PREF_SIZE, LUT_SIZE);
      return 1;
    }
    log_error("[ibl] bake format 0x%X gagal\n", (unsigned)fmt_try[i]);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return 0;
}

static int bake_chain(IblContext *ibl, const float *hdr, int w, int h,
                      GLenum fmt) {
  free_ibl_textures(ibl);
  /* Ikat FBO di sini (bukan sekali di pemanggil): path gagal di percobaan
   * sebelumnya melepasnya, dan tanpa ini retry menempel ke FBO default. */
  glBindFramebuffer(GL_FRAMEBUFFER, G.fbo);

  /* --- 1. Equirect HDR -> texture 2D --- */
  GLuint equirect = 0;
  glGenTextures(1, &equirect);
  glBindTexture(GL_TEXTURE_2D, equirect);
  glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, GL_RGBA, GL_FLOAT, hdr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  /* --- 2. Konversi ke env cubemap + mips --- */
  ibl->env_cubemap = make_cubemap(ENV_SIZE, 1, fmt, 1);
  glUseProgram(G.prog_convert);
  glUniform1i(glGetUniformLocation(G.prog_convert, "uEquirect"), 0);
  glBindTexture(GL_TEXTURE_2D, equirect);
  glBindVertexArray(G.cube_vao);
  for (int f = 0; f < 6; f++) {
    if (!attach_layer(ibl->env_cubemap, 0, f))
      goto fail;
    glViewport(0, 0, ENV_SIZE, ENV_SIZE);
    draw_cube_face(G.prog_convert, f);
  }
  glBindTexture(GL_TEXTURE_CUBE_MAP, ibl->env_cubemap);
  glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
  glDeleteTextures(1, &equirect);
  equirect = 0;

  /* --- 3. Irradiance (diffuse convolution) --- */
  ibl->irradiance = make_cubemap(IRR_SIZE, 1, fmt, 0);
  glUseProgram(G.prog_irr);
  glUniform1i(glGetUniformLocation(G.prog_irr, "uEnv"), 0);
  glBindTexture(GL_TEXTURE_CUBE_MAP, ibl->env_cubemap);
  for (int f = 0; f < 6; f++) {
    if (!attach_layer(ibl->irradiance, 0, f))
      goto fail;
    glViewport(0, 0, IRR_SIZE, IRR_SIZE);
    draw_cube_face(G.prog_irr, f);
  }

  /* --- 4. Prefilter specular (per mip = per roughness) --- */
  ibl->prefilter = make_cubemap(PREF_SIZE, PREF_MIPS, fmt, 1);
  glUseProgram(G.prog_pre);
  glUniform1i(glGetUniformLocation(G.prog_pre, "uEnv"), 0);
  glUniform1f(glGetUniformLocation(G.prog_pre, "uEnvSize"), (float)ENV_SIZE);
  glBindTexture(GL_TEXTURE_CUBE_MAP, ibl->env_cubemap);
  for (int m = 0; m < PREF_MIPS; m++) {
    int size = PREF_SIZE >> m;
    glUniform1f(glGetUniformLocation(G.prog_pre, "uRoughness"),
                (float)m / (float)(PREF_MIPS - 1));
    for (int f = 0; f < 6; f++) {
      if (!attach_layer(ibl->prefilter, m, f))
        goto fail;
      glViewport(0, 0, size, size);
      draw_cube_face(G.prog_pre, f);
    }
  }

  /* --- 5. BRDF LUT --- */
  glGenTextures(1, &ibl->brdf_lut);
  glBindTexture(GL_TEXTURE_2D, ibl->brdf_lut);
  glTexImage2D(GL_TEXTURE_2D, 0, fmt, LUT_SIZE, LUT_SIZE, 0, GL_RGBA,
               GL_FLOAT, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         ibl->brdf_lut, 0);
  if (!check_fbo())
    goto fail;
  glViewport(0, 0, LUT_SIZE, LUT_SIZE);
  glUseProgram(G.prog_brdf);
  glBindVertexArray(G.quad_vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindVertexArray(0);
  ibl->ok = 1;
  return 1;

fail:
  if (equirect)
    glDeleteTextures(1, &equirect);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  free_ibl_textures(ibl);
  return 0;
}

void ibl_draw_background(const IblContext *ibl, const float inv_view_proj[16],
                         const float cam_pos[3], float exposure) {
  if (!ibl || !ibl->ok)
    return;

  glDisable(GL_DEPTH_TEST);
  glUseProgram(G.prog_bg);
  glUniformMatrix4fv(glGetUniformLocation(G.prog_bg, "uInvVP"), 1, GL_FALSE,
                     inv_view_proj);
  glUniform3fv(glGetUniformLocation(G.prog_bg, "uCamPos"), 1, cam_pos);
  glUniform1f(glGetUniformLocation(G.prog_bg, "uExposure"), exposure);
  glUniform1i(glGetUniformLocation(G.prog_bg, "uEnv"), 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_CUBE_MAP, ibl->env_cubemap);
  glBindVertexArray(G.quad_vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
  glEnable(GL_DEPTH_TEST);
}

void ibl_free(IblContext *ibl) {
  if (ibl) {
    if (ibl->env_cubemap)
      glDeleteTextures(1, &ibl->env_cubemap);
    if (ibl->irradiance)
      glDeleteTextures(1, &ibl->irradiance);
    if (ibl->prefilter)
      glDeleteTextures(1, &ibl->prefilter);
    if (ibl->brdf_lut)
      glDeleteTextures(1, &ibl->brdf_lut);
    memset(ibl, 0, sizeof(*ibl));
  }

  GLuint progs[] = {G.prog_convert, G.prog_irr, G.prog_pre, G.prog_brdf,
                    G.prog_bg};
  for (size_t i = 0; i < sizeof(progs) / sizeof(progs[0]); i++)
    if (progs[i])
      glDeleteProgram(progs[i]);
  if (G.cube_vbo)
    glDeleteBuffers(1, &G.cube_vbo);
  if (G.quad_vbo)
    glDeleteBuffers(1, &G.quad_vbo);
  if (G.cube_vao)
    glDeleteVertexArrays(1, &G.cube_vao);
  if (G.quad_vao)
    glDeleteVertexArrays(1, &G.quad_vao);
  if (G.fbo)
    glDeleteFramebuffers(1, &G.fbo);
  memset(&G, 0, sizeof(G));
}

