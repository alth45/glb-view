#include "app/app.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cglm/cglm.h>

#include "app/config.h"
#include "app/hud.h"
#include "camera/camera.h"
#include "core/log.h"
#include "gfx/pbr_ibl.h"
#include "gfx/shader.h"
#include "gl/gl_loader.h"
#include "model/gltf_model.h"

#include "stb_image_write.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CONFIG_FILE "glb_viewer.ini"
#define SHADER_VS   "shaders/pbr.vert"
#define SHADER_FS   "shaders/pbr.frag"

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
  Hud *hud;

  struct {
    GLint view, proj, cam_pos;
    GLint base_color, metallic, roughness, emissive;
    GLint normal_scale, occlusion_str, alpha_cutoff, alpha_mode;
    GLint has_albedo, has_mr, has_normal, has_occ, has_emissive;
    GLint uv_albedo, uv_mr, uv_normal, uv_occ, uv_emis;
    GLint sun_dir, sun_color, exposure, use_ibl;
    GLint debug_mode, vignette, screen_size;
  } u;

  int    dragging;
  double last_x, last_y;

  double fps_last;
  int    fps_frames;

  double last_time;
  float  fps;

  float exposure;
  vec3  sun_dir;
  vec3  sun_color;

  int   wireframe;
  int   auto_rotate;
  int   use_ibl_user;
  int   debug_mode;
  float vignette;
  int   screenshot_pending;

  char *model_path;
  char *hdr_path;
};

/* Forward decls untuk fungsi yang dipakai callback tapi didefinisi di bawah */
static void snapshot_config(const struct AppState *app, ViewerConfig *cfg);
static void reload_model(struct AppState *app);
void        app_reload_ibl(struct AppState *app);   /* dipakai cb_drop */


/* ================================================================== */
/*  Helper: baca file teks                                            */
/* ================================================================== */
static char *read_text_file(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0) { fclose(f); return NULL; }
  char *buf = (char *)malloc((size_t)sz + 1);
  if (!buf) { fclose(f); return NULL; }
  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
    fclose(f); free(buf); return NULL;
  }
  buf[sz] = 0;
  fclose(f);
  return buf;
}

/* ================================================================== */
/*  Shader load & reload                                              */
/* ================================================================== */
static void set_sampler_uniforms(GLuint p) {
  glUniform1i(glGetUniformLocation(p, "uAlbedoTex"),    0);
  glUniform1i(glGetUniformLocation(p, "uMrTex"),        1);
  glUniform1i(glGetUniformLocation(p, "uNormalTex"),    2);
  glUniform1i(glGetUniformLocation(p, "uOcclusionTex"), 3);
  glUniform1i(glGetUniformLocation(p, "uEmissiveTex"),  4);
  glUniform1i(glGetUniformLocation(p, "uIrradiance"),   5);
  glUniform1i(glGetUniformLocation(p, "uPrefilter"),    6);
  glUniform1i(glGetUniformLocation(p, "uBrdfLUT"),      7);
}

static void cache_uniforms(struct AppState *app);

static GLuint load_program_from_files(void) {
  char *vs = read_text_file(SHADER_VS);
  char *fs = read_text_file(SHADER_FS);
  if (!vs || !fs) {
    log_error("[shader] gagal baca %s / %s", SHADER_VS, SHADER_FS);
    free(vs); free(fs);
    return 0;
  }
  GLuint p = shader_build(vs, fs);
  free(vs); free(fs);
  return p;
}

static int reload_shader(struct AppState *app) {
  GLuint new_prog = load_program_from_files();
  if (!new_prog) {
    log_error("[shader] reload gagal, program lama tetap dipakai");
    return 0;
  }
  GLuint old = app->program;
  app->program = new_prog;
  glUseProgram(app->program);
  set_sampler_uniforms(app->program);
  cache_uniforms(app);
  if (old) glDeleteProgram(old);
  log_info("[shader] reload OK");
  return 1;
}

/* ================================================================== */
/*  Callbacks                                                          */
/* ================================================================== */

static void cb_mouse_button(GLFWwindow *w, int button, int action, int mods) {
  (void)mods;
  struct AppState *app = (struct AppState *)glfwGetWindowUserPointer(w);
  if (button == GLFW_MOUSE_BUTTON_LEFT) {
    app->dragging = (action == GLFW_PRESS);
    glfwGetCursorPos(w, &app->last_x, &app->last_y);
  }
}

