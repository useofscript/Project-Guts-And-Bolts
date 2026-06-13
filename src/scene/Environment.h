#pragma once
#include <glm/glm.hpp>
#include <cmath>

// Scene-wide lighting & atmosphere settings — the engine's equivalent of
// Roblox's Lighting service. Owned by Scene, edited via the Environment panel,
// consumed by the viewport renderer.
struct Environment {
    // Sun (single directional light) -----------------------------------------
    float     sunAzimuth     = 50.0f;   // degrees around the horizon
    float     sunElevation   = 55.0f;   // degrees above the horizon
    glm::vec3 sunColor       = {1.00f, 0.96f, 0.88f};
    float     sunIntensity   = 1.15f;
    bool      shadows        = true;    // sun casts shadows

    // Ambient / sky fill light ------------------------------------------------
    glm::vec3 ambientColor      = {0.40f, 0.45f, 0.55f};
    float     ambientIntensity  = 0.45f;

    // Procedural sky ----------------------------------------------------------
    bool      showSky    = true;
    glm::vec3 skyZenith  = {0.10f, 0.26f, 0.52f};
    glm::vec3 skyHorizon = {0.64f, 0.74f, 0.86f};
    glm::vec3 skyGround  = {0.16f, 0.15f, 0.14f};

    // Atmosphere (exponential distance fog) -----------------------------------
    bool      fogEnabled = true;
    glm::vec3 fogColor   = {0.64f, 0.74f, 0.86f};
    float     fogDensity = 0.012f;

    // Unit vector pointing from a surface toward the sun.
    glm::vec3 sunDirection() const {
        float el = glm::radians(sunElevation);
        float az = glm::radians(sunAzimuth);
        return glm::normalize(glm::vec3(
            std::cos(el) * std::cos(az),
            std::sin(el),
            std::cos(el) * std::sin(az)));
    }
};

// Ready-made atmospheres, like Roblox's lighting presets.
namespace EnvironmentPresets {

inline Environment day() { return Environment{}; }

inline Environment sunset() {
    Environment e;
    e.sunAzimuth = 18.0f;  e.sunElevation = 7.0f;
    e.sunColor = {1.00f, 0.56f, 0.30f}; e.sunIntensity = 1.35f;
    e.ambientColor = {0.45f, 0.33f, 0.32f}; e.ambientIntensity = 0.40f;
    e.skyZenith  = {0.16f, 0.18f, 0.40f};
    e.skyHorizon = {0.97f, 0.55f, 0.30f};
    e.skyGround  = {0.16f, 0.11f, 0.11f};
    e.fogColor = {0.95f, 0.58f, 0.36f}; e.fogDensity = 0.020f;
    return e;
}

inline Environment night() {
    Environment e;
    e.sunAzimuth = 205.0f; e.sunElevation = 35.0f;
    e.sunColor = {0.55f, 0.62f, 0.85f}; e.sunIntensity = 0.40f;
    e.ambientColor = {0.16f, 0.20f, 0.33f}; e.ambientIntensity = 0.35f;
    e.skyZenith  = {0.02f, 0.03f, 0.08f};
    e.skyHorizon = {0.06f, 0.09f, 0.18f};
    e.skyGround  = {0.03f, 0.03f, 0.05f};
    e.fogColor = {0.06f, 0.09f, 0.18f}; e.fogDensity = 0.022f;
    return e;
}

inline Environment overcast() {
    Environment e;
    e.sunElevation = 70.0f;
    e.sunColor = {0.90f, 0.90f, 0.92f}; e.sunIntensity = 0.70f;
    e.ambientColor = {0.60f, 0.62f, 0.66f}; e.ambientIntensity = 0.70f;
    e.skyZenith  = {0.55f, 0.58f, 0.62f};
    e.skyHorizon = {0.73f, 0.75f, 0.77f};
    e.skyGround  = {0.30f, 0.30f, 0.31f};
    e.fogColor = {0.73f, 0.75f, 0.77f}; e.fogDensity = 0.030f;
    return e;
}

} // namespace EnvironmentPresets
