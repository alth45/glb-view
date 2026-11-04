#include "app/app.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cglm/cglm.h>

#include "camera/camera.h"
#include "core/log.h"
#include "gfx/pbr_ibl.h"
#include "gfx/shader.h"
#include "gl/gl_loader.h"
#include "model/gltf_model.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/*  Shader PBR                                                         */
/* ================================================================== */
static const char *VS_SRC =
    "#version 330 core\n"
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=1) in vec3 aNormal;\n"
    "layout(location=2) in vec2 aUV0;\n"
    "layout(location=3) in vec2 aUV1;\n"
    "uniform mat4 uView;\n"
    "uniform mat4 uProj;\n"
    "out vec3 vWorldPos;\n"
    "out vec3 vNormal;\n"
    "out vec2 vUV0;\n"
    "out vec2 vUV1;\n"
    "void main() {\n"
    "  vWorldPos = aPos;\n"
    "  vNormal   = aNormal;\n"
    "  vUV0      = aUV0;\n"
    "  vUV1      = aUV1;\n"
    "  gl_Position = uProj * uView * vec4(aPos, 1.0);\n"
    "}\n";

static const char *FS_SRC =
    "#version 330 core\n"
    "in vec3 vWorldPos;\n"
    "in vec3 vNormal;\n"
    "in vec2 vUV0;\n"
    "in vec2 vUV1;\n"
    "out vec4 FragColor;\n"
    "\n"
    "const float PI = 3.14159265359;\n"
    "\n"
    "/* ---- Material ---- */\n"
    "uniform vec4  uBaseColor;\n"
    "uniform float uMetallic;\n"
    "uniform float uRoughness;\n"
    "uniform vec3  uEmissive;\n"
    "uniform float uNormalScale;\n"
    "uniform float uOcclusionStr;\n"
    "uniform float uAlphaCutoff;\n"
    "uniform int   uAlphaMode;      /* 0=OPAQUE 1=MASK 2=BLEND */\n"
    "uniform int   uHasAlbedo, uHasMR, uHasNormal, uHasOcc, uHasEmissive;\n"
    "uniform int   uUVAlbedo, uUVMR, uUVNormal, uUVOcc, uUVEmis;\n"
    "uniform sampler2D uAlbedoTex;\n"
    "uniform sampler2D uMrTex;\n"
    "uniform sampler2D uNormalTex;\n"
    "uniform sampler2D uOcclusionTex;\n"
    "uniform sampler2D uEmissiveTex;\n"
    "\n"
    "/* ---- Lighting ---- */\n"
    "uniform vec3  uCamPos;\n"
    "uniform vec3  uSunDir;\n"
    "uniform vec3  uSunColor;\n"
    "uniform float uExposure;\n"
    "uniform int   uUseIBL;\n"
    "uniform samplerCube uIrradiance;\n"
    "uniform samplerCube uPrefilter;\n"
    "uniform sampler2D   uBrdfLUT;\n"
    "\n"
    "vec2 pickUV(int set) { return set == 0 ? vUV0 : vUV1; }\n"
    "\n"
    "float distributionGGX(vec3 N, vec3 H, float roughness) {\n"
    "  float a  = roughness * roughness;\n"
    "  float a2 = a * a;\n"
    "  float NdotH = max(dot(N, H), 0.0);\n"
    "  float d = NdotH * NdotH * (a2 - 1.0) + 1.0;\n"
    "  return a2 / (PI * d * d + 1e-6);\n"
    "}\n"
    "\n"
    "float geomSchlick(float NdotX, float k) {\n"
    "  return NdotX / (NdotX * (1.0 - k) + k);\n"
    "}\n"
    "\n"
    "vec3 fresnelSchlick(float VdotH, vec3 F0) {\n"
    "  return F0 + (1.0 - F0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);\n"
    "}\n"
    "\n"
    "vec3 fresnelSchlickRoughness(float NdotV, vec3 F0, float roughness) {\n"
    "  vec3 fr = max(vec3(1.0 - roughness), F0);\n"
    "  return F0 + (fr - F0) * pow(clamp(1.0 - NdotV, 0.0, 1.0), 5.0);\n"
    "}\n"
    "\n"
    "/* TBN dari derivative (tanpa atribut tangent) */\n"
    "vec3 perturbNormal(vec3 N, vec3 pos, vec2 uv, vec3 mapN) {\n"
    "  vec3 dp1 = dFdx(pos), dp2 = dFdy(pos);\n"
    "  vec2 duv1 = dFdx(uv),  duv2 = dFdy(uv);\n"
    "  vec3 dp2p = cross(dp2, N), dp1p = cross(N, dp1);\n"
    "  vec3 T = dp2p * duv1.x + dp1p * duv2.x;\n"
    "  vec3 B = dp2p * duv1.y + dp1p * duv2.y;\n"
    "  float invmax = inversesqrt(max(dot(T, T), dot(B, B)));\n"
    "  return normalize(mat3(T * invmax, B * invmax, N) * mapN);\n"
    "}\n"
    "\n"
    "vec3 aces(vec3 x) {\n"
    "  const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;\n"
    "  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);\n"
    "}\n"
    "\n"
    "void main() {\n"
    "  vec4 base = uBaseColor;\n"
    "  if (uHasAlbedo == 1) base *= texture(uAlbedoTex, pickUV(uUVAlbedo));\n"
    "\n"
    "  if (uAlphaMode == 1 && base.a < uAlphaCutoff) discard;\n"
    "\n"
    "  vec3 albedo = base.rgb;\n"
    "  float metallic = uMetallic;\n"
    "  float roughness = uRoughness;\n"
    "  if (uHasMR == 1) {\n"
    "    vec3 mr = texture(uMrTex, pickUV(uUVMR)).rgb;\n"
    "    roughness *= mr.g;   /* G = roughness */\n"
    "    metallic  *= mr.b;   /* B = metallic  */\n"
    "  }\n"
    "  roughness = clamp(roughness, 0.045, 1.0);\n"
    "  metallic  = clamp(metallic, 0.0, 1.0);\n"
    "\n"
    "  vec3 N = normalize(vNormal);\n"
    "  if (uHasNormal == 1) {\n"
    "    vec3 mapN = texture(uNormalTex, pickUV(uUVNormal)).xyz * 2.0 - 1.0;\n"
    "    mapN.xy *= uNormalScale;\n"
    "    N = perturbNormal(N, vWorldPos, pickUV(uUVNormal), normalize(mapN));\n"
    "  }\n"
    "\n"
    "  float ao = 1.0;\n"
    "  if (uHasOcc == 1) {\n"
    "    float occ = texture(uOcclusionTex, pickUV(uUVOcc)).r;\n"
    "    ao = mix(1.0, occ, uOcclusionStr);\n"
    "  }\n"
    "\n"
    "  vec3 emissive = uEmissive;\n"
    "  if (uHasEmissive == 1)\n"
    "    emissive *= texture(uEmissiveTex, pickUV(uUVEmis)).rgb;\n"
    "\n"
    "  vec3 V = normalize(uCamPos - vWorldPos);\n"
    "  float NdotV = max(dot(N, V), 1e-4);\n"
    "  vec3 F0 = mix(vec3(0.04), albedo, metallic);\n"
    "\n"
    "  /* ---- Direct: satu directional sun (Cook-Torrance) ---- */\n"
    "  vec3 L = normalize(uSunDir);\n"
    "  vec3 H = normalize(V + L);\n"
    "  float NdotL = max(dot(N, L), 0.0);\n"
    "  vec3 direct = vec3(0.0);\n"
    "  if (NdotL > 0.0) {\n"
    "    float VdotH = max(dot(V, H), 0.0);\n"
    "    vec3  F = fresnelSchlick(VdotH, F0);\n"
    "    float D = distributionGGX(N, H, roughness);\n"
    "    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;\n"
    "    float G = geomSchlick(NdotV, k) * geomSchlick(NdotL, k);\n"
    "    vec3 spec = D * G * F / (4.0 * NdotV * NdotL + 1e-4);\n"
    "    vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);\n"
    "    direct = (kd * albedo / PI + spec) * NdotL * uSunColor;\n"
    "  }\n"
    "\n"
    "  /* ---- Ambient: IBL split-sum (irradiance + prefilter + LUT) ---- */\n"
    "  vec3 ambient;\n"
    "  if (uUseIBL == 1) {\n"
    "    vec3 R = reflect(-V, N);\n"
    "    vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);\n"
    "    vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);\n"
    "    vec3 diff = textureLod(uIrradiance, N, 0.0).rgb;\n"
    "    vec3 pre  = textureLod(uPrefilter, R, roughness * 4.0).rgb;\n"
    "    vec2 brdf = textureLod(uBrdfLUT, vec2(NdotV, roughness), 0.0).rg;\n"
    "    ambient = (kd * diff * albedo + pre * (F0 * brdf.x + brdf.y)) * ao;\n"
    "  } else {\n"
    "    ambient = vec3(0.12, 0.13, 0.15) * albedo * ao;\n"
    "  }\n"
    "\n"
    "  vec3 color = ambient + direct + emissive;\n"
    "  color = aces(color * uExposure);\n"
    "  color = pow(color, vec3(1.0 / 2.2));\n"
    "  FragColor = vec4(color, uAlphaMode == 2 ? base.a : 1.0);\n"
    "}\n";

