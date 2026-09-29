#include "Npc.h"
#include "Scene.h"
#include "Physics.h"
#include "../core/Audio.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace {

const char* kLimbs[] = {"Left Arm", "Right Arm", "Left Leg", "Right Leg"};

float approach(float cur, float target, float rate, float dt) {
    return cur + (target - cur) * std::min(1.0f, rate * dt);
}

std::mt19937& rng() { static std::mt19937 r{777u}; return r; }
float rand01() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng()); }

bool hasTag(const SceneNode* n, const char* tag) {
    return std::find(n->tags.begin(), n->tags.end(), tag) != n->tags.end();
}

// Is this point where the feet are? (The bottom of the body parts, under the root part.)
glm::vec3 feetOf(SceneNode* model) {
    SceneNode* hrp = model->findChild("HumanoidRootPart");
    glm::vec3 feet = glm::vec3(hrp->worldMatrix()[3]);
    float bottom = 1e9f;
    for (auto& c : model->children)
        if (c->isPart()) bottom = std::min(bottom, Physics::worldBounds(c.get()).min.y);
    if (bottom < 1e8f) feet.y = bottom;
    return feet;
}

} // namespace

bool NpcSystem::isRig(Scene& scene, const SceneNode* m) {
    if (!m || m->kind != NodeKind::Model || m->internal || !m->parent) return false;
    if (scene.isCharacterRoot(m->id) || scene.isCharacterPart(m)) return false;
    const SceneNode* hrp = m->findChild("HumanoidRootPart");
    const SceneNode* torso = m->findChild("Torso");
    const SceneNode* head = m->findChild("Head");
    if (!hrp || !torso || !head || !hrp->isPart() || !torso->isPart() || !head->isPart()) return false;
    // Hidden = kept in storage (ServerStorage, ReplicatedStorage) as a template.
    for (const SceneNode* n = m; n; n = n->parent) if (!n->visible) return false;
    // A character inside another character (or a tool) isn't walking around.
    for (const SceneNode* n = m->parent; n; n = n->parent)
        if (n->isTool() || n->isPart()) return false;
    return true;
}

void NpcSystem::begin(Scene& scene) {
    end();
    m_active = true;
    std::vector<SceneNode*> rigs;
    scene.forEach([&](SceneNode* n) { if (isRig(scene, n)) rigs.push_back(n); });
    for (SceneNode* r : rigs) adopt(scene, r);
}

void NpcSystem::end() {
    m_npcs.clear();
    died.clear();
    finished.clear();
    m_active = false;
}

Npc* NpcSystem::find(uint64_t rootId) {
    for (auto& n : m_npcs) if (n->rootId == rootId) return n.get();
    return nullptr;
}

Npc* NpcSystem::find(Scene& scene, uint64_t rootId, bool adoptIt) {
    if (Npc* n = find(rootId)) return n;
    if (!adoptIt || !m_active) return nullptr;
    SceneNode* m = scene.findById(rootId);
    return isRig(scene, m) ? adopt(scene, m) : nullptr;
}

