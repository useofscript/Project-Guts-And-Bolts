// The liquid's physics as compute shaders (see LiquidGpu.h for the big picture).
#include "LiquidGpu.h"
#include "../renderer/GL.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// ---------------------------------------------------------------------------
// The "grid" buffer holds everything that isn't one-per-drop, as uints:
//   [0]      how many drops there are
//   [1..3]   dispatch size for "one thread per drop" (x, 1, 1)
//   [4..7]   draw command for the renderer (count, 1, 0, 0)
//   [8]      how many drops fell into pools this frame
//   [16..22] lowest x y z, highest x y z (sortable float bits), fastest (x1000)
//   [32..]   splashes: x y z speed (float bits), kMaxSplashes of them
//   [288..]  probes: count, vx vy vz (x256), top (sortable float bits)
//   [640..]  drops per grid cell (table + 1 for "dead"), then where each cell
//            starts in the sorted list, then per-block sums for the prefix sum
// ---------------------------------------------------------------------------
constexpr uint32_t kState = 640, kSplash = 32, kProbe = 288, kStats = 16;
constexpr uint32_t kGroup = 128;          // threads per work group (every OpenGL ES 3.1 GPU has this)
constexpr uint32_t kBlock = kGroup * 8;   // cells per prefix-sum block

enum Kernel { EMIT, SETCOUNT, PREDICT, CLEAR, COUNT, SCAN1, SCAN2, SCAN3, SCATTER, FINISH,
              LAMBDA, DELTA, VELOCITY, VISCOSITY, STATS, KERNELS };
const char* kKernelNames[KERNELS] = {"EMIT", "SETCOUNT", "PREDICT", "CLEAR", "COUNT", "SCAN1", "SCAN2", "SCAN3",
                                     "SCATTER", "FINISH", "LAMBDA", "DELTA", "VELOCITY", "VISCOSITY", "STATS"};
bool kUsesCollide(int k) { return k == PREDICT || k == DELTA || k == VISCOSITY; }

const char* kCommon = R"(
layout(local_size_x = 128) in;
layout(std430, binding = 3) buffer Grid { uint g[]; };
uniform uint uT;          // grid cells in the table (a power of two); uT itself = "dead"
const uint STATE = 640u;
uint countAt(uint h) { return STATE + h; }
uint startAt(uint h) { return STATE + uT + 1u + h; }
uint blockAt(uint b) { return STATE + 2u * (uT + 1u) + b; }

const float kH = 1.0;                 // how far a drop feels its neighbours
const float kRadius = 0.3;
const float kPi = 3.14159265358979;
float poly6(float r2) {
    float d = kH * kH - r2;
    return d > 0.0 ? (315.0 / (64.0 * kPi * pow(kH, 9.0))) * d * d * d : 0.0;
}
vec3 spikyGrad(vec3 r, float len) {
    if (len <= 1e-6 || len >= kH) return vec3(0.0);
    float d = kH - len;
    return (-45.0 / (kPi * pow(kH, 6.0))) * d * d * (r / len);
}
ivec3 cellOf(vec3 p) { return ivec3(floor(p / kH)); }
uint cellHash(ivec3 c) {
    return ((uint(c.x) * 73856093u) ^ (uint(c.y) * 19349663u) ^ (uint(c.z) * 83492791u)) & (uT - 1u);
}
uint sortable(float f) {
    uint b = floatBitsToUint(f);
    return (b & 0x80000000u) != 0u ? ~b : (b | 0x80000000u);
}
)";

// Bumping into solid things: boxes, balls, cylinders (turned any way) and the
// triangles of custom meshes. Same rules as Liquid::collide.
const char* kCollide = R"(
#ifdef K_VISCOSITY
layout(std430, binding = 5) buffer Wet { uint WET[]; };
#endif
layout(std430, binding = 6) readonly buffer Coll { vec4 col[]; };
layout(std430, binding = 7) readonly buffer CGrid { int cg[]; };
uniform ivec3 uCgMin;
uniform ivec3 uCgDim;
uniform float uCgCell;
uniform int   uCgItems;
const float kFriction = 0.15;       // per second (as Liquid.cpp)

vec3 closestOnTri(vec3 p, vec3 a, vec3 b, vec3 c) {
    vec3 ab = b - a, ac = c - a, ap = p - a;
    float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0) return a;
    vec3 bp = p - b;
    float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0.0 && d4 <= d3) return b;
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) return a + ab * (d1 / (d1 - d3));
    vec3 cp = p - c;
    float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6) return c;
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) return a + ac * (d2 / (d2 - d6));
    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    float denom = 1.0 / (va + vb + vc);
    return a + ab * (vb * denom) + ac * (vc * denom);
}