/* ================================================================== */
/*  State                                                              */
/* ================================================================== */
struct AppState {
  GLFWwindow *window;
  OrbitCamera camera;
  Model model;
  IblContext ibl;
  int ibl_ok;

  GLuint program;
  Material default_mat;

  struct {
    GLint view, proj, cam_pos;
    GLint base_color, metallic, roughness, emissive;
    GLint normal_scale, occlusion_str, alpha_cutoff, alpha_mode;
    GLint has_albedo, has_mr, has_normal, has_occ, has_emissive;
    GLint uv_albedo, uv_mr, uv_normal, uv_occ, uv_emis;
    GLint sun_dir;      
    GLint sun_color;    
    GLint exposure;   
  } u;

  /* Input */
  int dragging;
  double last_x, last_y;

  /* FPS */
  double fps_last;
  int fps_frames;

  float exposure;
  vec3 sun_dir;
  vec3 sun_color;
};

/* ================================================================== */
/*  Callbacks                                                          */
/* ================================================================== */
static void cb_mouse_button(GLFWwindow *w, int button, int action, int mods) {
  (void)mods;
  AppState *app = (AppState *)glfwGetWindowUserPointer(w);
  if (button == GLFW_MOUSE_BUTTON_LEFT) {
    app->dragging = (action == GLFW_PRESS);
    glfwGetCursorPos(w, &app->last_x, &app->last_y);
  }
}

