#ifndef GL_LOADER_H
#define GL_LOADER_H

#include <stddef.h>

#ifdef _WIN32
#define GLAPI __stdcall
#else
#define GLAPI
#endif

/* ---------- Tipe dasar OpenGL ---------- */
typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;
typedef unsigned char GLubyte;
typedef float GLfloat;
typedef unsigned int GLbitfield;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
typedef char GLchar;
typedef void GLvoid;

/* ---------- Konstanta ---------- */
#define GL_FALSE 0
#define GL_TRUE  1
#define GL_NO_ERROR 0
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE  0x0B44
#define GL_BACK       0x0405
#define GL_TRIANGLES  0x0004
#define GL_FLOAT      0x1406
#define GL_UNSIGNED_INT  0x1405
#define GL_UNSIGNED_BYTE 0x1401
#define GL_ARRAY_BUFFER  0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_VERTEX_SHADER   0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS  0x8B81
#define GL_LINK_STATUS     0x8B82
#define GL_TEXTURE_2D      0x0DE1
#define GL_TEXTURE0        0x84C0
#define GL_TEXTURE1        0x84C1
#define GL_TEXTURE2        0x84C2
#define GL_TEXTURE3        0x84C3
#define GL_TEXTURE4        0x84C4
#define GL_TEXTURE5        0x84C5
#define GL_TEXTURE6        0x84C6
#define GL_TEXTURE7        0x84C7
#define GL_RGBA            0x1908
#define GL_RGBA8           0x8058
#define GL_TEXTURE_WRAP_S  0x2802
#define GL_TEXTURE_WRAP_T  0x2803
#define GL_REPEAT          0x2901
#define GL_TEXTURE_MIN_FILTER   0x2801
#define GL_TEXTURE_MAG_FILTER   0x2800
#define GL_LINEAR               0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_UNPACK_ALIGNMENT     0x0CF5
#define GL_MULTISAMPLE          0x809D
#define GL_VERSION              0x1F02
#define GL_RENDERER             0x1F01

/* Depth compare */
#define GL_LESS   0x0201
#define GL_LEQUAL 0x0203

/* Framebuffer */
#define GL_FRAMEBUFFER         0x8D40
#define GL_COLOR_ATTACHMENT0   0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5

/* Format tekstur */
#define GL_RGBA16F      0x881A
#define GL_RG16F        0x822F
#define GL_SRGB8_ALPHA8 0x8C43

/* Cubemap */
#define GL_TEXTURE_CUBE_MAP            0x8513
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515

/* Wrap / level */
#define GL_CLAMP_TO_EDGE      0x812F
#define GL_TEXTURE_BASE_LEVEL 0x813C
#define GL_TEXTURE_MAX_LEVEL  0x813D

/* Blend */
#define GL_BLEND 0x0BE2
#define GL_ZERO  0
#define GL_ONE   1
#define GL_SRC_ALPHA            0x0302
#define GL_ONE_MINUS_SRC_ALPHA  0x0303


/* Depth compare */
#define GL_LESS   0x0201
#define GL_LEQUAL 0x0203

/* Polygon mode */
#define GL_FRONT_AND_BACK 0x0408
#define GL_POINT          0x1B00
#define GL_LINE           0x1B01
#define GL_FILL           0x1B02

/* Pixel store / read */
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_RGB            0x1907

