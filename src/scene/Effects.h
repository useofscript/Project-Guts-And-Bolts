#pragma once
#include <glm/glm.hpp>

class Scene;

namespace Effects {
// Boom! Fire and smoke, loose parts get thrown, and the player gets hurt
// (or blown apart) depending on how close they are.
void explode(Scene& scene, const glm::vec3& pos, float radius, float power = 1.0f);
}