static void cb_cursor_pos(GLFWwindow *w, double x, double y) {
  AppState *app = (AppState *)glfwGetWindowUserPointer(w);
  if (!app->dragging) return;
  float dx = (float)(x - app->last_x);
  float dy = (float)(y - app->last_y);
  app->last_x = x;
  app->last_y = y;
  camera_orbit(&app->camera, dx, dy);
}

static void cb_scroll(GLFWwindow *w, double xo, double yo) {
  (void)xo;
  AppState *app = (AppState *)glfwGetWindowUserPointer(w);
  camera_zoom(&app->camera, (float)yo);
}

/* ================================================================== */
/*  Setup                                                              */
/* ================================================================== */
static void *proc_loader(const char *name) {
  return (void *)glfwGetProcAddress(name);
}

static GLFWwindow *create_window(void) {
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_SAMPLES, 4);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

  GLFWwindow *w = glfwCreateWindow(1280, 720, "GLB Viewer", NULL, NULL);
  if (!w) {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    w = glfwCreateWindow(1280, 720, "GLB Viewer", NULL, NULL);
  }
  return w;
}

static int setup_gl(void) {
  if (!gl_load(proc_loader)) {
    log_error("Gagal memuat fungsi OpenGL");
    return 0;
  }
  const char *ver = (const char *)glGetString(GL_VERSION);
  const char *ren = (const char *)glGetString(GL_RENDERER);
  log_info("OpenGL : %s", ver ? ver : "(null)");
  log_info("Renderer: %s", ren ? ren : "(null)");
  return 1;
}

