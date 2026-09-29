#pragma once
// GLSL source for the scene renderer. Everything is rendered in linear HDR,
// then post-processed (ambient occlusion, bloom, tone mapping, colour grading,
// FXAA) into the final 8-bit image.

namespace Shaders {

// Shared helpers pasted into several shaders: a line reading
// "#pragma gb_common" is replaced with this (see Shader.cpp).
inline const char* common = R"(
vec3 lin(vec3 c) { return pow(max(c, vec3(0.0)), vec3(2.2)); }   // colour picker -> linear
float hash12(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float hash13(vec3 p) { return fract(sin(dot(p, vec3(12.9898, 78.233, 37.719))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), f.x),
               mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 5; ++i) { v += a * vnoise(p); p = p * 2.03 + vec2(1.7, 9.2); a *= 0.5; }
    return v;
}
// Procedural sky colour in a direction (also used for reflections).
vec3 skyGradient(vec3 dir, vec3 zenith, vec3 horizon, vec3 ground) {
    float t = dir.y;
    if (t > 0.0) return mix(horizon, zenith, pow(clamp(t, 0.0, 1.0), 0.45));
    return mix(horizon, ground, pow(clamp(-t, 0.0, 1.0), 0.5));
}
)";

// ---------------------------------------------------------------------------
// Lit geometry
// ---------------------------------------------------------------------------

inline const char* litVert = R"(#version 410 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat3 uNormalMat;

out vec3 vNormal;
out vec3 vWorldPos;
out vec2 vUV;

void main() {
    vUV = aUV;
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos  = world.xyz;
    vNormal    = normalize(uNormalMat * aNormal);
    gl_Position = uProj * uView * world;
}
)";

inline const char* litFrag = R"(#version 410 core
in vec3 vNormal;
in vec3 vWorldPos;
in vec2 vUV;

uniform bool      uUseDecal;   // drawing a Decal: its picture colours the surface
uniform sampler2D uDecal;

uniform vec3  uColor;
uniform int   uMaterial;      // 0 plastic 1 metal 2 neon 3 wood 4 glass 5 concrete 6 ice
uniform float uAlpha;
uniform bool  uSelected;
uniform vec3  uViewPos;
uniform float uTime;

uniform vec3  uSunDir;        // surface -> sun
uniform vec3  uSunColor;
uniform float uSunIntensity;
uniform vec3  uAmbientSky;
uniform vec3  uAmbientGround;
uniform float uAmbientIntensity;
uniform float uReflections;
uniform vec3  uZenith, uHorizon, uGround;

uniform bool  uFogEnabled;
uniform vec3  uFogColor;
uniform float uFogDensity;
uniform float uFogSunGlow;

uniform bool      uShadowsEnabled;
uniform int       uShadowQuality;     // 0 hard, 1 soft, 2 PCSS
uniform float     uShadowSoftness;
uniform float     uShadowStrength;
uniform float     uShadowTexelWorld;  // world size of one shadow-map texel
uniform float     uShadowDepthRange;  // world distance covered by depth 0..1
uniform mat4      uLightSpace;
uniform sampler2D uShadowMap;

#define MAX_LIGHTS 32
uniform int  uLightCount;
uniform vec4 uLightPosRange[MAX_LIGHTS];   // xyz position, w range
uniform vec4 uLightColor[MAX_LIGHTS];      // rgb colour * brightness, w: 0 point 1 spot
uniform vec4 uLightDir[MAX_LIGHTS];        // xyz direction, w cos(half angle)

out vec4 FragColor;
#pragma gb_common
const float PI = 3.14159265;

const vec2 POISSON[16] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2( 0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870), vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845), vec2( 0.97484398,  0.75648379),
    vec2( 0.44323325, -0.97511554), vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023), vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507), vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,  0.78641367), vec2( 0.14383161, -0.14100790));

