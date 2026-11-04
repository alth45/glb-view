#include "gfx/shader.h"
#include "core/log.h"
#include <stdio.h>
#include <string.h>

static GLuint compile(GLenum type, const char *src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, NULL);
  glCompileShader(s);

  GLint ok = GL_FALSE;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096] = {0};
    GLsizei log_len = 0;
    glGetShaderInfoLog(s, (GLsizei)sizeof(log), &log_len, log);

    const char *kind = (type == GL_VERTEX_SHADER) ? "vertex" : "fragment";

    if (log_len > 0 && log[0] != '\0') {
      log_error("[shader] %s compile error:\n%s", kind, log);
    } else {
      /* Driver tidak memberi info — dump source untuk debug */
      char snippet[201];
      size_t n = strlen(src);
      if (n > 200) n = 200;
      memcpy(snippet, src, n);
      snippet[n] = '\0';
      log_error("[shader] %s compile error (driver gave no log). "
                "Source starts with:\n---\n%s\n---",
                kind, snippet);
    }

    glDeleteShader(s);
    return 0;
  }
  return s;
}

GLuint shader_build(const char *vs_src, const char *fs_src) {
  GLuint vs = compile(GL_VERTEX_SHADER, vs_src);
  GLuint fs = compile(GL_FRAGMENT_SHADER, fs_src);
  if (!vs || !fs) {
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    return 0;
  }

  GLuint p = glCreateProgram();
  glAttachShader(p, vs);
  glAttachShader(p, fs);
  glLinkProgram(p);

  GLint ok = GL_FALSE;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[4096] = {0};
    GLsizei log_len = 0;
    glGetProgramInfoLog(p, (GLsizei)sizeof(log), &log_len, log);
    if (log_len > 0 && log[0] != '\0') {
      log_error("[shader] link error:\n%s", log);
    } else {
      log_error("[shader] link error (driver gave no log)");
    }
    glDeleteProgram(p);
    p = 0;
  }
  glDeleteShader(vs);
  glDeleteShader(fs);
  return p;
}