#include "gl_loader.h"
/* Proyek memakai loader GL sendiri (gl_loader.h) — cegah GLFW menarik
 * <GL/gl.h> bawaan yang mendefinisikan ulang fungsi GL. */
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cglm/cglm.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "gltf_model.h"
#include "pbr_ibl.h"
#include "shader.h"

/* ================================================================== */
/*  Shader PBR (Cook-Torrance + IBL split-sum)                         */
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
/*  Orbit camera                                                      */
/* ================================================================== */
typedef struct {
  vec3 target;
  float distance;
  float yaw;
  float pitch;
  float fov;
} OrbitCamera;

static OrbitCamera g_cam = {
    .target = {0, 0, 0},
    .distance = 3.0f,
    .yaw = 0.0f,
    .pitch = 0.3f,
    .fov = 45.0f,
};

/* FPS counter */
static double g_fps_last   = 0.0;
static int    g_fps_frames = 0;
static char   g_title[128];


static int g_dragging = 0;
static double g_last_x = 0, g_last_y = 0;

static void cb_mouse_button(GLFWwindow *w, int button, int action, int mods) {
  (void)mods;
  if (button == GLFW_MOUSE_BUTTON_LEFT) {
    g_dragging = (action == GLFW_PRESS);
    glfwGetCursorPos(w, &g_last_x, &g_last_y);
  }
}

static void cb_cursor_pos(GLFWwindow *w, double x, double y) {
  (void)w;
  if (!g_dragging)
    return;
  float dx = (float)(x - g_last_x);
  float dy = (float)(y - g_last_y);
  g_last_x = x;
  g_last_y = y;

  g_cam.yaw -= dx * 0.01f;
  g_cam.pitch += dy * 0.01f;
  if (g_cam.pitch > 1.5f)
    g_cam.pitch = 1.5f;
  if (g_cam.pitch < -1.5f)
    g_cam.pitch = -1.5f;
}

static void cb_scroll(GLFWwindow *w, double xo, double yo) {
  (void)w;
  (void)xo;
  g_cam.distance *= (float)pow(0.9, yo);
  if (g_cam.distance < 0.05f)
    g_cam.distance = 0.05f;
  if (g_cam.distance > 5000.0f)
    g_cam.distance = 5000.0f;
}