// 1 = lit, 0 = in shadow. Soft shadows get softer the further the shadow
// is from whatever casts it (contact-hardening, like real sunlight).
float sunVisibility(vec3 N, vec3 L) {
    vec3 wp = vWorldPos + N * uShadowTexelWorld * 1.5;     // normal offset: no acne
    vec4 lc = uLightSpace * vec4(wp, 1.0);
    vec3 p  = lc.xyz / lc.w * 0.5 + 0.5;
    if (p.z > 1.0 || p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return 1.0;

    float bias  = 0.0002 + 0.0006 * (1.0 - max(dot(N, L), 0.0));
    vec2  texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    if (uShadowQuality == 0)
        return texture(uShadowMap, p.xy).r < p.z - bias ? 0.0 : 1.0;

    float a = hash12(gl_FragCoord.xy) * 6.2831853;
    mat2 rot = mat2(cos(a), sin(a), -sin(a), cos(a));

    float radius = 1.0 + 1.5 * uShadowSoftness;          // in texels
    if (uShadowQuality == 2) {
        float search = 3.0 + 8.0 * uShadowSoftness;
        float sum = 0.0; int n = 0;
        for (int i = 0; i < 16; ++i) {
            float d = texture(uShadowMap, p.xy + rot * POISSON[i] * search * texel).r;
            if (d < p.z - bias) { sum += d; ++n; }
        }
        if (n == 0) return 1.0;
        float dist = (p.z - sum / float(n)) * uShadowDepthRange;       // world units
        float penumbra = dist * 0.05 * uShadowSoftness / uShadowTexelWorld;
        radius = clamp(penumbra, 1.0, 24.0);
    }

    float lit = 0.0;
    for (int i = 0; i < 16; ++i) {
        float d = texture(uShadowMap, p.xy + rot * POISSON[i] * radius * texel).r;
        lit += (p.z - bias > d) ? 0.0 : 1.0;
    }
    lit /= 16.0;

    // Fade out towards the edge of the shadow area instead of a hard cut.
    float edge = max(abs(p.x - 0.5), abs(p.y - 0.5)) * 2.0;
    return mix(lit, 1.0, smoothstep(0.85, 1.0, edge));
}

// Cook-Torrance GGX specular + Lambert diffuse.
vec3 shade(vec3 N, vec3 V, vec3 L, vec3 albedo, float rough, float metal, vec3 radiance) {
    vec3  H   = normalize(L + V);
    float NoL = max(dot(N, L), 0.0);
    if (NoL <= 0.0) return vec3(0.0);
    float NoV = max(dot(N, V), 1e-3);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);

    float a  = max(rough * rough, 0.002);
    float a2 = a * a;
    float dd = NoH * NoH * (a2 - 1.0) + 1.0;
    float D  = a2 / (PI * dd * dd);
    float k  = (rough + 1.0) * (rough + 1.0) / 8.0;
    float G  = (NoV / (NoV * (1.0 - k) + k)) * (NoL / (NoL * (1.0 - k) + k));
    vec3  f0 = mix(vec3(0.04), albedo, metal);
    vec3  F  = f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0);

    vec3 spec = D * G * F / (4.0 * NoV * NoL + 1e-4);
    vec3 kd   = (1.0 - F) * (1.0 - metal);
    return (kd * albedo / PI + spec) * radiance * NoL;
}

