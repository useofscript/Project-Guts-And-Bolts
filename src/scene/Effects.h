#pragma once
#include <glm/glm.hpp>
#include <utility>
#include <vector>

class Scene;
class SceneNode;
struct BlastOptions;

namespace Effects {
// A classic explosion (scripts' Explode, exploding barrels): a blast with the usual options.
void explode(Scene& scene, const glm::vec3& pos, float radius, float power = 1.0f);
// The full blast (shockwave, fire, smoke, mushroom cloud, water: see Blast.h) with its
// options. Gives back what it reached (for Explosion.Hit). Shown to other players too.
std::vector<std::pair<SceneNode*, float>> blast(Scene& scene, const glm::vec3& pos, const BlastOptions& opts);
}