/* ================================================================== */
/*  Entry point                                                       */
/* ================================================================== */
static void *get_proc(const char *name) {
  return (void *)glfwGetProcAddress(name);
}

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0); /* log langsung terbaca saat di-redirect */

  if (argc < 2) {
    fprintf(stderr, "Usage: %s <model.glb> [env.hdr]\n", argv[0]);
    return 1;
  }

  /* ---- GLFW ---- */
  if (!glfwInit()) {
    fprintf(stderr, "glfwInit gagal\n");
    return 1;
  }

  /* Minta GL 4.1 dulu (float render target core utk IBL); fallback ke 3.3. */
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_SAMPLES, 4);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

  GLFWwindow *window = glfwCreateWindow(1280, 720, "GLB Viewer", NULL, NULL);
  if (!window) {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    window = glfwCreateWindow(1280, 720, "GLB Viewer", NULL, NULL);
  }
  if (!window) {
    fprintf(stderr, "Gagal membuat window GLFW\n");
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  if (!gl_load(get_proc)) {
    fprintf(stderr, "Gagal memuat fungsi OpenGL\n");
    return 1;
  }

  printf("OpenGL : %s\n", (const char *)glGetString(GL_VERSION));
  printf("Vendor : %s\n", (const char *)glGetString(GL_RENDERER));
  /* stderr (unbuffered) agar terbaca walau stdout di-redirect */
  fprintf(stderr, "[gl] %s | %s\n", (const char *)glGetString(GL_VERSION),
          (const char *)glGetString(GL_RENDERER));

  glfwSetMouseButtonCallback(window, cb_mouse_button);
  glfwSetCursorPosCallback(window, cb_cursor_pos);
  glfwSetScrollCallback(window, cb_scroll);

  /* ---- GL state ---- */
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_MULTISAMPLE);
  glDepthFunc(0x0201, GL_LESS);

  /* ---- IBL: environment -> cubemap -> irradiance/prefilter/BRDF ---- */
  IblContext ibl;
  int env_w = 0, env_h = 0;
  float *env_data = NULL;

  if (argc >= 3) {
    env_data = ibl_env_load_hdr(argv[2], &env_w, &env_h);
    if (env_data)
      printf("Environment HDR: %s (%dx%d)\n", argv[2], env_w, env_h);
    else
      fprintf(stderr, "Gagal memuat .hdr: %s (pakai prosedural)\n", argv[2]);
  }
  if (!env_data)
    env_data = ibl_env_procedural(&env_w, &env_h);

  int ibl_ok = env_data && ibl_build(&ibl, env_data, env_w, env_h);
  free(env_data);
  if (!ibl_ok)
    fprintf(stderr,
            "[ibl] bake gagal (float RT mungkin tidak didukung); "
            "memakai ambient konstan\n");

  /* ---- Shader PBR ---- */
  GLuint program = shader_build(VS_SRC, FS_SRC);
  if (!program)
    return 1;

  glUseProgram(program);
  /* Unit tetap: 0..4 material, 5 irradiance, 6 prefilter, 7 BRDF LUT */
  glUniform1i(glGetUniformLocation(program, "uAlbedoTex"), 0);
  glUniform1i(glGetUniformLocation(program, "uMrTex"), 1);
  glUniform1i(glGetUniformLocation(program, "uNormalTex"), 2);
  glUniform1i(glGetUniformLocation(program, "uOcclusionTex"), 3);
  glUniform1i(glGetUniformLocation(program, "uEmissiveTex"), 4);
  glUniform1i(glGetUniformLocation(program, "uIrradiance"), 5);
  glUniform1i(glGetUniformLocation(program, "uPrefilter"), 6);
  glUniform1i(glGetUniformLocation(program, "uBrdfLUT"), 7);
  glUniform1i(glGetUniformLocation(program, "uUseIBL"), ibl_ok ? 1 : 0);

  /* Sun: arah sama dengan atmosphere prosedural */
  vec3 sun_dir = {0.5f, 1.0f, 0.3f};
  glm_vec3_normalize(sun_dir);
  vec3 sun_color = {3.0f, 2.85f, 2.55f};
  glUniform3fv(glGetUniformLocation(program, "uSunDir"), 1, sun_dir);
  glUniform3fv(glGetUniformLocation(program, "uSunColor"), 1, sun_color);
  glUniform1f(glGetUniformLocation(program, "uExposure"), 1.0f);

  /* ---- Load model ---- */
  Model model;
  if (!model_load(&model, argv[1])) {
    fprintf(stderr, "Gagal memuat model: %s\n", argv[1]);
    return 1;
  }
  printf("Model dimuat: %zu mesh, %zu material, radius=%.3f\n",
         model.mesh_count, model.material_count, model.radius);

  /* Auto-fit kamera */
  glm_vec3_copy(model.center, g_cam.target);
  g_cam.distance = model.radius * 2.5f;
  if (g_cam.distance < 0.5f)
    g_cam.distance = 0.5f;

  /* ---- Render loop ---- */
  vec3 up = {0.0f, 1.0f, 0.0f};

  struct {
    GLint view, proj, cam_pos;
    GLint base_color, metallic, roughness, emissive;
    GLint normal_scale, occlusion_str, alpha_cutoff, alpha_mode;
    GLint has_albedo, has_mr, has_normal, has_occ, has_emissive;
    GLint uv_albedo, uv_mr, uv_normal, uv_occ, uv_emis;
  } U = {
      .view = glGetUniformLocation(program, "uView"),
      .proj = glGetUniformLocation(program, "uProj"),
      .cam_pos = glGetUniformLocation(program, "uCamPos"),
      .base_color = glGetUniformLocation(program, "uBaseColor"),
      .metallic = glGetUniformLocation(program, "uMetallic"),
      .roughness = glGetUniformLocation(program, "uRoughness"),
      .emissive = glGetUniformLocation(program, "uEmissive"),
      .normal_scale = glGetUniformLocation(program, "uNormalScale"),
      .occlusion_str = glGetUniformLocation(program, "uOcclusionStr"),
      .alpha_cutoff = glGetUniformLocation(program, "uAlphaCutoff"),
      .alpha_mode = glGetUniformLocation(program, "uAlphaMode"),
      .has_albedo = glGetUniformLocation(program, "uHasAlbedo"),
      .has_mr = glGetUniformLocation(program, "uHasMR"),
      .has_normal = glGetUniformLocation(program, "uHasNormal"),
      .has_occ = glGetUniformLocation(program, "uHasOcc"),
      .has_emissive = glGetUniformLocation(program, "uHasEmissive"),
      .uv_albedo = glGetUniformLocation(program, "uUVAlbedo"),
      .uv_mr = glGetUniformLocation(program, "uUVMR"),
      .uv_normal = glGetUniformLocation(program, "uUVNormal"),
      .uv_occ = glGetUniformLocation(program, "uUVOcc"),
      .uv_emis = glGetUniformLocation(program, "uUVEmis"),
  };

  Material default_mat;
  memset(&default_mat, 0, sizeof(default_mat));
  default_mat.base_color[0] = default_mat.base_color[1] =
      default_mat.base_color[2] = default_mat.base_color[3] = 0.8f;
  default_mat.roughness = 0.8f;
  default_mat.normal_scale = 1.0f;
  default_mat.occlusion_str = 1.0f;
  default_mat.alpha_cutoff = 0.5f;

  while (!glfwWindowShouldClose(window)) {
    int fb_w = 0, fb_h = 0;
    glfwGetFramebufferSize(window, &fb_w, &fb_h);
    glViewport(0, 0, fb_w, fb_h);

    glClearColor(0.09f, 0.09f, 0.11f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* Eye position */
    float cp = cosf(g_cam.pitch);
    vec3 eye = {
        g_cam.target[0] + g_cam.distance * cp * sinf(g_cam.yaw),
        g_cam.target[1] + g_cam.distance * sinf(g_cam.pitch),
        g_cam.target[2] + g_cam.distance * cp * cosf(g_cam.yaw),
    };

    mat4 view, proj;
    glm_lookat(eye, g_cam.target, up, view);
    float aspect = (fb_h > 0) ? (float)fb_w / (float)fb_h : 1.0f;

    float near_p = model.radius * 0.01f;
    if (near_p < 0.001f)
      near_p = 0.001f;
    float far_p = model.radius * 100.0f;
    if (far_p < 10.0f)
      far_p = 10.0f;
    glm_perspective(glm_rad(g_cam.fov), aspect, near_p, far_p, proj);

    /* Latar belakang: env cubemap (fullscreen ray) */
    if (ibl_ok) {
      mat4 vp, inv_vp;
      glm_mat4_mul(proj, view, vp);
      glm_mat4_inv(vp, inv_vp);
      ibl_draw_background(&ibl, (const float *)inv_vp, eye, 1.0f);
    }

    glUseProgram(program);
    glUniformMatrix4fv(U.view, 1, GL_FALSE, (float *)view);
    glUniformMatrix4fv(U.proj, 1, GL_FALSE, (float *)proj);
    glUniform3fv(U.cam_pos, 1, eye);

    /* Bind tekstur IBL (unit 5/6/7) */
    if (ibl_ok) {
      glActiveTexture(GL_TEXTURE5);
      glBindTexture(GL_TEXTURE_CUBE_MAP, ibl.irradiance);
      glActiveTexture(GL_TEXTURE6);
      glBindTexture(GL_TEXTURE_CUBE_MAP, ibl.prefilter);
      glActiveTexture(GL_TEXTURE7);
      glBindTexture(GL_TEXTURE_2D, ibl.brdf_lut);
      glActiveTexture(GL_TEXTURE0);
    }

    for (int pass = 0; pass < 2; pass++) {
      if (pass == 1) {
        /* Transparan (BLEND): sesudah opaque, depth write dimatikan */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
      }

      for (size_t i = 0; i < model.mesh_count; i++) {
        Mesh *m = &model.meshes[i];
        if (m->index_count == 0)
          continue;

        Material *mat = &default_mat;
        if (m->material_index >= 0 &&
            (size_t)m->material_index < model.material_count)
          mat = &model.materials[m->material_index];

        int is_blend = mat->alpha_mode == 2;
        if ((pass == 0 && is_blend) || (pass == 1 && !is_blend))
          continue;

        /* Set material uniform */
        glUniform4fv(U.base_color, 1, mat->base_color);
        glUniform1f(U.metallic, mat->metallic);
        glUniform1f(U.roughness, mat->roughness);
        glUniform3fv(U.emissive, 1, mat->emissive);
        glUniform1f(U.normal_scale, mat->normal_scale);
        glUniform1f(U.occlusion_str, mat->occlusion_str);
        glUniform1f(U.alpha_cutoff, mat->alpha_cutoff);
        glUniform1i(U.alpha_mode, mat->alpha_mode);
        glUniform1i(U.has_albedo, mat->albedo_tex ? 1 : 0);
        glUniform1i(U.has_mr, mat->mr_tex ? 1 : 0);
        glUniform1i(U.has_normal, mat->normal_tex ? 1 : 0);
        glUniform1i(U.has_occ, mat->occlusion_tex ? 1 : 0);
        glUniform1i(U.has_emissive, mat->emissive_tex ? 1 : 0);
        glUniform1i(U.uv_albedo, mat->uv_albedo);
        glUniform1i(U.uv_mr, mat->uv_mr);
        glUniform1i(U.uv_normal, mat->uv_normal);
        glUniform1i(U.uv_occ, mat->uv_occlusion);
        glUniform1i(U.uv_emis, mat->uv_emissive);

        /* Bind tekstur material (unit 0..4) */
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, mat->albedo_tex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, mat->mr_tex);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, mat->normal_tex);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, mat->occlusion_tex);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, mat->emissive_tex);
        glActiveTexture(GL_TEXTURE0);

        glBindVertexArray(m->vao);
        glDrawElements(GL_TRIANGLES, m->index_count, GL_UNSIGNED_INT, 0);
      }

      if (pass == 1) {
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
      }
    }
    glBindVertexArray(0);

    glfwSwapBuffers(window);

        /* FPS counter -> judul window */
    double now = glfwGetTime();
    g_fps_frames++;
    if (now - g_fps_last >= 0.5) {
      double fps = g_fps_frames / (now - g_fps_last);
      g_fps_last   = now;
      g_fps_frames = 0;
      snprintf(g_title, sizeof(g_title),
               "GLB Viewer — %s — %.1f FPS",
               argv[1], fps);
      glfwSetWindowTitle(window, g_title);
    }
    
    glfwPollEvents();

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
      glfwSetWindowShouldClose(window, GLFW_TRUE);
  }

  model_free(&model);
  ibl_free(&ibl);
  glDeleteProgram(program);
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}