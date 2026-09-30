#pragma once
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

// Gerstner waves: the ocean swell on water parts (the "Waves" / "WaveScale"
// attributes). Unlike plain up-and-down sine waves, each bit of water also moves
// in a little circle, so the surface bunches up towards the crests: sharp peaks
// and wide, smooth troughs, like real water.
//
// The water's vertex shader moves the surface with exactly these numbers (the
// renderer passes kWaves in), and the physics uses heightAt() below, so what you
// see and what you float on are the same waves.
namespace Gerstner {

struct Wave {
    float dirX, dirZ;   // which way it rolls (unit length)
    float length;       // crest to crest, in studs
    float share;        // its part of the total height (they add up to 1)
    float phase;        // so the waves don't all line up at the start
};
inline constexpr int kCount = 4;
inline constexpr Wave kWaves[kCount] = {
    { 0.958f,  0.287f, 19.0f, 0.46f, 0.0f},
    {-0.370f,  0.929f, 11.5f, 0.28f, 1.7f},
    { 0.707f,  0.707f,  6.8f, 0.17f, 4.1f},
    {-0.829f,  0.559f,  3.9f, 0.09f, 2.6f},
};
inline constexpr float kGravity = 9.8f;   // deep-water waves: speed from their length

inline float wavenumber(const Wave& w) { return 6.2831853f / w.length; }
inline float speed(const Wave& w) { return std::sqrt(kGravity * wavenumber(w)); }   // radians / second
// How much the water bunches towards the crests: none for a tiny swell, most for a
// big one (never so much that a crest folds over itself).
inline float steepness(float amplitude) { return 0.8f * std::clamp(amplitude / 0.6f, 0.0f, 1.0f); }

// Where the water that sits at (x, z) when calm is moved to: sideways and up.
// `edge` (0..1) fades the sideways part out near the sides of the pool, so the
// water never pokes through the walls.
inline glm::vec3 offset(float x, float z, float t, float amplitude, float edge) {
    glm::vec3 d(0.0f);
    if (amplitude <= 0.0f) return d;
    const float q = steepness(amplitude) / kCount;
    for (const Wave& w : kWaves) {
        const float k = wavenumber(w);
        const float ph = k * (w.dirX * x + w.dirZ * z) - speed(w) * t + w.phase;
        const float c = std::cos(ph);
        d.x += w.dirX * (q / k) * c * edge;
        d.z += w.dirZ * (q / k) * c * edge;
        d.y += amplitude * w.share * std::sin(ph);
    }
    return d;
}

// The height of the waves above the point (x, z) (the sideways pull undone).
inline float heightAt(float x, float z, float t, float amplitude, float edge) {
    if (amplitude <= 0.0f) return 0.0f;
    float x0 = x, z0 = z;
    for (int i = 0; i < 3; ++i) {   // which calm point ends up over (x, z)?
        const glm::vec3 d = offset(x0, z0, t, amplitude, edge);
        x0 = x - d.x;
        z0 = z - d.z;
    }
    return offset(x0, z0, t, amplitude, edge).y;
}

} // namespace Gerstner