static void cb_cursor_pos(GLFWwindow *w, double x, double y) {
  struct AppState *app = (struct AppState *)glfwGetWindowUserPointer(w);
  if (!app->dragging) return;
  float dx = (float)(x - app->last_x);
  float dy = (float)(y - app->last_y);
  app->last_x = x;
  app->last_y = y;
  camera_orbit(&app->camera, dx, dy);
}

static void cb_scroll(GLFWwindow *w, double xo, double yo) {
  (void)xo;
  struct AppState *app = (struct AppState *)glfwGetWindowUserPointer(w);
  camera_zoom(&app->camera, (float)yo);
}

static void cb_key(GLFWwindow *w, int key, int scancode, int action,
                   int mods) {
  (void)scancode;
  if (action != GLFW_PRESS) return;
  struct AppState *app = (struct AppState *)glfwGetWindowUserPointer(w);

    if ((mods & GLFW_MOD_CONTROL) && key == GLFW_KEY_S) {
      ViewerConfig cfg;
      snapshot_config((const struct AppState *)app, &cfg);
      if (config_save(&cfg, CONFIG_FILE))
        log_info("Config disimpan: %s", CONFIG_FILE);
      else
        log_error("Config gagal disimpan: %s", CONFIG_FILE);
      return;
  }

  switch (key) {
    case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(w, GLFW_TRUE); break;
    case GLFW_KEY_F:
      app->wireframe = !app->wireframe;
      log_info("Wireframe: %s", app->wireframe ? "ON" : "OFF");
      break;
    case GLFW_KEY_SPACE:
      app->auto_rotate = !app->auto_rotate;
      log_info("Auto-rotate: %s", app->auto_rotate ? "ON" : "OFF");
      break;
    case GLFW_KEY_I:
      app->use_ibl_user = !app->use_ibl_user;
      log_info("IBL: %s", app->use_ibl_user ? "ON" : "OFF");
      break;
    case GLFW_KEY_EQUAL:
    case GLFW_KEY_KP_ADD:
      app->exposure *= 1.1f;
      if (app->exposure > 10.0f) app->exposure = 10.0f;
      log_info("Exposure: %.2f", app->exposure);
      break;
    case GLFW_KEY_MINUS:
    case GLFW_KEY_KP_SUBTRACT:
      app->exposure /= 1.1f;
      if (app->exposure < 0.1f) app->exposure = 0.1f;
      log_info("Exposure: %.2f", app->exposure);
      break;
    case GLFW_KEY_P:
      app->screenshot_pending = 1;
      break;
    case GLFW_KEY_R:
      reload_model(app);
      break;
    case GLFW_KEY_HOME:
      camera_init(&app->camera, app->model.center, app->model.radius);
      log_info("Camera reset");
      break;
    case GLFW_KEY_F5:
      reload_shader(app);
      break;
    case GLFW_KEY_0: app->debug_mode = 0; log_info("Debug: OFF"); break;
    case GLFW_KEY_1: app->debug_mode = 1; log_info("Debug: albedo"); break;
    case GLFW_KEY_2: app->debug_mode = 2; log_info("Debug: normal"); break;
    case GLFW_KEY_3: app->debug_mode = 3; log_info("Debug: metallic"); break;
    case GLFW_KEY_4: app->debug_mode = 4; log_info("Debug: roughness"); break;
    case GLFW_KEY_5: app->debug_mode = 5; log_info("Debug: ao"); break;
    case GLFW_KEY_6: app->debug_mode = 6; log_info("Debug: emissive"); break;
    case GLFW_KEY_7: app->debug_mode = 7; log_info("Debug: uv"); break;
  }
}

