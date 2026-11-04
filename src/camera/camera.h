#ifndef CAMERA_H
#define CAMERA_H

#include <cglm/cglm.h>

typedef struct {
  vec3  target;
  float distance;
  float yaw;        /* radian */
  float pitch;      /* radian, di-clamp ±1.5 */
  float fov;        /* derajat */
  float near_plane;
  float far_plane;
} OrbitCamera;

void camera_init(OrbitCamera *cam, vec3 target, float scene_radius);
void camera_get_eye(const OrbitCamera *cam, vec3 out);
void camera_get_view(const OrbitCamera *cam, vec3 up, mat4 out);
void camera_get_projection(const OrbitCamera *cam, float aspect, mat4 out);
void camera_orbit(OrbitCamera *cam, float dx_pixels, float dy_pixels);
void camera_zoom(OrbitCamera *cam, float scroll_delta);

#endif