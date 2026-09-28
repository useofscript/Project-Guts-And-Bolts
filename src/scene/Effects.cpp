#include "Effects.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Player.h"
#include "Physics.h"
#include "../core/Audio.h"

#include <algorithm>
#include <vector>

namespace Effects {

void explode(Scene& scene, const glm::vec3& pos, float radius, float power) {
    radius = std::max(0.5f, radius);
    scene.particles().explosion(pos, radius);
    Audio::play("explosion", std::min(1.0f, 0.5f + radius * 0.06f), 1.1f - std::min(0.4f, radius * 0.02f), false, &pos);
    scene.pushFx(FxEvent::Explosion, pos, radius);

    // Throw unanchored parts away from the blast.
    std::vector<SceneNode*> stack{scene.root()};
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        for (auto& c : n->children) stack.push_back(c.get());
        if (!n->isPart() || n->anchored || scene.isCharacterPart(n)) continue;
        glm::vec3 p = glm::vec3(n->worldMatrix()[3]);
        glm::vec3 d = p - pos;
        float dist = glm::length(d);
        if (dist > radius) continue;
        glm::vec3 dir = dist > 1e-3f ? d / dist : glm::vec3(0, 1, 0);
        n->velocity += (dir + glm::vec3(0, 0.6f, 0)) * 30.0f * power * (1.0f - dist / radius);
    }

    // Hurt the player.
    Player* player = scene.player();
    if (player && player->root() && !player->isDead()) {
        glm::vec3 body = player->position() + glm::vec3(0, 1.3f, 0);
        glm::vec3 d = body - pos;
        float dist = glm::length(d);
        if (dist < radius) {
            float k = 1.0f - dist / radius;
            glm::vec3 dir = dist > 1e-3f ? d / dist : glm::vec3(0, 1, 0);
            glm::vec3 impulse = (dir + glm::vec3(0, 0.8f, 0)) * 22.0f * power * k;
            player->hurt(160.0f * power * k, std::min(1.0f, 0.4f + k * power), impulse);
        }
    }

    // ...and other players (multiplayer host): their computers do the ragdoll.
    for (RemoteCharacter& rc : scene.remotes()) {
        SceneNode* root = scene.findById(rc.rootId);
        if (!root || !rc.alive) continue;
        if (root->hasForceField()) continue;
        glm::vec3 body = root->transform.position + glm::vec3(0, 1.3f, 0);
        glm::vec3 d = body - pos;
        float dist = glm::length(d);
        if (dist >= radius) continue;
        float k = 1.0f - dist / radius;
        glm::vec3 dir = dist > 1e-3f ? d / dist : glm::vec3(0, 1, 0);
        glm::vec3 impulse = (dir + glm::vec3(0, 0.8f, 0)) * 22.0f * power * k;
        rc.humanoid.health = std::max(0.0f, rc.humanoid.health - 160.0f * power * k);
        rc.humanoidDirty = true;
        if (rc.humanoid.health <= 0.0f) rc.kills.push_back({std::min(1.0f, 0.4f + k * power), impulse});
        else                            rc.kills.push_back({-1.0f, impulse});   // just a shove
    }
}

} // namespace Effects