static void cb_drop(GLFWwindow *w, int count, const char **paths) {
  struct AppState *app = (struct AppState *)glfwGetWindowUserPointer(w);
  if (count < 1 || !paths[0]) return;
  const char *p = paths[0];
  size_t n = strlen(p);
  int is_hdr = (n > 4 && strcmp(p + n - 4, ".hdr") == 0);

  if (is_hdr) {
    free(app->hdr_path);
    app->hdr_path = strdup(p);
    log_info("Drop HDR: %s", p);
    /* setup_ibl forward declaration tidak diekspos; panggil ulang via reload */
    app_reload_ibl(app);
  } else {
    free(app->model_path);
    app->model_path = strdup(p);
    log_info("Drop model: %s", p);
    reload_model(app);
  }
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

static void cache_uniforms(struct AppState *app) {
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
  app->u.sun_dir      = glGetUniformLocation(p, "uSunDir");
  app->u.sun_color    = glGetUniformLocation(p, "uSunColor");
  app->u.exposure     = glGetUniformLocation(p, "uExposure");
  app->u.use_ibl      = glGetUniformLocation(p, "uUseIBL");
  app->u.debug_mode   = glGetUniformLocation(p, "uDebugMode");
  app->u.vignette     = glGetUniformLocation(p, "uVignette");
  app->u.screen_size  = glGetUniformLocation(p, "uScreenSize");
}

static int setup_shader(struct AppState *app) {
  app->program = load_program_from_files();
  if (!app->program) return 0;
  glUseProgram(app->program);
  set_sampler_uniforms(app->program);
  cache_uniforms(app);
  return 1;
}

static void setup_ibl_internal(struct AppState *app) {
  int env_w = 0, env_h = 0;
  float *env_data = NULL;

  if (app->hdr_path) {
    env_data = ibl_env_load_hdr(app->hdr_path, &env_w, &env_h);
    if (env_data)
      log_info("Environment HDR: %s (%dx%d)", app->hdr_path, env_w, env_h);
    else
      log_warn("Gagal memuat .hdr: %s (pakai prosedural)", app->hdr_path);
  }
  if (!env_data)
    env_data = ibl_env_procedural(&env_w, &env_h);

  app->ibl_ok = (env_data && ibl_build(&app->ibl, env_data, env_w, env_h));
  free(env_data);
  if (!app->ibl_ok)
    log_warn("[ibl] bake gagal, pakai ambient konstan");
}

/* Dipakai oleh drag-and-drop untuk reload env */
void app_reload_ibl(struct AppState *app) {
  ibl_free(&app->ibl);
  setup_ibl_internal(app);
}

static int load_model_and_camera(struct AppState *app, const char *path) {
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

static void setup_default_material(struct AppState *app) {
  Material *m = &app->default_mat;
  memset(m, 0, sizeof(*m));
  m->base_color[0] = m->base_color[1] = 0.8f;
  m->base_color[2] = m->base_color[3] = 0.8f;
  m->roughness    = 0.8f;
  m->normal_scale = 1.0f;
  m->occlusion_str= 1.0f;
  m->alpha_cutoff = 0.5f;
}

static void reload_model(struct AppState *app) {
  if (!app->model_path) return;
  log_info("Reload: %s", app->model_path);
  model_free(&app->model);
  if (!model_load(&app->model, app->model_path)) {
    log_error("Reload gagal — keluar");
    glfwSetWindowShouldClose(app->window, GLFW_TRUE);
    return;
  }
  camera_init(&app->camera, app->model.center, app->model.radius);
}

/* ================================================================== */
/*  Config <-> AppState                                               */
/* ================================================================== */
static void apply_config(struct AppState *app, const ViewerConfig *cfg) {
  app->exposure     = cfg->exposure;
  app->wireframe    = cfg->wireframe;
  app->auto_rotate  = cfg->auto_rotate;
  app->use_ibl_user = cfg->use_ibl;
  app->debug_mode   = cfg->debug_mode;
  app->vignette     = cfg->vignette;
  glm_vec3_copy(cfg->sun_dir,   app->sun_dir);
  glm_vec3_copy(cfg->sun_color, app->sun_color);
}

static void snapshot_config(const struct AppState *app, ViewerConfig *cfg) {
  memset(cfg, 0, sizeof(*cfg));
  cfg->exposure     = app->exposure;
  cfg->wireframe    = app->wireframe;
  cfg->auto_rotate  = app->auto_rotate;
  cfg->use_ibl      = app->use_ibl_user;
  cfg->debug_mode   = app->debug_mode;
  cfg->vignette     = app->vignette;
  glm_vec3_copy(app->sun_dir,   cfg->sun_dir);
  glm_vec3_copy(app->sun_color, cfg->sun_color);
}



/* ================================================================== */
/*  Screenshot                                                         */
/* ================================================================== */
static void take_screenshot(struct AppState *app) {
  int w = 0, h = 0;
  glfwGetFramebufferSize(app->window, &w, &h);
  if (w <= 0 || h <= 0) return;

  size_t stride = (size_t)w * 3;
  unsigned char *pixels = (unsigned char *)malloc(stride * (size_t)h);
  if (!pixels) { log_error("Screenshot: OOM"); return; }

  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels);

  /* flip vertikal */
  unsigned char *tmp = (unsigned char *)malloc(stride);
  if (tmp) {
    for (int y = 0; y < h / 2; y++) {
      unsigned char *top = pixels + (size_t)y * stride;
      unsigned char *bot = pixels + (size_t)(h - 1 - y) * stride;
      memcpy(tmp, top, stride);
      memcpy(top, bot, stride);
      memcpy(bot, tmp, stride);
    }
    free(tmp);
  }

  char fname[128];
  time_t t = time(NULL);
  snprintf(fname, sizeof(fname), "screenshot_%lld.png", (long long)t);

  if (stbi_write_png(fname, w, h, 3, pixels, (int)stride))
    log_info("Screenshot: %s (%dx%d)", fname, w, h);
  else
    log_error("Screenshot: gagal tulis %s", fname);

  free(pixels);
}

/* ================================================================== */
/*  Render                                                            */
/* ================================================================== */
static void render_pass(struct AppState *app, int blend_pass) {
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

static const char *debug_mode_name(int m) {
  switch (m) {
    case 0: return "OFF";
    case 1: return "albedo";
    case 2: return "normal";
    case 3: return "metallic";
    case 4: return "roughness";
    case 5: return "ao";
    case 6: return "emissive";
    case 7: return "uv";
    default: return "?";
  }
}

static void draw_hud(struct AppState *app, int fb_w, int fb_h) {
  char line1[256], line2[256];
  const char *base = strrchr(app->model_path, '/');
  base = base ? base + 1 : app->model_path;

  snprintf(line1, sizeof(line1), "%s", base ? base : "(no model)");
  snprintf(line2, sizeof(line2),
           "FPS %.1f | Exp %.2f | IBL %s | Debug %s",
           app->fps, app->exposure,
           (app->ibl_ok && app->use_ibl_user) ? "ON" : "OFF",
           debug_mode_name(app->debug_mode));

  HudLine lines[] = {
    { line1, { 255, 255, 255, 255 } },
    { line2, { 200, 220, 255, 255 } },
    { "[F] wire  [Space] auto  [I] ibl  [P] shot  [R] reload  "
      "[F5] shader  [0-7] debug  [Home] reset",
      { 170, 170, 170, 255 } },
  };
  hud_draw(app->hud, (float)fb_w, (float)fb_h, lines, 3);
}

static void render_frame(struct AppState *app) {
  double now = glfwGetTime();
  double dt  = now - app->last_time;
  app->last_time = now;
  if (dt < 0.0) dt = 0.0;
  if (dt > 0.1) dt = 0.1;

  if (app->auto_rotate && !app->dragging)
    app->camera.yaw += (float)dt * 0.4f;

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

  /* Background: hanya kalau bukan mode debug */
  if (app->ibl_ok && app->debug_mode == 0) {
    mat4 vp, inv_vp;
    glm_mat4_mul(proj, view, vp);
    glm_mat4_inv(vp, inv_vp);
    ibl_draw_background(&app->ibl, (const float *)inv_vp, eye,
                        app->exposure);
  }

  glUseProgram(app->program);
  glUniformMatrix4fv(app->u.view, 1, GL_FALSE, (float *)view);
  glUniformMatrix4fv(app->u.proj, 1, GL_FALSE, (float *)proj);
  glUniform3fv(app->u.cam_pos,   1, eye);
  glUniform3fv(app->u.sun_dir,   1, app->sun_dir);
  glUniform3fv(app->u.sun_color, 1, app->sun_color);
  glUniform1f(app->u.exposure,      app->exposure);
  glUniform1f(app->u.vignette,      app->vignette);
  glUniform2f(app->u.screen_size,   (float)fb_w, (float)fb_h);
  glUniform1i(app->u.debug_mode,    app->debug_mode);
  glUniform1i(app->u.use_ibl,
              (app->ibl_ok && app->use_ibl_user) ? 1 : 0);

  if (app->ibl_ok) {
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_CUBE_MAP, app->ibl.irradiance);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_CUBE_MAP, app->ibl.prefilter);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, app->ibl.brdf_lut);
    glActiveTexture(GL_TEXTURE0);
  }

  if (app->wireframe)
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

  render_pass(app, 0);
  render_pass(app, 1);

  if (app->wireframe)
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

  glBindVertexArray(0);

  if (app->screenshot_pending) {
    app->screenshot_pending = 0;
    take_screenshot(app);
  }

  /* HUD paling akhir, sebelum swap */
  glUseProgram(0);
  if (app->hud) draw_hud(app, fb_w, fb_h);

  glfwSwapBuffers(app->window);
}

