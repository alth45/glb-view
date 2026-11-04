#include "camera/camera.h"
#include <math.h>

void camera_init(OrbitCamera *cam, vec3 target, float scene_radius) {
  glm_vec3_copy(target, cam->target);
  cam->distance = scene_radius * 2.5f;
  if (cam->distance < 0.5f) cam->distance = 0.5f;
  cam->yaw   = 0.0f;
  cam->pitch = 0.3f;
  cam->fov   = 45.0f;

  cam->near_plane = scene_radius * 0.01f;
  if (cam->near_plane < 0.001f) cam->near_plane = 0.001f;
  cam->far_plane = scene_radius * 100.0f;
  if (cam->far_plane < 10.0f) cam->far_plane = 10.0f;
}

void camera_get_eye(const OrbitCamera *cam, vec3 out) {
  float cp = cosf(cam->pitch);
  out[0] = cam->target[0] + cam->distance * cp * sinf(cam->yaw);
  out[1] = cam->target[1] + cam->distance * sinf(cam->pitch);
  out[2] = cam->target[2] + cam->distance * cp * cosf(cam->yaw);
}

void camera_get_view(const OrbitCamera *cam, vec3 up, mat4 out) {
  vec3 eye;
  camera_get_eye(cam, eye);
  glm_lookat(eye, cam->target, up, out);
}

void camera_get_projection(const OrbitCamera *cam, float aspect, mat4 out) {
  glm_perspective(glm_rad(cam->fov), aspect,
                  cam->near_plane, cam->far_plane, out);
}

void camera_orbit(OrbitCamera *cam, float dx_pixels, float dy_pixels) {
  cam->yaw   -= dx_pixels * 0.01f;
  cam->pitch += dy_pixels * 0.01f;
  if (cam->pitch >  1.5f) cam->pitch =  1.5f;
  if (cam->pitch < -1.5f) cam->pitch = -1.5f;
}

void camera_zoom(OrbitCamera *cam, float scroll_delta) {
  cam->distance *= powf(0.9f, scroll_delta);
  if (cam->distance < 0.05f)   cam->distance = 0.05f;
  if (cam->distance > 5000.0f) cam->distance = 5000.0f;
}