Npc* NpcSystem::adopt(Scene& scene, SceneNode* model) {
    // Put the model's origin at its feet (Roblox rigs have it anywhere), so it
    // turns around its middle and stands where the physics expects.
    glm::vec3 feet = feetOf(model);
    glm::vec3 local = glm::vec3(glm::inverse(model->worldMatrix()) * glm::vec4(feet, 1.0f));
    for (auto& c : model->children) c->transform.position -= local;
    glm::mat4 parentW = model->parent ? model->parent->worldMatrix() : glm::mat4(1.0f);
    model->transform.position = glm::vec3(glm::inverse(parentW) * glm::vec4(feet, 1.0f));

    auto npc = std::make_unique<Npc>();
    npc->rootId = model->id;
    npc->zombie = hasTag(model, "Zombie") || model->name.find("Zombie") != std::string::npos;
    // Its body parts are carried by the Humanoid: held in place, and not solid
    // (so the player and other NPCs don't get stuck on them, and it doesn't trip on itself).
    for (auto& c : model->children) {
        if (!c->isPart()) continue;
        c->anchored = true;
        c->canCollide = false;
        npc->rest[c->id] = c->transform;
    }
    // Settings a builder can give the model as attributes.
    if (const Attribute* a = model->findAttribute("WalkSpeed"); a && a->type == Attribute::Number) npc->humanoid.walkSpeed = (float)a->n;
    if (const Attribute* a = model->findAttribute("MaxHealth"); a && a->type == Attribute::Number)
        npc->humanoid.maxHealth = npc->humanoid.health = std::max(1.0f, (float)a->n);
    npc->lastHealth = npc->humanoid.health;
    scene.markDirty();
    m_npcs.push_back(std::move(npc));
    return m_npcs.back().get();
}

void NpcSystem::kill(uint64_t rootId, float force, const glm::vec3& impulse) {
    if (Npc* n = find(rootId)) {
        n->humanoid.health = 0.0f;
        n->pendingForce = std::max(n->pendingForce, force);
        n->pendingImpulse += impulse;
    }
}

void NpcSystem::update(float dt, Scene& scene, Physics& physics) {
    if (!m_active) return;
    // Now and then, look for characters scripts have put in the world.
    if ((m_scanTime -= dt) <= 0.0f) {
        m_scanTime = 0.5f;
        std::vector<SceneNode*> rigs;
        scene.forEach([&](SceneNode* n) { if (isRig(scene, n) && !find(n->id)) rigs.push_back(n); });
        for (SceneNode* r : rigs) adopt(scene, r);
    }
    // Forget ones that were destroyed.
    m_npcs.erase(std::remove_if(m_npcs.begin(), m_npcs.end(),
                                [&](const std::unique_ptr<Npc>& n) { return !scene.findById(n->rootId); }),
                 m_npcs.end());

    for (auto& n : m_npcs) {
        SceneNode* root = scene.findById(n->rootId);
        if (n->dead) { updateDeath(*n, root, dt, scene, physics); continue; }
        step(*n, root, dt, scene, physics);
    }

    // Don't let NPCs stand inside each other: nudge apart ones that are too close.
    for (size_t i = 0; i < m_npcs.size(); ++i)
        for (size_t j = i + 1; j < m_npcs.size(); ++j) {
            Npc& a = *m_npcs[i]; Npc& b = *m_npcs[j];
            if (a.dead || b.dead) continue;
            SceneNode* ra = scene.findById(a.rootId); SceneNode* rb = scene.findById(b.rootId);
            glm::vec3 d = ra->transform.position - rb->transform.position;
            if (std::abs(d.y) > 2.0f) continue;
            d.y = 0.0f;
            float len = glm::length(d);
            if (len >= 1.0f) continue;
            glm::vec3 push = (len > 1e-3f ? d / len : glm::vec3(1, 0, 0)) * (1.0f - len) * 0.5f;
            if (!physics.solidAt(ra->transform.position + push + glm::vec3(0, 1.3f, 0))) ra->transform.position += push;
            if (!physics.solidAt(rb->transform.position - push + glm::vec3(0, 1.3f, 0))) rb->transform.position -= push;
        }
}