/* ---------- Daftar fungsi GL ---------- */
#define GL_FUNCTIONS(X)                                                        \
  X(void, glClear, (GLbitfield))                                               \
  X(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))                  \
  X(void, glEnable, (GLenum))                                                  \
  X(void, glDisable, (GLenum))                                                 \
  X(void, glViewport, (GLint, GLint, GLsizei, GLsizei))                        \
  X(GLenum, glGetError, (void))                                                \
  X(const GLubyte *, glGetString, (GLenum))                                    \
  X(void, glDepthFunc, (GLenum))                                               \
  X(void, glCullFace, (GLenum))                                                \
  X(void, glPixelStorei, (GLenum, GLint))                                      \
  X(void, glGenBuffers, (GLsizei, GLuint *))                                   \
  X(void, glBindBuffer, (GLenum, GLuint))                                      \
  X(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum))            \
  X(void, glDeleteBuffers, (GLsizei, const GLuint *))                          \
  X(void, glGenVertexArrays, (GLsizei, GLuint *))                              \
  X(void, glBindVertexArray, (GLuint))                                         \
  X(void, glDeleteVertexArrays, (GLsizei, const GLuint *))                     \
  X(void, glEnableVertexAttribArray, (GLuint))                                 \
  X(void, glVertexAttribPointer,                                               \
    (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *))                 \
  X(void, glDrawElements, (GLenum, GLsizei, GLenum, const void *))             \
  X(void, glGenTextures, (GLsizei, GLuint *))                                  \
  X(void, glBindTexture, (GLenum, GLuint))                                     \
  X(void, glTexImage2D,                                                        \
    (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,            \
     const void *))                                                            \
  X(void, glTexParameteri, (GLenum, GLenum, GLint))                            \
  X(void, glGenerateMipmap, (GLenum))                                          \
  X(void, glActiveTexture, (GLenum))                                           \
  X(void, glDeleteTextures, (GLsizei, const GLuint *))                         \
  X(GLuint, glCreateShader, (GLenum))                                          \
  X(void, glShaderSource,                                                      \
    (GLuint, GLsizei, const GLchar *const *, const GLint *))                   \
  X(void, glCompileShader, (GLuint))                                           \
  X(void, glGetShaderiv, (GLuint, GLenum, GLint *))                            \
  X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))          \
  X(void, glDeleteShader, (GLuint))                                            \
  X(GLuint, glCreateProgram, (void))                                           \
  X(void, glAttachShader, (GLuint, GLuint))                                    \
  X(void, glLinkProgram, (GLuint))                                             \
  X(void, glGetProgramiv, (GLuint, GLenum, GLint *))                           \
  X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))         \
  X(void, glUseProgram, (GLuint))                                              \
  X(void, glDeleteProgram, (GLuint))                                           \
  X(GLint, glGetUniformLocation, (GLuint, const GLchar *))                     \
  X(void, glUniform1i, (GLint, GLint))                                         \
  X(void, glUniform1f, (GLint, GLfloat))                                       \
  X(void, glUniform2fv, (GLint, GLsizei, const GLfloat *))                     \
  X(void, glUniform3fv, (GLint, GLsizei, const GLfloat *))                     \
  X(void, glUniform4fv, (GLint, GLsizei, const GLfloat *))                     \
  X(void, glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *))    \
  X(void, glGenFramebuffers, (GLsizei, GLuint *))                              \
  X(void, glBindFramebuffer, (GLenum, GLuint))                                 \
  X(void, glDeleteFramebuffers, (GLsizei, const GLuint *))                     \
  X(void, glFramebufferTexture2D,                                              \
    (GLenum, GLenum, GLenum, GLuint, GLint))                                   \
  X(void, glFramebufferTextureLayer, (GLenum, GLenum, GLuint, GLint, GLint))   \
  X(GLenum, glCheckFramebufferStatus, (GLenum))                                \
  X(void, glDrawArrays, (GLenum, GLint, GLsizei))                              \
  X(void, glDepthMask, (GLboolean))                                            \
  X(void, glBlendFunc, (GLenum, GLenum))                                       \
  X(void, glUniform2f, (GLint, GLfloat, GLfloat))                              \
  X(void, glBufferSubData, (GLenum, GLintptr, GLsizeiptr, const void *))       \
  X(void, glPolygonMode, (GLenum, GLenum))                                     \
  X(void, glReadPixels,                                                        \
    (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *))

#define GL_DECL(ret, name, args)                                               \
  typedef ret(GLAPI *PFN_##name) args;                                         \
  extern PFN_##name name;
GL_FUNCTIONS(GL_DECL)
#undef GL_DECL

int gl_load(void *(*get_proc)(const char *));

#endif