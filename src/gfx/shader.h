#ifndef SHADER_H
#define SHADER_H
#include "gl/gl_loader.h"
GLuint shader_build(const char *vs_src, const char *fs_src);
#endif