static void setup_gl_state(void) {
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_MULTISAMPLE);
  glDepthFunc(GL_LESS);
}

static void cache_uniforms(AppState *app) {
  GLuint p = app->program;
  app->u.view         = glGetUniformLocation(p, "uView");
  app->u.proj         = glGetUniformLocation(p, "uProj");
  app->u.cam_pos      = glGetUniformLocation(p, "uCamPos");
  app->u.base_color   = glGetUniformLocation(p, "uBaseColor");
  app->u.metallic     = glGetUniformLocation(p, "uMetallic");
  app->u.roughness    = glGetUniformLocation(p, "uRoughness");
  app->u.emissive     = glGetUniformLocation(p, "uEmissive");
  app->u.normal_scale = glGetUniformLocation(p, "uNormalScale");
  app->u.occlusion_str= glGetUniformLocation(p, "uOcclusionStr");
  app->u.alpha_cutoff = glGetUniformLocation(p, "uAlphaCutoff");
  app->u.alpha_mode   = glGetUniformLocation(p, "uAlphaMode");
  app->u.has_albedo   = glGetUniformLocation(p, "uHasAlbedo");
  app->u.has_mr       = glGetUniformLocation(p, "uHasMR");
  app->u.has_normal   = glGetUniformLocation(p, "uHasNormal");
  app->u.has_occ      = glGetUniformLocation(p, "uHasOcc");
  app->u.has_emissive = glGetUniformLocation(p, "uHasEmissive");
  app->u.uv_albedo    = glGetUniformLocation(p, "uUVAlbedo");
  app->u.uv_mr        = glGetUniformLocation(p, "uUVMR");
  app->u.uv_normal    = glGetUniformLocation(p, "uUVNormal");
  app->u.uv_occ       = glGetUniformLocation(p, "uUVOcc");
  app->u.uv_emis      = glGetUniformLocation(p, "uUVEmis");
  app->u.uv_emis      = glGetUniformLocation(p, "uUVEmis");
  app->u.sun_dir      = glGetUniformLocation(p, "uSunDir");
  app->u.sun_color    = glGetUniformLocation(p, "uSunColor");
  app->u.exposure     = glGetUniformLocation(p, "uExposure");
}

static int setup_shader(AppState *app) {
  app->program = shader_build(VS_SRC, FS_SRC);
  if (!app->program) return 0;

  glUseProgram(app->program);
  glUniform1i(glGetUniformLocation(app->program, "uAlbedoTex"),   0);
  glUniform1i(glGetUniformLocation(app->program, "uMrTex"),       1);
  glUniform1i(glGetUniformLocation(app->program, "uNormalTex"),   2);
  glUniform1i(glGetUniformLocation(app->program, "uOcclusionTex"),3);
  glUniform1i(glGetUniformLocation(app->program, "uEmissiveTex"), 4);
  glUniform1i(glGetUniformLocation(app->program, "uIrradiance"),  5);
  glUniform1i(glGetUniformLocation(app->program, "uPrefilter"),   6);
  glUniform1i(glGetUniformLocation(app->program, "uBrdfLUT"),     7);

  cache_uniforms(app);
  return 1;
}

static void setup_ibl(AppState *app, const char *hdr_path) {
  int env_w = 0, env_h = 0;
  float *env_data = NULL;

  if (hdr_path) {
    env_data = ibl_env_load_hdr(hdr_path, &env_w, &env_h);
    if (env_data)
      log_info("Environment HDR: %s (%dx%d)", hdr_path, env_w, env_h);
    else
      log_warn("Gagal memuat .hdr: %s (pakai prosedural)", hdr_path);
  }
  if (!env_data)
    env_data = ibl_env_procedural(&env_w, &env_h);

  app->ibl_ok = (env_data && ibl_build(&app->ibl, env_data, env_w, env_h));
  free(env_data);
  if (!app->ibl_ok)
    log_warn("[ibl] bake gagal, pakai ambient konstan");

  glUseProgram(app->program);
  glUniform1i(glGetUniformLocation(app->program, "uUseIBL"),
              app->ibl_ok ? 1 : 0);
}