void collide(inout vec3 p, inout vec3 v, float dt, vec3 prev) {
    if (uCgDim.x == 0) return;
    ivec3 cell = ivec3(floor(p / uCgCell)) - uCgMin;
    if (any(lessThan(cell, ivec3(0))) || any(greaterThanEqual(cell, uCgDim))) return;
    int ci = (cell.z * uCgDim.y + cell.y) * uCgDim.x + cell.x;
    int k1 = cg[ci + 1];
    for (int k = cg[ci]; k < k1; ++k) {
        int base = cg[uCgItems + k] * 7;
        vec4 mn = col[base + 5], mx = col[base + 6];
        if (any(lessThan(p, mn.xyz - vec3(kRadius))) || any(greaterThan(p, mx.xyz + vec3(kRadius)))) continue;
        vec4 c0 = col[base], c1 = col[base + 1], c2 = col[base + 2], c3 = col[base + 3];
        vec3 cv = col[base + 4].xyz;
        int shape = int(c0.w + 0.5);
        vec3 n;
        if (shape == 3) {
            // A triangle: stay on the side the drop came from.
            vec3 a = c0.xyz, b = c1.xyz, c = c2.xyz, tn = c3.xyz;
            float sp = dot(prev - a, tn), sn = dot(p - a, tn);
            float side = abs(sp) > 1e-4 ? sign(sp) : (sn >= 0.0 ? 1.0 : -1.0);
            bool hit = false;
            if (sp * sn < 0.0) {             // went through the plane this step: where?
                vec3 x = mix(prev, p, sp / (sp - sn));
                vec3 q = closestOnTri(x, a, b, c);
                if (dot(q - x, q - x) < 1e-6) { p = q + tn * side * kRadius; n = tn * side; hit = true; }
            }
            if (!hit) {
                vec3 q = closestOnTri(p, a, b, c);
                vec3 d = p - q;
                float dist = length(d);
                if (dist >= kRadius) continue;
                vec3 dir = dist > 1e-5 ? d / dist : tn * side;
                if (dot(dir, tn * side) < 0.0) dir = tn * side;
                p = q + dir * kRadius;
                n = dir;
            }
        } else {
            vec3 center = c0.xyz, a0 = c1.xyz, a1 = c2.xyz, a2 = c3.xyz;
            vec3 hs = vec3(c1.w, c2.w, c3.w);
            vec3 d = p - center;
            vec3 l = vec3(dot(d, a0), dot(d, a1), dot(d, a2));
            vec3 nl = vec3(0.0);
            if (shape == 0) {
                vec3 e = hs + vec3(kRadius);
                if (abs(l.x) >= e.x || abs(l.y) >= e.y || abs(l.z) >= e.z) continue;
                // Out through the side it came in by (never pop out through the far side of a thin wall).
                vec3 dp = prev - center;
                vec3 lp = vec3(dot(dp, a0), dot(dp, a1), dot(dp, a2));
                vec3 o = abs(lp) / e;
                int ax = -1;
                float best = 1.0;
                if (o.x >= best) { best = o.x; ax = 0; }
                if (o.y >= best) { best = o.y; ax = 1; }
                if (o.z >= best) { best = o.z; ax = 2; }
                float s;
                if (ax >= 0) {
                    s = lp[ax] < 0.0 ? -1.0 : 1.0;
                } else {                    // it was already inside: the nearest face
                    vec3 pen = e - abs(l);
                    ax = pen.x <= pen.y && pen.x <= pen.z ? 0 : (pen.y <= pen.z ? 1 : 2);
                    s = l[ax] < 0.0 ? -1.0 : 1.0;
                }
                l[ax] = s * e[ax];
                nl[ax] = s;
            } else if (shape == 1) {
                vec3 e = hs + vec3(kRadius);
                vec3 q = l / e;
                float len = length(q);
                if (len >= 1.0) continue;
                vec3 dq = len > 1e-5 ? q / len : vec3(0.0, 1.0, 0.0);
                l = dq * e;
                nl = normalize(dq / e);
            } else {
                float rx = hs.x + kRadius, rz = hs.z + kRadius, hy = hs.y + kRadius;
                float rr = sqrt((l.x / rx) * (l.x / rx) + (l.z / rz) * (l.z / rz));
                if (rr >= 1.0 || abs(l.y) >= hy) continue;
                float radialPen = (1.0 - rr) * min(rx, rz), capPen = hy - abs(l.y);
                if (capPen < radialPen) {
                    float s = l.y < 0.0 ? -1.0 : 1.0;
                    l.y = s * hy;
                    nl = vec3(0.0, s, 0.0);
                } else {
                    vec2 dir = rr > 1e-5 ? vec2(l.x / rx, l.z / rz) / rr : vec2(1.0, 0.0);
                    l.x = dir.x * rx; l.z = dir.y * rz;
                    nl = normalize(vec3(dir.x / rx, 0.0, dir.y / rz));
                }
            }
            p = center + a0 * l.x + a1 * l.y + a2 * l.z;
            n = normalize(a0 * nl.x + a1 * nl.y + a2 * nl.z);
        }
        vec3 rel = v - cv;
        float vn = dot(rel, n);
        if (vn < 0.0) rel -= vn * n;                                  // no going into it
        if (dt > 0.0) rel -= (rel - dot(rel, n) * n) * min(1.0, kFriction * dt);   // a little drag along it
        v = rel + cv;
#ifdef K_VISCOSITY
        WET[cg[uCgItems + k]] = 1u;                                   // it's wet now
#endif
    }
}
)";

// Loop over every drop near `pi` (the 27 grid cells around it, each bucket once).
const char* kNeighbours = R"(
#define FOR_NEIGHBOURS(pi, BODY) { \
    ivec3 c_ = cellOf(pi); uint seen_[27]; int nseen_ = 0; \
    for (int dz_ = -1; dz_ <= 1; ++dz_) for (int dy_ = -1; dy_ <= 1; ++dy_) for (int dx_ = -1; dx_ <= 1; ++dx_) { \
        uint h_ = cellHash(c_ + ivec3(dx_, dy_, dz_)); bool dup_ = false; \
        for (int s_ = 0; s_ < nseen_; ++s_) if (seen_[s_] == h_) dup_ = true; \
        if (dup_) continue; seen_[nseen_++] = h_; \
        uint j0_ = g[startAt(h_)], j1_ = j0_ + g[countAt(h_)]; \
        for (uint j = j0_; j < j1_; ++j) { BODY } } }
)";