static void update_fps(struct AppState *app) {
  double now = glfwGetTime();
  app->fps_frames++;
  if (now - app->fps_last >= 0.5) {
    app->fps = (float)(app->fps_frames / (now - app->fps_last));
    app->fps_last = now;
    app->fps_frames = 0;

    char title[192];
    snprintf(title, sizeof(title),
             "GLB Viewer — %.1f FPS — exp %.2f",
             app->fps, app->exposure);
    glfwSetWindowTitle(app->window, title);
  }
}

/* ================================================================== */
/*  Public API                                                        */
/* ================================================================== */
struct AppState *app_create(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  if (argc < 2) {
    log_error("Usage: %s <model.glb> [env.hdr]", argv[0]);
    return NULL;
  }

  struct AppState *app = (struct AppState *)calloc(1, sizeof(struct AppState));
  if (!app) return NULL;

  /* default (di-override config kalau ada) */
  ViewerConfig cfg;
  config_defaults(&cfg);
  if (config_load(&cfg, CONFIG_FILE))
    log_info("Config dimuat: %s", CONFIG_FILE);
  apply_config(app, &cfg);

  app->last_time  = glfwGetTime();
  app->model_path = strdup(argv[1]);
  if (argc >= 3) app->hdr_path = strdup(argv[2]);

  if (!glfwInit()) {
    log_error("glfwInit gagal");
    free(app->model_path); free(app->hdr_path); free(app);
    return NULL;
  }

  app->window = create_window();
  if (!app->window) {
    log_error("Gagal membuat window GLFW");
    glfwTerminate();
    free(app->model_path); free(app->hdr_path); free(app);
    return NULL;
  }
  glfwMakeContextCurrent(app->window);
  glfwSwapInterval(1);
  glfwSetWindowUserPointer(app->window, app);
  glfwSetMouseButtonCallback(app->window, cb_mouse_button);
  glfwSetCursorPosCallback(app->window,   cb_cursor_pos);
  glfwSetScrollCallback(app->window,      cb_scroll);
  glfwSetKeyCallback(app->window,         cb_key);
  glfwSetDropCallback(app->window,        cb_drop);

  if (!setup_gl()) { app_destroy(app); return NULL; }
  setup_gl_state();

  if (!setup_shader(app)) {
    log_error("Gagal compile shader PBR (cek %s / %s)",
              SHADER_VS, SHADER_FS);
    app_destroy(app);
    return NULL;
  }
  setup_ibl_internal(app);

  if (!load_model_and_camera(app, argv[1])) {
    app_destroy(app);
    return NULL;
  }
  setup_default_material(app);

  app->hud = hud_create();
  if (!app->hud)
    log_warn("HUD gagal dibuat; lanjut tanpa overlay");

  return app;
}

void app_run(struct AppState *app) {
  if (!app || !app->window) return;
  while (!glfwWindowShouldClose(app->window)) {
    render_frame(app);
    update_fps(app);
    glfwPollEvents();
  }
}

void app_destroy(struct AppState *app) {
  if (!app) return;

  /* Auto-save config saat keluar */
  ViewerConfig cfg;
  snapshot_config(app, &cfg);
  if (config_save(&cfg, CONFIG_FILE))
    log_info("Config disimpan: %s", CONFIG_FILE);

  if (app->program) glDeleteProgram(app->program);
  model_free(&app->model);
  ibl_free(&app->ibl);
  hud_destroy(app->hud);

  if (app->window) {
    glfwDestroyWindow(app->window);
    glfwTerminate();
  }
  free(app->model_path);
  free(app->hdr_path);
  free(app);
}