void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uViewPos - vWorldPos);
    vec3 albedo = lin(uColor);
    float decalAlpha = 1.0;
    if (uUseDecal) {
        vec4 px = texture(uDecal, vUV);
        if (px.a < 0.01) discard;
        albedo *= lin(px.rgb);
        decalAlpha = px.a;
    }

    // --- Material look ---
    // Fine surface detail fades out when it gets smaller than a pixel (no shimmering).
    float detail = clamp(1.5 - length(fwidth(vWorldPos)) * 24.0, 0.0, 1.0);
    float rough = 0.5, metal = 0.0;
    if (uMaterial == 1) { rough = 0.3; metal = 1.0;
        albedo *= mix(1.0, 0.92 + 0.08 * vnoise(vec2(dot(vWorldPos.xz, vec2(1.0)) * 60.0, vWorldPos.y * 2.0)), detail * 0.5); }
    else if (uMaterial == 3) {                                    // wood grain
        float rings = sin((vWorldPos.x * 0.7 + vWorldPos.z * 0.3 + fbm(vWorldPos.xy * 1.5) * 1.2) * 14.0);
        albedo *= 0.78 + 0.22 * (0.5 + 0.5 * rings);
        rough = 0.75;
    }
    else if (uMaterial == 4) { rough = 0.04; }                    // glass
    else if (uMaterial == 5) {                                    // concrete speckle
        albedo *= mix(0.92, 0.85 + 0.25 * hash13(floor(vWorldPos * 24.0)) * vnoise(vWorldPos.xz * 3.0), detail);
        rough = 0.95;
    }
    else if (uMaterial == 6) { rough = 0.08; albedo = mix(albedo, vec3(0.8, 0.9, 1.0), 0.2); }

    vec3 color;
    float alpha = uAlpha * decalAlpha;

    if (uMaterial == 2) {
        // Neon glows on its own (and blooms).
        color = albedo * 4.0;
    } else {
        vec3 L = normalize(uSunDir);
        float vis = 1.0;
        if (uShadowsEnabled) vis = mix(1.0, sunVisibility(N, L), uShadowStrength);
        color = shade(N, V, L, albedo, rough, metal, lin(uSunColor) * uSunIntensity * PI * vis);

        // Point / spot lights.
        for (int i = 0; i < uLightCount; ++i) {
            vec3  toL  = uLightPosRange[i].xyz - vWorldPos;
            float dist = length(toL);
            float range = uLightPosRange[i].w;
            if (dist > range) continue;
            vec3  Ld   = toL / max(dist, 1e-4);
            float fall = pow(clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0), 2.0) / (dist * dist + 1.0);
            if (uLightColor[i].w > 0.5) {
                float cd = dot(-Ld, uLightDir[i].xyz);
                float c0 = uLightDir[i].w;
                fall *= smoothstep(c0, mix(c0, 1.0, 0.25), cd);
            }
            color += shade(N, V, Ld, albedo, rough, metal, uLightColor[i].rgb * fall * 6.0);
        }

        // Ambient: sky light from above, bounce light from below.
        vec3 hemi = mix(lin(uAmbientGround), lin(uAmbientSky), N.y * 0.5 + 0.5);
        color += albedo * (1.0 - metal) * hemi * uAmbientIntensity;

        // Reflections of the sky on shiny surfaces.
        vec3  f0  = mix(vec3(0.04), albedo, metal);
        float NoV = max(dot(N, V), 0.0);
        vec3  F   = f0 + (max(vec3(1.0 - rough), f0) - f0) * pow(1.0 - NoV, 5.0);
        vec3  R   = reflect(-V, N);
        vec3  env = lin(skyGradient(R, uZenith, uHorizon, uGround));
        vec3  avg = lin((uZenith + uHorizon + uGround) / 3.0);
        env = mix(env, avg, rough);
        color += env * F * uReflections * (1.0 - rough * 0.7);

        // Glass & ice get more opaque at glancing angles.
        if (uMaterial == 4 || uMaterial == 6)
            alpha = mix(alpha, 1.0, pow(1.0 - NoV, 3.0) * 0.7);
    }

    if (uSelected) {
        float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0);
        color = mix(color, vec3(2.0, 0.9, 0.2), rim * 0.8);
    }

    if (uFogEnabled) {
        float dist = length(uViewPos - vWorldPos);
        float f = 1.0 - exp(-uFogDensity * dist);
        vec3 fog = lin(uFogColor);
        float toward = pow(max(dot(-V, normalize(uSunDir)), 0.0), 8.0);
        fog += lin(uSunColor) * uSunIntensity * toward * uFogSunGlow * 0.6;
        color = mix(color, fog, clamp(f, 0.0, 1.0));
    }

    FragColor = vec4(color, alpha);
}
)";

// ---------------------------------------------------------------------------
// Sky
// ---------------------------------------------------------------------------

inline const char* skyVert = R"(#version 410 core
// Fullscreen triangle generated from gl_VertexID — no vertex buffer needed.
out vec2 vNdc;
void main() {
    float x = float((gl_VertexID & 1) << 2) - 1.0;  // -1, 3, -1
    float y = float((gl_VertexID & 2) << 1) - 1.0;  // -1, -1, 3
    vNdc = vec2(x, y);
    gl_Position = vec4(x, y, 0.0, 1.0);
}
)";