const char* kKernels = R"(
#ifdef K_EMIT
layout(std430, binding = 0) readonly buffer In { vec4 IN[]; };
layout(std430, binding = 1) buffer BX { vec4 X[]; };
layout(std430, binding = 2) buffer BV { vec4 V[]; };
uniform uint uAdd; uniform uint uOff; uniform uint uCap;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uAdd) return;
    uint j = g[0] + i;
    if (j >= uCap) return;
    vec3 v = IN[uOff + 2u * i + 1u].xyz;
    vec4 p = IN[uOff + 2u * i];      // w: which kind of liquid
    X[j] = vec4(p.xyz, floor(p.w + 0.5) * 4096.0 + 6.0 * 64.0 + min(length(v), 63.0));
    V[j] = vec4(v, 0.0);
}
#endif

#ifdef K_SETCOUNT
uniform uint uAdd; uniform uint uCap;
void main() {
    if (gl_GlobalInvocationID.x != 0u) return;
    uint c = min(uCap, g[0] + uAdd);
    g[0] = c; g[1] = (c + 127u) / 128u; g[2] = 1u; g[3] = 1u;
}
#endif

#ifdef K_PREDICT
layout(std430, binding = 0) buffer BX { vec4 X[]; };
layout(std430, binding = 1) buffer BV { vec4 V[]; };
layout(std430, binding = 2) buffer BP { vec4 P[]; };
layout(std430, binding = 4) buffer BK { uvec2 K[]; };
uniform float uDt; uniform vec3 uGravity; uniform float uMaxAge;
uniform vec4 uPoolMin[32]; uniform vec4 uPoolMax[32]; uniform int uPools;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= g[0]) return;
    vec3 x = X[i].xyz;
    vec4 vv = V[i];
    vec3 v = vv.xyz;
    float age = vv.w + uDt;
    bool dead = age > uMaxAge || x.y < -40.0 || any(isnan(x)) || any(isnan(v));
    // Liquid that pours into a pool of still water becomes part of it.
    for (int k = 0; k < uPools && !dead; ++k) {
        vec3 lo = uPoolMin[k].xyz, hi = uPoolMax[k].xyz;
        if (x.x < lo.x || x.x > hi.x || x.z < lo.z || x.z > hi.z || x.y < lo.y || x.y > hi.y - 0.15) continue;
        dead = true;
        uint s = atomicAdd(g[8], 1u);
        if (s < 64u) {
            uint o = 32u + s * 4u;
            g[o] = floatBitsToUint(x.x); g[o + 1u] = floatBitsToUint(hi.y);
            g[o + 2u] = floatBitsToUint(x.z); g[o + 3u] = floatBitsToUint(length(v));
        }
    }
    if (dead) { K[i] = uvec2(uT, 0u); return; }
    v += uGravity * uDt;
    float sp = length(v);
    if (sp > 32.0) v *= 32.0 / sp;
    vec3 p = x + v * uDt;
    collide(p, v, 0.0, x);
    V[i] = vec4(v, age);
    P[i] = vec4(p, 0.0);
    K[i] = uvec2(cellHash(cellOf(p)), 0u);
}
#endif

#ifdef K_CLEAR
void main() {
    uint h = gl_GlobalInvocationID.x;
    if (h <= uT) g[countAt(h)] = 0u;
}
#endif

#ifdef K_COUNT
layout(std430, binding = 4) buffer BK { uvec2 K[]; };
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= g[0]) return;
    uint k = K[i].x;
    K[i].y = atomicAdd(g[countAt(k)], 1u);
}
#endif

// Prefix sum of the cell counts: where each cell's drops start in the sorted list.
#ifdef K_SCAN1
shared uint sh[128];
void main() {
    uint lid = gl_LocalInvocationID.x, n = uT + 1u;
    uint base = gl_WorkGroupID.x * 1024u + lid * 8u;
    uint mine[8];
    uint sum = 0u;
    for (uint j = 0u; j < 8u; ++j) {
        uint idx = base + j;
        uint c = idx < n ? g[countAt(idx)] : 0u;
        mine[j] = sum;
        sum += c;
    }
    sh[lid] = sum;
    barrier();
    for (uint off = 1u; off < 128u; off <<= 1u) {
        uint t = lid >= off ? sh[lid - off] : 0u;
        barrier();
        sh[lid] += t;
        barrier();
    }
    uint before = sh[lid] - sum;
    for (uint j = 0u; j < 8u; ++j) {
        uint idx = base + j;
        if (idx < n) g[startAt(idx)] = before + mine[j];
    }
    if (lid == 127u) g[blockAt(gl_WorkGroupID.x)] = sh[127];
}
#endif

#ifdef K_SCAN2
shared uint sh[128];
uniform uint uBlocks;
void main() {
    uint lid = gl_LocalInvocationID.x;
    uint per = (uBlocks + 127u) / 128u, base = lid * per;
    uint sum = 0u;
    for (uint j = 0u; j < per; ++j) if (base + j < uBlocks) sum += g[blockAt(base + j)];
    sh[lid] = sum;
    barrier();
    for (uint off = 1u; off < 128u; off <<= 1u) {
        uint t = lid >= off ? sh[lid - off] : 0u;
        barrier();
        sh[lid] += t;
        barrier();
    }
    uint run = sh[lid] - sum;
    for (uint j = 0u; j < per; ++j) {
        if (base + j >= uBlocks) break;
        uint t = g[blockAt(base + j)];
        g[blockAt(base + j)] = run;
        run += t;
    }
}
#endif

