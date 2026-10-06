#pragma once
// Small maths shared by the movers (BodyVelocity, AlignPosition...) on loose parts
// (RigidBodies.cpp) and on characters (Player.cpp).
#include "SceneNode.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace Movers {

// A turn from Euler degrees, in the same order as Transform (Z, then Y, then X).
inline glm::quat eulerQuat(const glm::vec3& deg) {
    const glm::vec3 r = glm::radians(deg);
    return glm::angleAxis(r.z, glm::vec3(0, 0, 1)) * glm::angleAxis(r.y, glm::vec3(0, 1, 0)) *
           glm::angleAxis(r.x, glm::vec3(1, 0, 0));
}

// Which way something faces in the world (its size taken out).
inline glm::quat worldRot(const SceneNode* n) {
    glm::mat3 m(n->worldMatrix());
    for (int i = 0; i < 3; ++i) {
        const float l = glm::length(m[i]);
        m[i] = l > 1e-8f ? m[i] / l : glm::vec3(i == 0, i == 1, i == 2);
    }
    return glm::normalize(glm::quat_cast(m));
}

// The turn that takes `from` to `to` (the short way round), as axis * angle in radians.
inline glm::vec3 rotationError(const glm::quat& from, const glm::quat& to) {
    glm::quat e = to * glm::inverse(from);
    if (e.w < 0.0f) e = -e;
    const float s = std::sqrt(std::max(0.0f, 1.0f - e.w * e.w));
    if (s < 1e-5f) return glm::vec3(0.0f);
    const float angle = 2.0f * std::acos(std::clamp(e.w, -1.0f, 1.0f));
    return glm::vec3(e.x, e.y, e.z) / s * angle;
}

// How much a spring (stiffness k) with damping c changes the speed of something
// `mass` heavy, `x` away from where it wants to be and moving at `v`, over `h`
// seconds. Worked out "implicitly", so even a very stiff spring never blows up.
inline float springDv(float x, float v, float k, float c, float mass, float h) {
    mass = std::max(mass, 1e-4f);
    return h * (k * x - c * v) / mass / (1.0f + h * c / mass + h * h * k / mass);
}

// Clamp each part of `d` to +-limit[i] (limit <= 0: that axis isn't touched at all).
inline glm::vec3 clampAxes(glm::vec3 d, const glm::vec3& limit) {
    for (int i = 0; i < 3; ++i) d[i] = limit[i] > 0.0f ? std::clamp(d[i], -limit[i], limit[i]) : 0.0f;
    return d;
}

// Shorten `d` to at most `most` long.
inline glm::vec3 clampLength(const glm::vec3& d, float most) {
    const float l = glm::length(d);
    return l > most && l > 1e-9f ? d * (most / l) : d;
}

} // namespace Movers