inline const char* skyFrag = R"(#version 410 core
in vec2 vNdc;
uniform mat4  uInvViewProj;
uniform vec3  uZenith;
uniform vec3  uHorizon;
uniform vec3  uGround;
uniform float uSkyBrightness;
uniform vec3  uSunDir;
uniform vec3  uSunColor;
uniform float uSunIntensity;
uniform float uSunSize;
uniform vec3  uAmbient;
uniform bool  uClouds;
uniform float uCloudCover;
uniform float uCloudSpeed;
uniform vec3  uCloudColor;
uniform bool  uStars;
uniform float uTime;
out vec4 FragColor;
#pragma gb_common
void main() {
    vec4 near = uInvViewProj * vec4(vNdc, -1.0, 1.0);
    vec4 far  = uInvViewProj * vec4(vNdc,  1.0, 1.0);
    vec3 dir  = normalize(far.xyz / far.w - near.xyz / near.w);
    vec3 L    = normalize(uSunDir);

    vec3 sky = lin(skyGradient(dir, uZenith, uHorizon, uGround)) * uSkyBrightness;

    // Stars, fading in as the sky gets dark.
    if (uStars && dir.y > 0.0) {
        float dark = 1.0 - clamp(dot(lin(uZenith), vec3(0.3, 0.6, 0.1)) * 12.0, 0.0, 1.0);
        vec3 cell = floor(dir * 300.0);
        float h = hash13(cell);
        if (h > 0.9975) {
            float tw = 0.6 + 0.4 * sin(uTime * 3.0 + h * 400.0);
            sky += vec3(1.0, 0.95, 0.9) * (h - 0.9975) * 900.0 * tw * dark * smoothstep(0.0, 0.2, dir.y);
        }
    }

    // Sun disc + glow.
    float d    = max(dot(dir, L), 0.0);
    float s2   = uSunSize * uSunSize;
    float disc = smoothstep(1.0 - 0.0009 * s2, 1.0 - 0.0004 * s2, d);
    float glow = pow(d, 300.0 / max(uSunSize, 0.1)) * 1.5 + pow(d, 10.0) * 0.12;
    vec3  sunC = lin(uSunColor) * uSunIntensity;

    // Clouds: animated noise on a flat layer above the world.
    float cover = 0.0;
    if (uClouds && dir.y > 0.0) {
        vec2 uv = dir.xz / (dir.y + 0.12) * 0.7 + uTime * 0.008 * uCloudSpeed * vec2(1.0, 0.35);
        float n = fbm(uv * 1.3);
        cover = smoothstep(1.05 - uCloudCover, 1.35 - uCloudCover, n + 0.25) * smoothstep(0.0, 0.2, dir.y);
        float thick = fbm(uv * 2.6 + 3.0);
        vec3 lit = sunC * 0.35 * (0.55 + 0.45 * pow(d, 2.0)) + lin(uAmbient) * 0.8;
        vec3 cloud = lin(uCloudColor) * lit * (0.75 + 0.35 * thick);
        cloud += sunC * pow(d, 12.0) * 0.6 * (1.0 - thick);            // silver lining
        sky = mix(sky, cloud, cover * 0.95);
    }

    sky += sunC * (disc * 40.0 + glow) * (1.0 - cover * 0.9);
    FragColor = vec4(sky, 1.0);
}
)";

// ---------------------------------------------------------------------------
// Editor grid / shadow depth
// ---------------------------------------------------------------------------

inline const char* gridVert = R"(#version 410 core
layout(location=0) in vec3 aPos;
uniform mat4 uView;
uniform mat4 uProj;
out float vDist;
void main() {
    vDist = length(aPos.xz);
    gl_Position = uProj * uView * vec4(aPos, 1.0);
}
)";

inline const char* gridFrag = R"(#version 410 core
in float vDist;
uniform vec3 uColor;
out vec4 FragColor;
void main() {
    float a = clamp(1.0 - vDist / 22.0, 0.0, 1.0);
    FragColor = vec4(uColor, a * 0.6);
}
)";

inline const char* depthVert = R"(#version 410 core
layout(location=0) in vec3 aPos;
uniform mat4 uLightSpace;
uniform mat4 uModel;
void main() { gl_Position = uLightSpace * uModel * vec4(aPos, 1.0); }
)";

inline const char* depthFrag = R"(#version 410 core
void main() {}
)";

// ---------------------------------------------------------------------------
// Post-processing (all fullscreen passes)
// ---------------------------------------------------------------------------

inline const char* fullscreenVert = R"(#version 410 core
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Screen-space ambient occlusion from the depth buffer.
inline const char* ssaoFrag = R"(#version 410 core
in vec2 vUV;
uniform sampler2D uDepth;
uniform mat4  uProj;
uniform mat4  uInvProj;
uniform vec2  uTexel;
uniform float uRadius;
out float FragAO;
#pragma gb_common
vec3 viewPos(vec2 uv) {
    float d = texture(uDepth, uv).r;
    vec4 p = uInvProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}