#ifdef K_SCAN3
void main() {
    uint lid = gl_LocalInvocationID.x, n = uT + 1u;
    uint add = g[blockAt(gl_WorkGroupID.x)];
    uint base = gl_WorkGroupID.x * 1024u + lid * 8u;
    for (uint j = 0u; j < 8u; ++j) if (base + j < n) g[startAt(base + j)] += add;
}
#endif

// Copy every drop to its place in the sorted list (dead ones go to the end and are dropped).
#ifdef K_SCATTER
layout(std430, binding = 0) readonly buffer BX { vec4 X[]; };
layout(std430, binding = 1) readonly buffer BV { vec4 V[]; };
layout(std430, binding = 2) readonly buffer BP { vec4 P[]; };
layout(std430, binding = 4) readonly buffer BK { uvec2 K[]; };
layout(std430, binding = 5) writeonly buffer BX2 { vec4 X2[]; };
layout(std430, binding = 6) writeonly buffer BV2 { vec4 V2[]; };
layout(std430, binding = 7) writeonly buffer BP2 { vec4 P2[]; };
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= g[0]) return;
    uvec2 k = K[i];
    uint j = g[startAt(k.x)] + k.y;
    X2[j] = X[i]; V2[j] = V[i]; P2[j] = P[i];
}
#endif

#ifdef K_FINISH
void main() {
    if (gl_GlobalInvocationID.x != 0u) return;
    uint alive = g[startAt(uT)];
    g[0] = alive; g[1] = (alive + 127u) / 128u; g[2] = 1u; g[3] = 1u;
    g[4] = alive; g[5] = 1u; g[6] = 0u; g[7] = 0u;
}
#endif

// How crowded is each drop? (Density constraint: only push apart, never pull together.)
#ifdef K_LAMBDA
layout(std430, binding = 2) readonly buffer BP { vec4 P[]; };
layout(std430, binding = 5) writeonly buffer BL { float L[]; };
uniform float uInvRest;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= g[0]) return;
    vec3 pi = P[i].xyz;
    float rho = poly6(0.0), sumGrad2 = 0.0;
    vec3 gradI = vec3(0.0);
    FOR_NEIGHBOURS(pi, {
        if (j == i) continue;
        vec3 r = pi - P[j].xyz;
        float r2 = dot(r, r);
        if (r2 >= kH * kH) continue;
        float len = sqrt(r2);
        rho += poly6(r2);
        vec3 gr = spikyGrad(r, len) * uInvRest;
        sumGrad2 += dot(gr, gr);
        gradI += gr;
    })
    float C = max(0.0, rho * uInvRest - 1.0);
    L[i] = -C / (sumGrad2 + dot(gradI, gradI) + 0.5);
}
#endif

// Push the drops apart by how crowded they are.
#ifdef K_DELTA
layout(std430, binding = 0) readonly buffer BX { vec4 X[]; };
layout(std430, binding = 2) readonly buffer BP { vec4 P[]; };
layout(std430, binding = 4) writeonly buffer BP2 { vec4 P2[]; };
layout(std430, binding = 5) readonly buffer BL { float L[]; };
uniform float uInvRest; uniform float uWq;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= g[0]) return;
    vec3 pi = P[i].xyz;
    float li = L[i];
    vec3 d = vec3(0.0);
    FOR_NEIGHBOURS(pi, {
        if (j == i) continue;
        vec3 r = pi - P[j].xyz;
        float r2 = dot(r, r);
        if (r2 >= kH * kH) continue;
        float len = sqrt(r2);
        float corr = poly6(r2) / uWq;
        corr = -0.002 * corr * corr * corr * corr;           // stops drops clumping at the surface
        d += (li + L[j] + corr) * spikyGrad(r, len);
    })
    d *= uInvRest;
    float dl = length(d);
    if (dl > 0.25) d *= 0.25 / dl;       // never shove a drop more than half a spacing at once
    vec3 p = pi + d;
    vec3 dummy = vec3(0.0);
    collide(p, dummy, 0.0, X[i].xyz);
    P2[i] = vec4(p, 0.0);
}
#endif

#ifdef K_VELOCITY
layout(std430, binding = 0) readonly buffer BX { vec4 X[]; };
layout(std430, binding = 2) readonly buffer BP { vec4 P[]; };
layout(std430, binding = 4) writeonly buffer BS { vec4 S[]; };
uniform float uDt;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= g[0]) return;
    S[i] = vec4((P[i].xyz - X[i].xyz) / uDt, 0.0);
}
#endif

// Viscosity (drops move with their neighbours), friction on walls, and done.
#ifdef K_VISCOSITY
layout(std430, binding = 0) buffer BX { vec4 X[]; };
layout(std430, binding = 1) buffer BV { vec4 V[]; };
layout(std430, binding = 2) readonly buffer BP { vec4 P[]; };
layout(std430, binding = 4) readonly buffer BS { vec4 S[]; };
uniform float uDt; uniform float uInvRest;
uniform vec2 uFluid[16];             // each kind of liquid: viscosity, surface tension
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= g[0]) return;
    vec3 pi = P[i].xyz, vi = S[i].xyz;
    float kind = floor(X[i].w / 4096.0);
    vec2 fl = uFluid[int(clamp(kind, 0.0, 15.0))];
    vec3 acc = vec3(0.0), pull = vec3(0.0);
    float nearby = 0.0;
    FOR_NEIGHBOURS(pi, {
        if (j == i) continue;
        vec3 r = pi - P[j].xyz;
        float r2 = dot(r, r);
        if (r2 >= kH * kH) continue;
        acc += (S[j].xyz - vi) * (poly6(r2) * uInvRest);
        float len = sqrt(r2);
        if (len > 1e-5) pull -= r / len * sin(kPi * len / kH);   // surface tension: pull together
        nearby += 1.0;
    })
    // Viscosity (thick liquids move with their neighbours) and surface tension (beads, strands).
    vec3 v = vi + fl.x * acc + pull * (fl.y * 60.0 * uDt);
    vec3 p = pi;
    collide(p, v, nearby < 3.0 ? uDt * 10.0 : uDt, X[i].xyz);   // (a lone drop sticks to things; a stream slides)
    float age = V[i].w;
    if (nearby < 3.0 && dot(v, v) < 1.0) age += uDt * 30.0;   // a stray drop sitting on its own dries up in a few seconds
    // (w: how many neighbours x 64 + speed; the renderer draws lonely drops smaller)
    X[i] = vec4(p, kind * 4096.0 + min(nearby, 31.0) * 64.0 + min(length(v), 63.0));
    V[i] = vec4(v, age);
}
#endif