void NpcSystem::step(Npc& n, SceneNode* r, float dt, Scene& scene, Physics& physics) {
    Humanoid& h = n.humanoid;
    // Hurt since last frame: a grunt, and gore if the game has it on.
    if (h.health < n.lastHealth - 0.5f) {
        glm::vec3 at = r->transform.position + glm::vec3(0, 1.5f, 0);
        if (h.health > 0.0f) Audio::play("hit", 0.5f, 0.9f, false, &at);
        if (scene.goreEnabled())
            scene.particles().spray(scene.goreKind(), at, glm::vec3(0, 1, 0),
                                    (int)std::clamp((n.lastHealth - h.health) * 0.3f, 3.0f, 20.0f), 3.0f);
    }
    n.lastHealth = h.health;
    if (h.health <= 0.0f) { startDeath(n, r, scene); return; }

    glm::vec3 pos = r->transform.position;

    // Where to go: a MoveTo point, or a Move direction.
    glm::vec3 dir = n.moveDir;
    if (n.hasTarget) {
        glm::vec3 d = n.target - pos;
        d.y = 0.0f;
        float len = glm::length(d);
        n.targetTime += dt;
        if (len < 0.5f) {
            n.hasTarget = false;
            finished.push_back({n.rootId, true});
            dir = glm::vec3(0.0f);
        } else if (n.targetTime > 8.0f) {
            n.hasTarget = false;
            finished.push_back({n.rootId, false});
            dir = glm::vec3(0.0f);
        } else {
            // Slow down right at the end so it doesn't overshoot.
            dir = d / len * std::min(1.0f, len / std::max(0.1f, h.walkSpeed * dt) );
        }
    }
    dir.y = 0.0f;
    float len = glm::length(dir);
    bool moving = len > 1e-4f;
    glm::vec3 horiz(0.0f);
    if (moving) {
        horiz = dir / len;
        if (h.autoRotate) {
            float target = glm::degrees(std::atan2(horiz.x, horiz.z));
            float cur = r->transform.rotation.y;
            float diff = std::fmod(target - cur + 540.0f, 360.0f) - 180.0f;
            r->transform.rotation.y = cur + diff * std::min(1.0f, dt * 10.0f);
        }
        horiz *= std::min(1.0f, len);
    }

    // Jumping: asked to (Humanoid.Jump), or stuck against something while walking.
    if (n.grounded && (n.jump || n.stuckTime > 0.25f)) {
        n.velocity.y = h.jumpPower;
        n.grounded = false;
        n.stuckTime = 0.0f;
    }
    n.jump = false;
    n.velocity.y -= scene.world().gravity * dt;

    glm::vec3 delta = horiz * h.walkSpeed * dt;
    delta.x += n.velocity.x * dt;
    delta.z += n.velocity.z * dt;
    delta.y = n.velocity.y * dt;
    float drag = std::max(0.0f, 1.0f - (n.grounded ? 8.0f : 0.8f) * dt);
    n.velocity.x *= drag;
    n.velocity.z *= drag;

    Physics::MoveResult res = physics.moveCharacter(pos, delta, n.grounded);
    glm::vec2 wanted(delta.x, delta.z), got(res.position.x - pos.x, res.position.z - pos.z);
    if (moving && n.grounded && glm::length(wanted) > 1e-4f && glm::length(got) < glm::length(wanted) * 0.3f)
        n.stuckTime += dt;
    else
        n.stuckTime = 0.0f;
    n.grounded = res.grounded;
    if (res.grounded && n.velocity.y < 0.0f) n.velocity.y = 0.0f;
    if (res.hitCeiling && n.velocity.y > 0.0f) n.velocity.y = 0.0f;
    r->transform.position = res.position;
    if (res.position.y < scene.world().fallenPartsHeight) h.health = 0.0f;

    animate(n, r, dt, moving && glm::length(got) > 1e-4f);
}