void main() {
    float depth = texture(uDepth, vUV).r;
    if (depth >= 1.0) { FragAO = 1.0; return; }
    vec3 P = viewPos(vUV);

    // Normal from neighbouring depths (pick the flatter side to avoid edges).
    vec3 r = viewPos(vUV + vec2(uTexel.x, 0)) - P, l = P - viewPos(vUV - vec2(uTexel.x, 0));
    vec3 u = viewPos(vUV + vec2(0, uTexel.y)) - P, b = P - viewPos(vUV - vec2(0, uTexel.y));
    vec3 dx = abs(r.z) < abs(l.z) ? r : l;
    vec3 dy = abs(u.z) < abs(b.z) ? u : b;
    vec3 N = normalize(cross(dx, dy));
    if (dot(N, -P) < 0.0) N = -N;

    float ang = hash12(gl_FragCoord.xy) * 6.2831853;
    vec3 rnd = vec3(cos(ang), sin(ang), 0.0);
    vec3 T = normalize(rnd - N * dot(rnd, N));
    vec3 B = cross(N, T);
    mat3 TBN = mat3(T, B, N);

    float occ = 0.0;
    const int S = 16;
    for (int i = 0; i < S; ++i) {
        float fi = (float(i) + 0.5) / float(S);
        float th = float(i) * 2.39996;
        float ph = acos(sqrt(1.0 - fi));
        vec3 s = vec3(cos(th) * sin(ph), sin(th) * sin(ph), cos(ph));
        s = normalize(vec3(s.xy, max(s.z, 0.35)));   // no samples lying flat on the surface
        s *= mix(0.1, 1.0, fi * fi);
        vec3 sp = P + TBN * s * uRadius;
        vec4 o = uProj * vec4(sp, 1.0);
        vec2 suv = o.xy / o.w * 0.5 + 0.5;
        float sceneZ = viewPos(suv).z;
        float range = smoothstep(0.0, 1.0, uRadius / max(abs(P.z - sceneZ), 1e-4));
        // The bias grows with distance: far-away depth values are less precise.
        float bias = 0.02 + 0.006 * abs(P.z);
        occ += (sceneZ >= sp.z + bias ? 1.0 : 0.0) * range;
    }
    FragAO = clamp(1.0 - occ / float(S), 0.0, 1.0);
}
)";

// Bloom: keep only the bright parts.
inline const char* bloomPrefilterFrag = R"(#version 410 core
in vec2 vUV;
uniform sampler2D uSrc;
uniform float uThreshold;
out vec4 FragColor;
void main() {
    vec3 c = texture(uSrc, vUV).rgb;
    float br = max(c.r, max(c.g, c.b));
    float soft = clamp(br - uThreshold + 0.5, 0.0, 1.0);
    soft = soft * soft * 0.5;
    float contrib = max(soft, br - uThreshold) / max(br, 1e-4);
    FragColor = vec4(min(c * contrib, vec3(60.0)), 1.0);
}
)";

inline const char* bloomDownFrag = R"(#version 410 core
in vec2 vUV;
uniform sampler2D uSrc;
uniform vec2 uTexel;     // of the source
out vec4 FragColor;
void main() {
    vec3 a = texture(uSrc, vUV + uTexel * vec2(-1, -1)).rgb;
    vec3 b = texture(uSrc, vUV + uTexel * vec2( 1, -1)).rgb;
    vec3 c = texture(uSrc, vUV + uTexel * vec2(-1,  1)).rgb;
    vec3 d = texture(uSrc, vUV + uTexel * vec2( 1,  1)).rgb;
    vec3 e = texture(uSrc, vUV).rgb;
    FragColor = vec4((a + b + c + d) * 0.125 + e * 0.5, 1.0);
}
)";

inline const char* bloomUpFrag = R"(#version 410 core
in vec2 vUV;
uniform sampler2D uSrc;
uniform vec2 uTexel;
out vec4 FragColor;
void main() {
    // 3x3 tent filter.
    vec3 s = texture(uSrc, vUV).rgb * 4.0;
    s += texture(uSrc, vUV + uTexel * vec2(-1, 0)).rgb * 2.0;
    s += texture(uSrc, vUV + uTexel * vec2( 1, 0)).rgb * 2.0;
    s += texture(uSrc, vUV + uTexel * vec2(0, -1)).rgb * 2.0;
    s += texture(uSrc, vUV + uTexel * vec2(0,  1)).rgb * 2.0;
    s += texture(uSrc, vUV + uTexel * vec2(-1, -1)).rgb;
    s += texture(uSrc, vUV + uTexel * vec2( 1, -1)).rgb;
    s += texture(uSrc, vUV + uTexel * vec2(-1,  1)).rgb;
    s += texture(uSrc, vUV + uTexel * vec2( 1,  1)).rgb;
    FragColor = vec4(s / 16.0, 1.0);
}
)";

