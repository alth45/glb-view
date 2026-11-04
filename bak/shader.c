#include "shader.h"
#include <stdio.h>

static GLuint compile(GLenum type, const char *src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, NULL);
  glCompileShader(s);

  GLint ok = GL_FALSE;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048];
    glGetShaderInfoLog(s, sizeof(log), NULL, log);
    fprintf(stderr, "[shader] compile error:\n%s\n", log);
    glDeleteShader(s);
    return 0;
  }
  return s;
}

GLuint shader_build(const char *vs_src, const char *fs_src) {
  GLuint vs = compile(GL_VERTEX_SHADER, vs_src);
  GLuint fs = compile(GL_FRAGMENT_SHADER, fs_src);
  if (!vs || !fs)
    return 0;

  GLuint p = glCreateProgram();
  glAttachShader(p, vs);
  glAttachShader(p, fs);
  glLinkProgram(p);

  GLint ok = GL_FALSE;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[2048];
    glGetProgramInfoLog(p, sizeof(log), NULL, log);
    fprintf(stderr, "[shader] link error:\n%s\n", log);
    glDeleteProgram(p);
    p = 0;
  }
  glDeleteShader(vs);
  glDeleteShader(fs);
  return p;
}