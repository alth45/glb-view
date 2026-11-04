#include "app/hud.h"
#include "gfx/shader.h"
#include "gl/gl_loader.h"
#include "core/log.h"

#include "stb_easy_font.h"

#include <stdlib.h>
#include <string.h>
// #include <gl.h>

#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif

#define HUD_MAX_QUADS 4096
#define HUD_BYTES     (HUD_MAX_QUADS * 64) /* 4 verts * 16 B per quad */

struct Hud {
  GLuint program;
  GLuint vao, vbo, ibo;
  GLint  u_screen_size;
  GLint  u_scale;
  char   buf[HUD_BYTES];
};

static const char *HUD_VS =
  "#version 330 core\n"
  "layout(location=0) in vec2 aPos;\n"
  "layout(location=1) in vec4 aColor;\n"
  "uniform vec2 uScreenSize;\n"
  "uniform float uScale;\n"
  "out vec4 vColor;\n"
  "void main(){\n"
  "  vec2 p = (aPos * uScale) / uScreenSize;\n"
  "  p = p * 2.0 - 1.0;\n"
  "  p.y = -p.y;\n"
  "  gl_Position = vec4(p, 0.0, 1.0);\n"
  "  vColor = aColor;\n"
  "}\n";

static const char *HUD_FS =
  "#version 330 core\n"
  "in vec4 vColor;\n"
  "out vec4 FragColor;\n"
  "void main(){ FragColor = vColor; }\n";

Hud *hud_create(void) {
  Hud *hud = (Hud *)calloc(1, sizeof(Hud));
  if (!hud) return NULL;

  hud->program = shader_build(HUD_VS, HUD_FS);
  if (!hud->program) { free(hud); return NULL; }
  hud->u_screen_size = glGetUniformLocation(hud->program, "uScreenSize");
  hud->u_scale       = glGetUniformLocation(hud->program, "uScale");

  glGenVertexArrays(1, &hud->vao);
  glBindVertexArray(hud->vao);

  glGenBuffers(1, &hud->vbo);
  glBindBuffer(GL_ARRAY_BUFFER, hud->vbo);
  glBufferData(GL_ARRAY_BUFFER, HUD_BYTES, NULL, GL_DYNAMIC_DRAW);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT,         GL_FALSE, 16, (void*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE,  16, (void*)8);

  /* Index buffer (static): quad -> 2 triangles */
  glGenBuffers(1, &hud->ibo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, hud->ibo);
  GLuint *idx = (GLuint *)malloc(sizeof(GLuint) * HUD_MAX_QUADS * 6);
  if (idx) {
    for (int i = 0; i < HUD_MAX_QUADS; i++) {
      GLuint v = (GLuint)(i * 4);
      idx[i*6+0]=v+0; idx[i*6+1]=v+1; idx[i*6+2]=v+2;
      idx[i*6+3]=v+0; idx[i*6+4]=v+2; idx[i*6+5]=v+3;
    }
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 sizeof(GLuint) * HUD_MAX_QUADS * 6, idx, GL_STATIC_DRAW);
    free(idx);
  }

  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  return hud;
}

void hud_destroy(Hud *hud) {
  if (!hud) return;
  if (hud->program) glDeleteProgram(hud->program);
  if (hud->vbo)     glDeleteBuffers(1, &hud->vbo);
  if (hud->ibo)     glDeleteBuffers(1, &hud->ibo);
  if (hud->vao)     glDeleteVertexArrays(1, &hud->vao);
  free(hud);
}

void hud_draw(Hud *hud, float screen_w, float screen_h,
              const HudLine *lines, int n_lines) {
  if (!hud || !lines || n_lines <= 0) return;

  int byte_offset = 0;
  const float scale = 1.5f;

  for (int i = 0; i < n_lines; i++) {
    if (byte_offset >= HUD_BYTES) break;
    /* stb_easy_font: y per baris ~ 12 px; kita pakai scale 1.5 -> 18 px spacing */
    float y = 6.0f + (float)i * 12.0f;
    int q = stb_easy_font_print(
        6.0f, y, (char *)lines[i].text, lines[i].rgba,
        hud->buf + byte_offset, HUD_BYTES - byte_offset);
    if (q > 0) byte_offset += q * 64;
  }
  if (byte_offset == 0) return;
  int total_quads = byte_offset / 64;

  glUseProgram(hud->program);
  glUniform2f(hud->u_screen_size, screen_w, screen_h);
  glUniform1f(hud->u_scale, scale);

  glBindVertexArray(hud->vao);
  glBindBuffer(GL_ARRAY_BUFFER, hud->vbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0, byte_offset, hud->buf);

  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glDrawElements(GL_TRIANGLES, total_quads * 6, GL_UNSIGNED_INT, 0);

  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
  glBindVertexArray(0);
}