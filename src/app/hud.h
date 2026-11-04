#ifndef APP_HUD_H
#define APP_HUD_H

typedef struct Hud Hud;

typedef struct {
  const char   *text;
  unsigned char rgba[4];
} HudLine;

Hud *hud_create(void);
void hud_destroy(Hud *hud);

/* Gambar sekumpulan baris teks di pojok kiri atas.
 * Panggil terakhir di render_frame (sebelum swap). */
void hud_draw(Hud *hud, float screen_w, float screen_h,
              const HudLine *lines, int n_lines);

#endif