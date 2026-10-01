#include "Effects.h"
#include "Blast.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Particles.h"

#include <algorithm>

namespace Effects {

std::vector<std::pair<SceneNode*, float>> blast(Scene& scene, const glm::vec3& pos, const BlastOptions& opts) {
    // Everyone sees it (the options ride along in the effect's text); this machine
    // runs the physics (single player, or the host of an online game).
    scene.pushFx(FxEvent::Explosion, pos, opts.radius, opts.encode());
    if (opts.visible) scene.particles().sparks(pos, (int)std::clamp(opts.radius * 4.0f, 10.0f, 80.0f));
    std::vector<std::pair<SceneNode*, float>> out;
    for (const auto& h : scene.blasts().start(scene, pos, opts, true)) out.push_back({h.part, h.distance});
    return out;
}

void explode(Scene& scene, const glm::vec3& pos, float radius, float power) {
    BlastOptions o;
    o.radius = std::max(0.5f, radius);
    o.power = power;
    blast(scene, pos, o);
}

} // namespace Effects
