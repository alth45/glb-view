#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV0;
in vec2 vUV1;
out vec4 FragColor;

const float PI = 3.14159265359;

uniform vec4  uBaseColor;
uniform float uMetallic;
uniform float uRoughness;
uniform vec3  uEmissive;
uniform float uNormalScale;
uniform float uOcclusionStr;
uniform float uAlphaCutoff;
uniform int   uAlphaMode;
uniform int   uHasAlbedo, uHasMR, uHasNormal, uHasOcc, uHasEmissive;
uniform int   uUVAlbedo, uUVMR, uUVNormal, uUVOcc, uUVEmis;
uniform sampler2D uAlbedoTex;
uniform sampler2D uMrTex;
uniform sampler2D uNormalTex;
uniform sampler2D uOcclusionTex;
uniform sampler2D uEmissiveTex;

uniform vec3  uCamPos;
uniform vec3  uSunDir;
uniform vec3  uSunColor;
uniform float uExposure;
uniform int   uUseIBL;
uniform samplerCube uIrradiance;
uniform samplerCube uPrefilter;
uniform sampler2D   uBrdfLUT;

uniform int   uDebugMode;
uniform float uVignette;
uniform vec2  uScreenSize;

vec2 pickUV(int set) { return set == 0 ? vUV0 : vUV1; }

float distributionGGX(vec3 N, vec3 H, float roughness) {
  float a = roughness*roughness, a2 = a*a;
  float NdotH = max(dot(N,H), 0.0);
  float d = NdotH*NdotH*(a2-1.0) + 1.0;
  return a2 / (PI*d*d + 1e-6);
}
float geomSchlick(float NdotX, float k) { return NdotX/(NdotX*(1.0-k)+k); }
vec3 fresnelSchlick(float VdotH, vec3 F0) {
  return F0 + (1.0-F0)*pow(clamp(1.0-VdotH,0.0,1.0),5.0);
}
vec3 fresnelSchlickRoughness(float NdotV, vec3 F0, float r) {
  vec3 fr = max(vec3(1.0-r), F0);
  return F0 + (fr-F0)*pow(clamp(1.0-NdotV,0.0,1.0),5.0);
}
vec3 perturbNormal(vec3 N, vec3 pos, vec2 uv, vec3 mapN) {
  vec3 dp1 = dFdx(pos), dp2 = dFdy(pos);
  vec2 duv1 = dFdx(uv),  duv2 = dFdy(uv);
  vec3 dp2p = cross(dp2, N), dp1p = cross(N, dp1);
  vec3 T = dp2p*duv1.x + dp1p*duv2.x;
  vec3 B = dp2p*duv1.y + dp1p*duv2.y;
  float im = inversesqrt(max(dot(T,T), dot(B,B)));
  return normalize(mat3(T*im, B*im, N) * mapN);
}
vec3 aces(vec3 x) {
  const float a=2.51, b=0.03, c=2.43, d=0.59, e=0.14;
  return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

void main() {
  vec4 base = uBaseColor;
  if (uHasAlbedo == 1) base *= texture(uAlbedoTex, pickUV(uUVAlbedo));
  if (uAlphaMode == 1 && base.a < uAlphaCutoff) discard;

  vec3 albedo = base.rgb;
  float metallic = uMetallic, roughness = uRoughness;
  if (uHasMR == 1) {
    vec3 mr = texture(uMrTex, pickUV(uUVMR)).rgb;
    roughness *= mr.g;
    metallic  *= mr.b;
  }
  roughness = clamp(roughness, 0.045, 1.0);
  metallic  = clamp(metallic, 0.0, 1.0);

  vec3 N = normalize(vNormal);
  if (uHasNormal == 1) {
    vec3 m = texture(uNormalTex, pickUV(uUVNormal)).xyz * 2.0 - 1.0;
    m.xy *= uNormalScale;
    N = perturbNormal(N, vWorldPos, pickUV(uUVNormal), normalize(m));
  }

  float ao = 1.0;
  if (uHasOcc == 1) {
    float occ = texture(uOcclusionTex, pickUV(uUVOcc)).r;
    ao = mix(1.0, occ, uOcclusionStr);
  }

  vec3 emissive = uEmissive;
  if (uHasEmissive == 1)
    emissive *= texture(uEmissiveTex, pickUV(uUVEmis)).rgb;

  /* ---- Debug views ---- */
  if (uDebugMode == 1) { FragColor = vec4(albedo, 1.0); return; }
  if (uDebugMode == 2) { FragColor = vec4(N * 0.5 + 0.5, 1.0); return; }
  if (uDebugMode == 3) { FragColor = vec4(vec3(metallic), 1.0); return; }
  if (uDebugMode == 4) { FragColor = vec4(vec3(roughness), 1.0); return; }
  if (uDebugMode == 5) { FragColor = vec4(vec3(ao), 1.0); return; }
  if (uDebugMode == 6) { FragColor = vec4(emissive, 1.0); return; }
  if (uDebugMode == 7) { FragColor = vec4(fract(vUV0), 0.0, 1.0); return; }

  /* ---- Lighting ---- */
  vec3 V = normalize(uCamPos - vWorldPos);
  float NdotV = max(dot(N, V), 1e-4);
  vec3 F0 = mix(vec3(0.04), albedo, metallic);

  vec3 L = normalize(uSunDir), H = normalize(V + L);
  float NdotL = max(dot(N, L), 0.0);
  vec3 direct = vec3(0.0);
  if (NdotL > 0.0) {
    float VdotH = max(dot(V, H), 0.0);
    vec3  F = fresnelSchlick(VdotH, F0);
    float D = distributionGGX(N, H, roughness);
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float G = geomSchlick(NdotV, k) * geomSchlick(NdotL, k);
    vec3 spec = D * G * F / (4.0 * NdotV * NdotL + 1e-4);
    vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);
    direct = (kd * albedo / PI + spec) * NdotL * uSunColor;
  }

  vec3 ambient;
  if (uUseIBL == 1) {
    vec3 R = reflect(-V, N);
    vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diff = textureLod(uIrradiance, N, 0.0).rgb;
    vec3 pre  = textureLod(uPrefilter, R, roughness * 4.0).rgb;
    vec2 brdf = textureLod(uBrdfLUT, vec2(NdotV, roughness), 0.0).rg;
    ambient = (kd * diff * albedo + pre * (F0 * brdf.x + brdf.y)) * ao;
  } else {
    ambient = vec3(0.12, 0.13, 0.15) * albedo * ao;
  }

  vec3 color = ambient + direct + emissive;
  color = aces(color * uExposure);
  color = pow(color, vec3(1.0 / 2.2));

  /* ---- Vignette ---- */
  vec2 vp = (gl_FragCoord.xy / uScreenSize - 0.5) * 2.0;
  float vig = 1.0 - dot(vp, vp) * 0.5 * uVignette;
  color *= clamp(vig, 0.0, 1.0);

  FragColor = vec4(color, uAlphaMode == 2 ? base.a : 1.0);
}