static int load_model_and_camera(AppState *app, const char *path) {
  if (!model_load(&app->model, path)) {
    log_error("Gagal memuat model: %s", path);
    return 0;
  }
  log_info("Model dimuat: %zu mesh, %zu material, radius=%.3f",
           app->model.mesh_count, app->model.material_count,
           app->model.radius);

  camera_init(&app->camera, app->model.center, app->model.radius);
  return 1;
}

static void setup_default_material(AppState *app) {
  Material *m = &app->default_mat;
  memset(m, 0, sizeof(*m));
  m->base_color[0] = m->base_color[1] = 0.8f;
  m->base_color[2] = m->base_color[3] = 0.8f;
  m->roughness    = 0.8f;
  m->normal_scale = 1.0f;
  m->occlusion_str= 1.0f;
  m->alpha_cutoff = 0.5f;
}

/* ================================================================== */
/*  Render                                                             */
/* ================================================================== */
static void render_pass(AppState *app, int blend_pass) {
  if (blend_pass) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
  }

  Model *model = &app->model;
  for (size_t i = 0; i < model->mesh_count; i++) {
    Mesh *m = &model->meshes[i];
    if (m->index_count == 0) continue;

    Material *mat = &app->default_mat;
    if (m->material_index >= 0 &&
        (size_t)m->material_index < model->material_count)
      mat = &model->materials[m->material_index];

    int is_blend = (mat->alpha_mode == 2);
    if ((blend_pass == 0 && is_blend) || (blend_pass == 1 && !is_blend))
      continue;

    glUniform4fv(app->u.base_color,   1, mat->base_color);
    glUniform1f(app->u.metallic,        mat->metallic);
    glUniform1f(app->u.roughness,       mat->roughness);
    glUniform3fv(app->u.emissive,    1, mat->emissive);
    glUniform1f(app->u.normal_scale,    mat->normal_scale);
    glUniform1f(app->u.occlusion_str,   mat->occlusion_str);
    glUniform1f(app->u.alpha_cutoff,    mat->alpha_cutoff);
    glUniform1i(app->u.alpha_mode,      mat->alpha_mode);
    glUniform1i(app->u.has_albedo,      mat->albedo_tex   ? 1 : 0);
    glUniform1i(app->u.has_mr,          mat->mr_tex       ? 1 : 0);
    glUniform1i(app->u.has_normal,      mat->normal_tex   ? 1 : 0);
    glUniform1i(app->u.has_occ,         mat->occlusion_tex? 1 : 0);
    glUniform1i(app->u.has_emissive,    mat->emissive_tex ? 1 : 0);
    glUniform1i(app->u.uv_albedo,       mat->uv_albedo);
    glUniform1i(app->u.uv_mr,           mat->uv_mr);
    glUniform1i(app->u.uv_normal,       mat->uv_normal);
    glUniform1i(app->u.uv_occ,          mat->uv_occlusion);
    glUniform1i(app->u.uv_emis,         mat->uv_emissive);

    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, mat->albedo_tex);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, mat->mr_tex);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, mat->normal_tex);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, mat->occlusion_tex);
    glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, mat->emissive_tex);
    glActiveTexture(GL_TEXTURE0);

    glBindVertexArray(m->vao);
    glDrawElements(GL_TRIANGLES, m->index_count, GL_UNSIGNED_INT, 0);
  }

  if (blend_pass) {
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
  }
}