// Once a frame: the box around all the liquid, the fastest drop, and how much
// liquid is around each probe (people, floating things).
#ifdef K_STATS
layout(std430, binding = 0) readonly buffer BX { vec4 X[]; };
layout(std430, binding = 1) readonly buffer BV { vec4 V[]; };
uniform vec4 uProbe[64]; uniform int uProbes;
shared vec4 sLo[128];
shared vec4 sHi[128];
void main() {
    uint i = gl_GlobalInvocationID.x, lid = gl_LocalInvocationID.x;
    bool live = i < g[0];
    vec4 x = live ? X[i] : vec4(0.0);
    x.w = x.w - floor(x.w / 64.0) * 64.0;             // speed (w also counts neighbours)
    sLo[lid] = live ? vec4(x.xyz, 0.0) : vec4(1e30);
    sHi[lid] = live ? x : vec4(-1e30);
    if (live) {
        vec3 v = V[i].xyz;
        for (int k = 0; k < uProbes; ++k) {
            vec3 d = x.xyz - uProbe[k].xyz;
            if (dot(d, d) > uProbe[k].w * uProbe[k].w) continue;
            uint o = 288u + uint(k) * 5u;
            atomicAdd(g[o], 1u);
            atomicAdd(g[o + 1u], uint(int(round(v.x * 256.0))));
            atomicAdd(g[o + 2u], uint(int(round(v.y * 256.0))));
            atomicAdd(g[o + 3u], uint(int(round(v.z * 256.0))));
            atomicMax(g[o + 4u], sortable(x.y));
        }
    }
    barrier();
    for (uint s = 64u; s > 0u; s >>= 1u) {
        if (lid < s) { sLo[lid] = min(sLo[lid], sLo[lid + s]); sHi[lid] = max(sHi[lid], sHi[lid + s]); }
        barrier();
    }
    if (lid == 0u && sLo[0].x < 1e29) {
        atomicMin(g[16], sortable(sLo[0].x)); atomicMin(g[17], sortable(sLo[0].y)); atomicMin(g[18], sortable(sLo[0].z));
        atomicMax(g[19], sortable(sHi[0].x)); atomicMax(g[20], sortable(sHi[0].y)); atomicMax(g[21], sortable(sHi[0].z));
        atomicMax(g[22], uint(sHi[0].w * 1000.0));   // (speed only: see below)
    }
}
#endif
)";

float unsortable(uint32_t u) {
    const uint32_t b = (u & 0x80000000u) ? (u & 0x7FFFFFFFu) : ~u;
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

GLuint compileKernel(int k, std::string& error) {
    std::string src;
#ifdef GB_GLES
    src = "#version 310 es\nprecision highp float;\nprecision highp int;\n";
#else
    src = "#version 430 core\n";
#endif
    src += std::string("#define K_") + kKernelNames[k] + "\n";
    src += kCommon;
    if (kUsesCollide(k)) src += kCollide;
    src += kNeighbours;
    src += kKernels;
    GLuint s = glCreateShader(GL_COMPUTE_SHADER);
    const char* text = src.c_str();
    glShaderSource(s, 1, &text, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        error = std::string(kKernelNames[k]) + ": " + log;
        glDeleteShader(s);
        return 0;
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, s);
    glLinkProgram(p);
    glDeleteShader(s);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        error = std::string(kKernelNames[k]) + ": " + log;
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

GLint U(GLuint prog, const char* name) { return glGetUniformLocation(prog, name); }

void makeBuffer(GLuint& b, size_t bytes) {
    if (!b) glGenBuffers(1, &b);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, b);
    glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)std::max<size_t>(bytes, 16), nullptr, GL_DYNAMIC_COPY);
}

void bind(int slot, GLuint b) { glBindBufferBase(GL_SHADER_STORAGE_BUFFER, (GLuint)slot, b); }
void barrier() { glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT); }
GLuint groups(size_t n) { return (GLuint)((n + kGroup - 1) / kGroup); }

// Read the start of a buffer back (OpenGL ES has no glGetBufferSubData).
void readBack(GLuint b, size_t bytes, void* out) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, b);
#ifdef GB_GLES
    if (void* m = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)bytes, GL_MAP_READ_BIT)) {
        std::memcpy(out, m, bytes);
        glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    } else {
        std::memset(out, 0, bytes);
    }
#else
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)bytes, out);
#endif
}

} // namespace

LiquidGpu::LiquidGpu() = default;

LiquidGpu::~LiquidGpu() {
    for (unsigned& p : m_prog) if (p) glDeleteProgram(p);
    unsigned* bufs[] = {&m_x[0], &m_x[1], &m_v[0], &m_v[1], &m_p[0], &m_p[1], &m_scratch, &m_lambda,
                        &m_key, &m_grid, &m_coll, &m_cgrid, &m_in, &m_touched};
    for (unsigned* b : bufs) if (*b) glDeleteBuffers(1, b);
}