// Final image: AO, bloom, exposure, filmic tone mapping, colour grading.
inline const char* compositeFrag = R"(#version 410 core
in vec2 vUV;
uniform sampler2D uHdr;
uniform sampler2D uBloom;
uniform sampler2D uAO;
uniform bool  uUseBloom;
uniform bool  uUseAO;
uniform bool  uPost;
uniform vec2  uAOTexel;
uniform float uExposure;
uniform float uBloomIntensity;
uniform float uAOIntensity;
uniform float uContrast;
uniform float uSaturation;
uniform float uVignette;
uniform vec3  uTint;
uniform bool  uLumaAlpha;   // store luma in alpha for the FXAA pass
out vec4 FragColor;

vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 c = texture(uHdr, vUV).rgb;
    if (uUseAO) {
        float ao = 0.0;
        ao += texture(uAO, vUV + uAOTexel * vec2(-1.5, -1.5)).r;
        ao += texture(uAO, vUV + uAOTexel * vec2( 1.5, -1.5)).r;
        ao += texture(uAO, vUV + uAOTexel * vec2(-1.5,  1.5)).r;
        ao += texture(uAO, vUV + uAOTexel * vec2( 1.5,  1.5)).r;
        ao *= 0.25;
        c *= mix(1.0, ao, clamp(uAOIntensity, 0.0, 2.0) * 0.85);
    }
    if (uUseBloom) c += texture(uBloom, vUV).rgb * uBloomIntensity * 0.15;
    c *= uExposure;

    if (uPost) {
        c = aces(c);
        c = pow(c, vec3(1.0 / 2.2));
        c = (c - 0.5) * uContrast + 0.5;
        float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
        c = mix(vec3(l), c, uSaturation) * uTint;
        float v = length(vUV - 0.5) * 1.41421;
        c *= 1.0 - uVignette * smoothstep(0.35, 1.1, v);
    } else {
        c = pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2));
    }
    c = clamp(c, 0.0, 1.0);
    FragColor = vec4(c, uLumaAlpha ? dot(c, vec3(0.299, 0.587, 0.114)) : 1.0);
}
)";

inline const char* fxaaFrag = R"(#version 410 core
in vec2 vUV;
uniform sampler2D uSrc;
uniform vec2 uTexel;
out vec4 FragColor;
void main() {
    vec3 rgbNW = texture(uSrc, vUV + vec2(-1, -1) * uTexel).rgb;
    vec3 rgbNE = texture(uSrc, vUV + vec2( 1, -1) * uTexel).rgb;
    vec3 rgbSW = texture(uSrc, vUV + vec2(-1,  1) * uTexel).rgb;
    vec3 rgbSE = texture(uSrc, vUV + vec2( 1,  1) * uTexel).rgb;
    vec3 rgbM  = texture(uSrc, vUV).rgb;
    vec3 lw = vec3(0.299, 0.587, 0.114);
    float lNW = dot(rgbNW, lw), lNE = dot(rgbNE, lw), lSW = dot(rgbSW, lw), lSE = dot(rgbSE, lw);
    float lM = dot(rgbM, lw);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));

    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), ((lNW + lSW) - (lNE + lSE)));
    float reduce = max((lNW + lNE + lSW + lSE) * (0.25 / 8.0), 1.0 / 128.0);
    float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcpMin, vec2(-8.0), vec2(8.0)) * uTexel;

    vec3 a = 0.5 * (texture(uSrc, vUV + dir * (1.0 / 3.0 - 0.5)).rgb +
                    texture(uSrc, vUV + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 b = a * 0.5 + 0.25 * (texture(uSrc, vUV + dir * -0.5).rgb +
                               texture(uSrc, vUV + dir *  0.5).rgb);
    float lB = dot(b, lw);
    FragColor = vec4((lB < lMin || lB > lMax) ? a : b, 1.0);
}
)";

} // namespace Shaders