void NpcSystem::animate(Npc& n, SceneNode* r, float dt, bool moving) {
    n.swing = approach(n.swing, (moving && n.grounded) ? 45.0f : 0.0f, 10.0f, dt);
    n.air = approach(n.air, n.grounded ? 0.0f : 1.0f, 10.0f, dt);
    if (moving) n.walkPhase += dt * (2.0f + n.humanoid.walkSpeed * 0.9f);
    const float s = std::sin(n.walkPhase) * n.swing;
    float angles[4] = {
        s * (1 - n.air) + (-165.0f) * n.air,
        -s * (1 - n.air) + (-165.0f) * n.air,
        -s * (1 - n.air) + 12.0f * n.air,
        s * (1 - n.air) - 12.0f * n.air,
    };
    if (n.zombie) {   // arms straight out in front, swaying a little
        angles[0] = -90.0f + s * 0.15f;
        angles[1] = -90.0f - s * 0.15f;
    }
    for (int i = 0; i < 4; ++i) {
        SceneNode* limb = r->findChild(kLimbs[i]);
        if (!limb) continue;
        auto it = n.rest.find(limb->id);
        if (it == n.rest.end()) continue;
        const Transform& rest = it->second;
        // Swing around the shoulder / hip (the top of the limb).
        float a = glm::radians(angles[i]);
        float hh = rest.scale.y * 0.5f;
        glm::vec3 pivot = rest.position + glm::vec3(0.0f, hh, 0.0f);
        limb->transform.position = pivot + glm::vec3(0.0f, -hh * std::cos(a), -hh * std::sin(a));
        limb->transform.rotation = rest.rotation + glm::vec3(angles[i], 0.0f, 0.0f);
    }
}

void NpcSystem::startDeath(Npc& n, SceneNode* r, Scene& scene) {
    n.dead = true;
    n.deadTime = 0.0f;
    n.hasTarget = false;
    died.push_back(n.rootId);
    glm::vec3 at = r->transform.position + glm::vec3(0, 2, 0);
    Audio::play("oof", 0.6f, n.zombie ? 0.8f : 1.0f, false, &at);

    if (scene.world().deathStyle == DeathStyle::Ragdoll) {
        n.ragdoll.start(scene, r, n.velocity, n.pendingImpulse, n.pendingForce);
        return;
    }
    // Classic: the pieces fall apart (moved into world space so each can tumble).
    glm::mat4 rootM = r->transform.matrix();
    float yaw = r->transform.rotation.y;
    glm::vec3 center = r->transform.position + glm::vec3(0, 1.3f, 0);
    for (auto& c : r->children) {
        if (!c->isPart()) continue;
        c->transform.position = glm::vec3(rootM * glm::vec4(c->transform.position, 1.0f));
        c->transform.rotation.y += yaw;
        glm::vec3 out = c->transform.position - center;
        out.y = 0.0f;
        if (glm::length(out) > 1e-3f) out = glm::normalize(out);
        glm::vec3 vel = out * (1.5f + rand01() * 2.0f) + glm::vec3(rand01() - 0.5f, 3.0f + rand01() * 2.5f, rand01() - 0.5f);
        n.debris.push_back({c->id, vel + n.pendingImpulse, (glm::vec3(rand01(), rand01(), rand01()) - 0.5f) * 500.0f});
    }
    r->transform.position = glm::vec3(0.0f);
    r->transform.rotation = glm::vec3(0.0f);
    r->transform.scale = glm::vec3(1.0f);
}

void NpcSystem::updateDeath(Npc& n, SceneNode* r, float dt, Scene& scene, Physics& physics) {
    (void)r;
    n.deadTime += dt;
    if (n.ragdoll.active()) {
        // After a while the body stops moving (and stops costing anything).
        if (n.deadTime < 8.0f) n.ragdoll.update(dt, scene, physics);
        return;
    }
    if (n.deadTime > 6.0f) return;
    const float g = scene.world().gravity;
    for (auto& d : n.debris) {
        SceneNode* p = scene.findById(d.id);
        if (!p) continue;
        d.vel.y -= g * dt;
        p->transform.position += d.vel * dt;
        p->transform.rotation += d.spin * dt;
        float push = physics.pushUp(Physics::worldBounds(p));
        if (push > 0.0f) {
            p->transform.position.y += push;
            if (d.vel.y < 0.0f) d.vel.y = -d.vel.y * 0.25f;
            d.vel.x *= 0.8f; d.vel.z *= 0.8f;
            d.spin *= 0.8f;
        }
    }
}