static void render_frame(AppState *app) {
  int fb_w = 0, fb_h = 0;
  glfwGetFramebufferSize(app->window, &fb_w, &fb_h);
  glViewport(0, 0, fb_w, fb_h);

  glClearColor(0.09f, 0.09f, 0.11f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  vec3 eye;
  camera_get_eye(&app->camera, eye);

  vec3 up = {0.0f, 1.0f, 0.0f};
  mat4 view, proj;
  camera_get_view(&app->camera, up, view);
  float aspect = (fb_h > 0) ? (float)fb_w / (float)fb_h : 1.0f;
  camera_get_projection(&app->camera, aspect, proj);

  if (app->ibl_ok) {
    mat4 vp, inv_vp;
    glm_mat4_mul(proj, view, vp);
    glm_mat4_inv(vp, inv_vp);
    ibl_draw_background(&app->ibl, (const float *)inv_vp, eye,
                        app->exposure);
  }

  glUseProgram(app->program);
  glUniformMatrix4fv(app->u.view, 1, GL_FALSE, (float *)view);
  glUniformMatrix4fv(app->u.proj, 1, GL_FALSE, (float *)proj);
  glUniform3fv(app->u.cam_pos, 1, eye);
  glUniform3fv(glGetUniformLocation(app->program, "uSunDir"), 1, app->sun_dir);
  glUniform3fv(glGetUniformLocation(app->program, "uSunColor"), 1,
               app->sun_color);
  glUniform1f(glGetUniformLocation(app->program, "uExposure"), app->exposure);
  glUniform3fv(app->u.sun_dir,   1, app->sun_dir);
  glUniform3fv(app->u.sun_color, 1, app->sun_color);
  glUniform1f(app->u.exposure,      app->exposure);

  if (app->ibl_ok) {
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_CUBE_MAP, app->ibl.irradiance);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_CUBE_MAP, app->ibl.prefilter);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, app->ibl.brdf_lut);
    glActiveTexture(GL_TEXTURE0);
  }

  render_pass(app, 0); /* opaque */
  render_pass(app, 1); /* blend  */

  glBindVertexArray(0);
  glfwSwapBuffers(app->window);
}

static void update_title(AppState *app) {
  double now = glfwGetTime();
  app->fps_frames++;
  if (now - app->fps_last >= 0.5) {
    double fps = app->fps_frames / (now - app->fps_last);
    app->fps_last = now;
    app->fps_frames = 0;
    char title[128];
    snprintf(title, sizeof(title), "GLB Viewer — %.1f FPS", fps);
    glfwSetWindowTitle(app->window, title);
  }
}

/* ================================================================== */
/*  Public API                                                         */
/* ================================================================== */
AppState *app_create(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);

  if (argc < 2) {
    log_error("Usage: %s <model.glb> [env.hdr]", argv[0]);
    return NULL;
  }

  AppState *app = (AppState *)calloc(1, sizeof(AppState));
  if (!app) return NULL;

  app->exposure = 1.0f;
  app->sun_dir[0] = 0.5f;
  app->sun_dir[1] = 1.0f;
  app->sun_dir[2] = 0.3f;
  glm_vec3_normalize(app->sun_dir);
  app->sun_color[0] = 3.0f;
  app->sun_color[1] = 2.85f;
  app->sun_color[2] = 2.55f;

  if (!glfwInit()) {
    log_error("glfwInit gagal");
    free(app);
    return NULL;
  }

  app->window = create_window();
  if (!app->window) {
    log_error("Gagal membuat window GLFW");
    glfwTerminate();
    free(app);
    return NULL;
  }
  glfwMakeContextCurrent(app->window);
  glfwSwapInterval(1);
  glfwSetWindowUserPointer(app->window, app);
  glfwSetMouseButtonCallback(app->window, cb_mouse_button);
  glfwSetCursorPosCallback(app->window, cb_cursor_pos);
  glfwSetScrollCallback(app->window, cb_scroll);

  if (!setup_gl()) { app_destroy(app); return NULL; }

  setup_gl_state();
  if (!setup_shader(app)) {
    log_error("Gagal compile shader PBR");
    app_destroy(app);
    return NULL;
  }
  setup_ibl(app, argc >= 3 ? argv[2] : NULL);
  if (!load_model_and_camera(app, argv[1])) {
    app_destroy(app);
    return NULL;
  }
  setup_default_material(app);
  return app;
}

void app_run(AppState *app) {
  if (!app || !app->window) return;

  while (!glfwWindowShouldClose(app->window)) {
    render_frame(app);
    update_title(app);
    glfwPollEvents();
    if (glfwGetKey(app->window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
      glfwSetWindowShouldClose(app->window, GLFW_TRUE);
  }
}

void app_destroy(AppState *app) {
  if (!app) return;

  if (app->program) glDeleteProgram(app->program);
  model_free(&app->model);
  ibl_free(&app->ibl);

  if (app->window) {
    glfwDestroyWindow(app->window);
    glfwTerminate();
  }
  free(app);
}