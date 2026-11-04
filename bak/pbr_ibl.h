#ifndef PBR_IBL_H
#define PBR_IBL_H

#include "gl_loader.h"

/* Hasil bake IBL (dari environment equirectangular HDR). */
typedef struct {
  GLuint env_cubemap; /* konversi equirect -> cubemap (dengan mips) */
  GLuint irradiance;  /* convolution diffuse (32x32x6) */
  GLuint prefilter;   /* specular prefiltered, 5 level mip */
  GLuint brdf_lut;    /* BRDF integration table 512x512 */
  int ok;             /* 1 kalau seluruh rantai berhasil dibake */
} IblContext;

/* Buat data equirect HDR prosedural (langit gradient + ground).
 * Hasil malloc float RGBA (4 channel), bebas dengan free().
 * row 0 = bawah (konvensi GL). */
float *ibl_env_procedural(int *out_w, int *out_h);

/* Muat file .hdr sebagai float RGBA. NULL kalau bukan .hdr / gagal.
 * Hasil malloc, bebas dengan free(). */
float *ibl_env_load_hdr(const char *path, int *out_w, int *out_h);

/* Bake seluruh rantai IBL dari equirect RGBA float.
 * Return 1 kalau sukses (termasuk pemeriksaan float render target). */
int ibl_build(IblContext *ibl, const float *hdr, int w, int h);

/* Gambar latar belakang (env cubemap) sebagai fullscreen ray;
 * inv_view_proj = inverse(projection * view), cam_pos = posisi kamera,
 * exposure = paparan sebelum tone-map. */
void ibl_draw_background(const IblContext *ibl, const float inv_view_proj[16],
                          const float cam_pos[3], float exposure);

/* Bebaskan semua resource IBL. */
void ibl_free(IblContext *ibl);

#endif /* PBR_IBL_H */