void LiquidGpu::forget() {
    for (unsigned& p : m_prog) p = 0;
    m_x[0] = m_x[1] = m_v[0] = m_v[1] = m_p[0] = m_p[1] = 0;
    m_scratch = m_lambda = m_key = m_grid = m_coll = m_cgrid = m_in = m_touched = 0;
}

bool LiquidGpu::supported(std::string* why) {
    auto no = [&](const char* w) { if (why) *why = w; return false; };
    if (const char* e = std::getenv("GB_LIQUID_CPU"); e && *e && *e != '0') return no("GB_LIQUID_CPU is set");
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
#ifdef GB_GLES
    if (major * 10 + minor < 31) return no("needs OpenGL ES 3.1");
#else
    if (major * 10 + minor < 43) return no("needs OpenGL 4.3");
    if (!glDispatchCompute || !glDispatchComputeIndirect || !glMemoryBarrier || !glDrawArraysIndirect)
        return no("compute shaders not loaded");
#endif
    GLint blocks = 0, bindings = 0;
    glGetIntegerv(GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS, &blocks);
    glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &bindings);
    if (blocks < 8 || bindings < 8) return no("not enough storage buffers");
    return true;
}

uint32_t LiquidGpu::maxCapacity() {
#ifdef GB_MOBILE
    return 1u << 18;     // 262,144 drops on phones
#else
    return 1u << 20;     // 1,048,576 drops on computers
#endif
}

bool LiquidGpu::init(std::string& error) {
    for (int k = 0; k < KERNELS; ++k) {
        m_prog[k] = compileKernel(k, error);
        if (!m_prog[k]) return false;
    }
    allocate(16384);
    return true;
}

void LiquidGpu::allocate(uint32_t cap) {
    cap = std::min(cap, maxCapacity());
    uint32_t table = 1024;
    while (table < cap * 2 && table < (1u << 21)) table <<= 1;
    const bool had = m_cap > 0;
    GLuint oldX = m_x[m_cur], oldV = m_v[m_cur], oldGrid = m_grid;
    const uint32_t oldCap = m_cap;
    if (had) { m_x[m_cur] = 0; m_v[m_cur] = 0; m_grid = 0; }   // keep these to copy from
    for (int i = 0; i < 2; ++i) {
        makeBuffer(m_x[i], (size_t)cap * 16);
        makeBuffer(m_v[i], (size_t)cap * 16);
        makeBuffer(m_p[i], (size_t)cap * 16);
    }
    makeBuffer(m_scratch, (size_t)cap * 16);
    makeBuffer(m_lambda, (size_t)cap * 4);
    makeBuffer(m_key, (size_t)cap * 8);
    const size_t blocks = (table + 1 + kBlock - 1) / kBlock;
    makeBuffer(m_grid, (kState + 2 * (size_t)(table + 1) + blocks + 16) * 4);
    if (had) {
        glBindBuffer(GL_COPY_READ_BUFFER, oldX);
        glBindBuffer(GL_COPY_WRITE_BUFFER, m_x[0]);
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, (GLsizeiptr)oldCap * 16);
        glBindBuffer(GL_COPY_READ_BUFFER, oldV);
        glBindBuffer(GL_COPY_WRITE_BUFFER, m_v[0]);
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, (GLsizeiptr)oldCap * 16);
        glBindBuffer(GL_COPY_READ_BUFFER, oldGrid);
        glBindBuffer(GL_COPY_WRITE_BUFFER, m_grid);
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, kState * 4);
        glDeleteBuffers(1, &oldX); glDeleteBuffers(1, &oldV); glDeleteBuffers(1, &oldGrid);
        m_cur = 0;
    }
    m_cap = cap;
    m_table = table;
    if (!had) clear();
}

void LiquidGpu::clear() {
    std::vector<uint32_t> s(kState, 0u);
    s[2] = s[3] = 1; s[5] = 1;
    for (int k = 0; k < 3; ++k) { s[kStats + k] = 0xFFFFFFFFu; s[kStats + 3 + k] = 0u; }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_grid);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, kState * 4, s.data());
    m_pending = false;
    m_res = Results{};
}

void LiquidGpu::beginFrame() {
    if (m_pending) {
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        std::vector<uint32_t> s(kState);
        readBack(m_grid, kState * 4, s.data());
        Results r;
        r.count = s[0];
        if (r.count) {
            r.lo = {unsortable(s[16]), unsortable(s[17]), unsortable(s[18])};
            r.hi = {unsortable(s[19]), unsortable(s[20]), unsortable(s[21])};
            r.fastest = s[22] / 1000.0f;
        }
        const uint32_t ns = std::min<uint32_t>(s[8], kMaxSplashes);
        for (uint32_t i = 0; i < ns; ++i) {
            glm::vec4 v;
            std::memcpy(&v, &s[kSplash + i * 4], 16);
            r.splashes.push_back(v);
        }
        for (size_t k = 0; k < m_probes.size(); ++k) {
            const uint32_t* q = &s[kProbe + k * 5];
            Results::Probe p;
            p.count = (int)q[0];
            if (p.count)
                p.vel = glm::vec3((int32_t)q[1], (int32_t)q[2], (int32_t)q[3]) / (256.0f * (float)p.count);
            p.top = p.count ? unsortable(q[4]) + 0.3f : 0.0f;
            r.probes.push_back(p);
        }
        m_res = std::move(r);
        m_pending = false;
    }
    // Start this frame's tallies again (the drop count stays).
    std::vector<uint32_t> s(kState - 8, 0u);
    for (int k = 0; k < 3; ++k) { s[kStats - 8 + k] = 0xFFFFFFFFu; s[kStats - 8 + 3 + k] = 0u; }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_grid);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 8 * 4, (kState - 8) * 4, s.data());
}

