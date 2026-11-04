#include "app/app.h"

int main(int argc, char **argv) {
  AppState *app = app_create(argc, argv);
  if (!app) return 1;
  app_run(app);
  app_destroy(app);
  return 0;
}