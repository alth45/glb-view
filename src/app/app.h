#ifndef APP_H
#define APP_H

typedef struct AppState AppState;

/* Return NULL kalau init gagal (argumen tidak valid, GLFW/GL/model gagal). */
AppState *app_create(int argc, char **argv);

/* Blok sampai window ditutup. */
void app_run(AppState *app);

/* Bebaskan semua resource; aman dipanggil pada state parsial. */
void app_destroy(AppState *app);

#endif