void LiquidGpu::setColliders(const std::vector<glm::vec4>& packed, const std::vector<int>& grid, int gridItems,
                             const glm::ivec3& cgMin, const glm::ivec3& cgDim, float cgCell) {
    auto upload = [](GLuint& b, size_t& cap, const void* data, size_t bytes) {
        if (!b) glGenBuffers(1, &b);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, b);
        if (bytes > cap || cap == 0) {
            cap = std::max<size_t>(bytes + bytes / 2, 256);
            glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)cap, nullptr, GL_DYNAMIC_DRAW);
        }
        if (bytes) glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)bytes, data);
    };
    upload(m_coll, m_collCap, packed.data(), packed.size() * sizeof(glm::vec4));
    upload(m_cgrid, m_cgridCap, grid.data(), grid.size() * sizeof(int));
    m_cgItems = gridItems;
    // One "the liquid touched it" flag per collider (starts at zero).
    const size_t n = packed.size() / 7;
    if (!m_touched || n > m_touchedCap) {
        m_touchedCap = std::max<size_t>(n + n / 2, 64);
        std::vector<uint32_t> zero(m_touchedCap, 0u);
        if (!m_touched) glGenBuffers(1, &m_touched);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_touched);
        glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)(m_touchedCap * 4), zero.data(), GL_DYNAMIC_COPY);
    }
    m_cgMin = cgMin;
    m_cgDim = packed.empty() ? glm::ivec3(0) : cgDim;
    m_cgCell = cgCell;
}

std::vector<uint32_t> LiquidGpu::takeTouched(size_t colliders) {
    std::vector<uint32_t> out(std::min(colliders, m_touchedCap), 0u);
    if (out.empty() || !m_touched) return out;
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    readBack(m_touched, out.size() * 4, out.data());
    const std::vector<uint32_t> zero(out.size(), 0u);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_touched);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)(zero.size() * 4), zero.data());
    return out;
}

void LiquidGpu::setPools(const std::vector<Pool>& pools) {
    m_pools.assign(pools.begin(), pools.begin() + std::min<size_t>(pools.size(), kMaxPools));
}

void LiquidGpu::dispatchDrops() {
    glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, m_grid);
    glDispatchComputeIndirect(4);
}

