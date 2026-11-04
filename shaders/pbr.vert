#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV0;
layout(location=3) in vec2 aUV1;
uniform mat4 uView;
uniform mat4 uProj;
out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV0;
out vec2 vUV1;
void main() {
  vWorldPos = aPos;
  vNormal   = aNormal;
  vUV0      = aUV0;
  vUV1      = aUV1;
  gl_Position = uProj * uView * vec4(aPos, 1.0);
}