void LiquidGpu::step(float dt, const glm::vec3& gravity, const std::vector<glm::vec4>& incoming) {
    // Make room first (the count we know is a frame old: keep plenty spare).
    const uint32_t adding = (uint32_t)(incoming.size() / 2);
    if (m_cap < maxCapacity() && m_res.count + adding * 8 + 4096 > m_cap * 3 / 4) allocate(m_cap * 2);
    const GLuint T = m_table;
    auto setCommon = [&](int k) {
        glUseProgram(m_prog[k]);
        glUniform1ui(U(m_prog[k], "uT"), T);
        bind(3, m_grid);
        if (kUsesCollide(k)) {
            bind(6, m_coll);
            bind(7, m_cgrid);
            glUniform3i(U(m_prog[k], "uCgMin"), m_cgMin.x, m_cgMin.y, m_cgMin.z);
            glUniform3i(U(m_prog[k], "uCgDim"), m_cgDim.x, m_cgDim.y, m_cgDim.z);
            glUniform1f(U(m_prog[k], "uCgCell"), m_cgCell);
            glUniform1i(U(m_prog[k], "uCgItems"), m_cgItems);
        }
    };

    // 1. New drops from the taps.
    if (adding) {
        if (!m_in) glGenBuffers(1, &m_in);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_in);
        const size_t bytes = incoming.size() * sizeof(glm::vec4);
        if (bytes > m_inCap) {
            m_inCap = std::max<size_t>(bytes * 2, 4096);
            glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)m_inCap, nullptr, GL_STREAM_DRAW);
        }
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)bytes, incoming.data());
        setCommon(EMIT);
        bind(0, m_in); bind(1, m_x[m_cur]); bind(2, m_v[m_cur]);
        glUniform1ui(U(m_prog[EMIT], "uAdd"), adding);
        glUniform1ui(U(m_prog[EMIT], "uOff"), 0u);
        glUniform1ui(U(m_prog[EMIT], "uCap"), m_cap);
        glDispatchCompute(groups(adding), 1, 1);
        barrier();
        setCommon(SETCOUNT);
        glUniform1ui(U(m_prog[SETCOUNT], "uAdd"), adding);
        glUniform1ui(U(m_prog[SETCOUNT], "uCap"), m_cap);
        glDispatchCompute(1, 1, 1);
        barrier();
        m_res.count += adding;
    }

    // 2. Move, and keep out of solid things. Drops that died are marked.
    setCommon(PREDICT);
    bind(0, m_x[m_cur]); bind(1, m_v[m_cur]); bind(2, m_p[m_cur]); bind(4, m_key);
    glUniform1f(U(m_prog[PREDICT], "uDt"), dt);
    glUniform3f(U(m_prog[PREDICT], "uGravity"), gravity.x, gravity.y, gravity.z);
    glUniform1f(U(m_prog[PREDICT], "uMaxAge"), m_maxAge);
    {
        std::vector<glm::vec4> lo(kMaxPools), hi(kMaxPools);
        for (size_t i = 0; i < m_pools.size(); ++i) { lo[i] = glm::vec4(m_pools[i].min, 0); hi[i] = glm::vec4(m_pools[i].max, 0); }
        glUniform4fv(U(m_prog[PREDICT], "uPoolMin"), kMaxPools, &lo[0].x);
        glUniform4fv(U(m_prog[PREDICT], "uPoolMax"), kMaxPools, &hi[0].x);
        glUniform1i(U(m_prog[PREDICT], "uPools"), (int)m_pools.size());
    }
    dispatchDrops();
    barrier();

    // 3. Sort by grid cell: count, prefix sum, scatter.
    setCommon(CLEAR);
    glDispatchCompute(groups(T + 1), 1, 1);
    barrier();
    setCommon(COUNT);
    bind(4, m_key);
    dispatchDrops();
    barrier();
    const GLuint blocks = (T + 1 + kBlock - 1) / kBlock;
    setCommon(SCAN1);
    glDispatchCompute(blocks, 1, 1);
    barrier();
    setCommon(SCAN2);
    glUniform1ui(U(m_prog[SCAN2], "uBlocks"), blocks);
    glDispatchCompute(1, 1, 1);
    barrier();
    setCommon(SCAN3);
    glDispatchCompute(blocks, 1, 1);
    barrier();
    const int nxt = 1 - m_cur;
    setCommon(SCATTER);
    bind(0, m_x[m_cur]); bind(1, m_v[m_cur]); bind(2, m_p[m_cur]); bind(4, m_key);
    bind(5, m_x[nxt]); bind(6, m_v[nxt]); bind(7, m_p[nxt]);
    dispatchDrops();
    barrier();
    m_cur = nxt;
    m_pcur = nxt;
    setCommon(FINISH);
    glDispatchCompute(1, 1, 1);
    barrier();

    // 4. Make the density the same everywhere (twice).
    float rest = 0.0f;
    {
        const float kH = 1.0f, spacing = 0.5f, pi = 3.14159265358979f;
        const float poly = 315.0f / (64.0f * pi * std::pow(kH, 9.0f));
        for (int x = -2; x <= 2; ++x)
            for (int y = -2; y <= 2; ++y)
                for (int z = -2; z <= 2; ++z) {
                    const float r2 = (float)(x * x + y * y + z * z) * spacing * spacing;
                    const float d = kH * kH - r2;
                    if (d > 0.0f) rest += poly * d * d * d;
                }
        const float r2 = 0.04f;
        const float d = kH * kH - r2;
        const float wq = poly * d * d * d;
        for (int it = 0; it < 2; ++it) {
            setCommon(LAMBDA);
            bind(2, m_p[m_pcur]); bind(5, m_lambda);
            glUniform1f(U(m_prog[LAMBDA], "uInvRest"), 1.0f / rest);
            dispatchDrops();
            barrier();
            setCommon(DELTA);
            bind(0, m_x[m_cur]); bind(2, m_p[m_pcur]); bind(4, m_p[1 - m_pcur]); bind(5, m_lambda);
            glUniform1f(U(m_prog[DELTA], "uInvRest"), 1.0f / rest);
            glUniform1f(U(m_prog[DELTA], "uWq"), wq);
            dispatchDrops();
            barrier();
            m_pcur = 1 - m_pcur;
        }
    }

    // 5. New speeds, viscosity, friction.
    setCommon(VELOCITY);
    bind(0, m_x[m_cur]); bind(2, m_p[m_pcur]); bind(4, m_scratch);
    glUniform1f(U(m_prog[VELOCITY], "uDt"), dt);
    dispatchDrops();
    barrier();
    setCommon(VISCOSITY);
    bind(0, m_x[m_cur]); bind(1, m_v[m_cur]); bind(2, m_p[m_pcur]); bind(4, m_scratch); bind(5, m_touched);
    glUniform1f(U(m_prog[VISCOSITY], "uDt"), dt);
    glUniform1f(U(m_prog[VISCOSITY], "uInvRest"), 1.0f / rest);
    {
        std::vector<glm::vec2> fl(16, glm::vec2(0.015f, 0.0f));
        for (size_t k = 0; k < m_fluidParams.size() && k < 16; ++k) fl[k] = m_fluidParams[k];
        glUniform2fv(U(m_prog[VISCOSITY], "uFluid"), 16, &fl[0].x);
    }
    dispatchDrops();
    barrier();
}

void LiquidGpu::endFrame(const std::vector<glm::vec4>& probes) {
    m_probes.assign(probes.begin(), probes.begin() + std::min<size_t>(probes.size(), kMaxProbes));
    glUseProgram(m_prog[STATS]);
    glUniform1ui(U(m_prog[STATS], "uT"), m_table);
    bind(3, m_grid); bind(0, m_x[m_cur]); bind(1, m_v[m_cur]);
    std::vector<glm::vec4> pr(kMaxProbes, glm::vec4(0.0f));
    std::copy(m_probes.begin(), m_probes.end(), pr.begin());
    glUniform4fv(U(m_prog[STATS], "uProbe"), kMaxProbes, &pr[0].x);
    glUniform1i(U(m_prog[STATS], "uProbes"), (int)m_probes.size());
    dispatchDrops();
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT | GL_COMMAND_BARRIER_BIT |
                    GL_BUFFER_UPDATE_BARRIER_BIT);
    glUseProgram(0);
    m_pending = true;
}

std::vector<glm::vec4> LiquidGpu::readDrops() {
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
    uint32_t head[8];
    readBack(m_grid, sizeof(head), head);
    std::vector<glm::vec4> out(std::min(head[4], m_cap));
    if (!out.empty()) readBack(m_x[m_cur], out.size() * sizeof(glm::vec4), out.data());
    return out;
}
