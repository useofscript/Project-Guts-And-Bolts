#include "Physics.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Player.h"

#include <algorithm>
#include <cmath>

namespace {

void localBounds(PrimitiveType type, glm::vec3& bmin, glm::vec3& bmax) {
    if (type == PrimitiveType::Plane) {
        bmin = {-0.5f, -0.02f, -0.5f};
        bmax = { 0.5f,  0.02f,  0.5f};
    } else {
        bmin = {-0.5f, -0.5f, -0.5f};
        bmax = { 0.5f,  0.5f,  0.5f};
    }
}

// Slab-method ray/AABB. Returns the nearest non-negative hit distance.
bool rayAABB(const glm::vec3& ro, const glm::vec3& rd,
             const glm::vec3& bmin, const glm::vec3& bmax, float& tHit) {
    float t0 = -1e30f, t1 = 1e30f;
    for (int i = 0; i < 3; ++i) {
        if (std::abs(rd[i]) < 1e-8f) {
            if (ro[i] < bmin[i] || ro[i] > bmax[i]) return false;
        } else {
            float inv = 1.0f / rd[i];
            float ta = (bmin[i] - ro[i]) * inv;
            float tb = (bmax[i] - ro[i]) * inv;
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            if (t0 > t1) return false;
        }
    }
    tHit = (t0 >= 0.0f) ? t0 : t1;
    return tHit >= 0.0f;
}

bool hasDynamicAncestor(const SceneNode* n) {
    for (const SceneNode* p = n->parent; p; p = p->parent)
        if (p->isPart() && !p->anchored) return true;
    return false;
}

} // namespace

AABB Physics::worldBounds(const SceneNode* node) {
    glm::vec3 bmin, bmax;
    localBounds(node->primitiveType, bmin, bmax);
    glm::mat4 m = node->worldMatrix();
    AABB box{glm::vec3(1e30f), glm::vec3(-1e30f)};
    for (int i = 0; i < 8; ++i) {
        glm::vec3 c{(i & 1) ? bmax.x : bmin.x, (i & 2) ? bmax.y : bmin.y, (i & 4) ? bmax.z : bmin.z};
        glm::vec3 w = glm::vec3(m * glm::vec4(c, 1.0f));
        box.min = glm::min(box.min, w);
        box.max = glm::max(box.max, w);
    }
    return box;
}

SceneNode* Physics::raycast(Scene& scene, const glm::vec3& ro, const glm::vec3& rd,
                            float* distance, const SceneNode* ignore) {
    SceneNode* best = nullptr;
    float bestDist = 1e30f;
    std::vector<SceneNode*> stack{scene.root()};
    while (!stack.empty()) {
        SceneNode* node = stack.back();
        stack.pop_back();
        if (!node->visible || node == ignore) continue;
        for (auto& c : node->children) stack.push_back(c.get());
        if (!node->isPart() || node->internal) continue;

        glm::mat4 world = node->worldMatrix();
        glm::mat4 inv   = glm::inverse(world);
        glm::vec3 lro   = glm::vec3(inv * glm::vec4(ro, 1.0f));
        glm::vec3 lrd   = glm::vec3(inv * glm::vec4(rd, 0.0f));
        glm::vec3 bmin, bmax;
        localBounds(node->primitiveType, bmin, bmax);

        float tLocal;
        if (rayAABB(lro, lrd, bmin, bmax, tLocal)) {
            // Compare in world space so objects of different scales are fair.
            glm::vec3 hit = glm::vec3(world * glm::vec4(lro + lrd * tLocal, 1.0f));
            float d = glm::dot(hit - ro, rd);
            if (d > 0.0f && d < bestDist) { bestDist = d; best = node; }
        }
    }
    if (distance) *distance = bestDist;
    return best;
}

AABB Physics::characterBox(const glm::vec3& f) {
    return {{f.x - kCharHalfWidth, f.y, f.z - kCharHalfWidth},
            {f.x + kCharHalfWidth, f.y + kCharHeight, f.z + kCharHalfWidth}};
}

void Physics::reset() {
    m_colliders.clear();
    m_touching.clear();
}

void Physics::gather(Scene& scene) {
    m_colliders.clear();
    // Manual walk so whole subtrees (hidden models, the character) can be skipped.
    std::vector<SceneNode*> stack{scene.root()};
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        if (!n->visible) continue;
        if (scene.isCharacterPart(n)) continue;
        if (n->isPart() && !n->internal)
            m_colliders.push_back({n, worldBounds(n), n->canCollide,
                                   !n->anchored && !hasDynamicAncestor(n)});
        for (auto& c : n->children) stack.push_back(c.get());
    }
}

bool Physics::blocked(const AABB& box) const {
    for (const auto& c : m_colliders)
        if (c.solid && box.overlaps(c.box)) return true;
    return false;
}

float Physics::pushUp(const AABB& box) const {
    float push = 0.0f;
    float h = box.max.y - box.min.y;
    for (const auto& c : m_colliders) {
        if (!c.solid || !box.overlaps(c.box)) continue;
        float d = c.box.max.y - box.min.y;
        if (d > 0.0f && d <= h + 0.05f) push = std::max(push, d);
    }
    return push;
}

Physics::MoveResult Physics::moveCharacter(const glm::vec3& feet, const glm::vec3& delta,
                                           bool wasGrounded) const {
    MoveResult r;
    glm::vec3 pos = feet;

    // Horizontal axes first (X then Z), with a small step-up for stairs / curbs.
    for (int axis : {0, 2}) {
        if (std::abs(delta[axis]) < 1e-7f) continue;
        pos[axis] += delta[axis];
        for (const auto& c : m_colliders) {
            if (!c.solid) continue;
            AABB box = characterBox(pos);
            if (!box.overlaps(c.box)) continue;

            float rise = c.box.max.y - pos.y;
            if (wasGrounded && rise > 0.0f && rise <= kStepHeight) {
                glm::vec3 up = pos;
                up.y = c.box.max.y + 0.001f;
                if (!blocked(characterBox(up))) { pos = up; continue; }
            }
            if (delta[axis] > 0.0f) pos[axis] = c.box.min[axis] - kCharHalfWidth - 1e-4f;
            else                    pos[axis] = c.box.max[axis] + kCharHalfWidth + 1e-4f;
        }
    }

    // Vertical.
    pos.y += delta.y;
    for (const auto& c : m_colliders) {
        if (!c.solid) continue;
        AABB box = characterBox(pos);
        if (!box.overlaps(c.box)) continue;
        if (delta.y <= 0.0f) {
            pos.y = c.box.max.y;
            r.grounded = true;
            r.groundId = c.node->id;
        } else {
            pos.y = c.box.min.y - kCharHeight - 1e-4f;
            r.hitCeiling = true;
        }
    }

    r.position = pos;
    return r;
}

void Physics::stepParts(Scene& scene, float dt, std::vector<uint64_t>& fallen) {
    const float g = scene.world().gravity;
    for (size_t i = 0; i < m_colliders.size(); ++i) {
        Collider& me = m_colliders[i];
        if (!me.dynamic) continue;
        SceneNode* n = me.node;

        n->velocity.y -= g * dt;
        glm::vec3 worldDelta = n->velocity * dt;

        // Resolve against every other solid part, one axis at a time.
        AABB box = me.box;
        for (int axis : {1, 0, 2}) {
            float d = worldDelta[axis];
            if (std::abs(d) < 1e-8f) continue;
            box.min[axis] += d;
            box.max[axis] += d;
            if (!me.solid) continue;
            for (size_t j = 0; j < m_colliders.size(); ++j) {
                if (j == i) continue;
                const Collider& o = m_colliders[j];
                if (!o.solid || !box.overlaps(o.box)) continue;
                if (o.node->isAncestorOf(n) || n->isAncestorOf(o.node)) continue;
                float fix = (d > 0.0f) ? (o.box.min[axis] - box.max[axis])
                                       : (o.box.max[axis] - box.min[axis]);
                box.min[axis] += fix;
                box.max[axis] += fix;
                worldDelta[axis] += fix;
                n->velocity[axis] = 0.0f;
                if (axis == 1 && d < 0.0f) {
                    // Resting on something: ground friction.
                    float f = std::max(0.0f, 1.0f - 6.0f * dt);
                    n->velocity.x *= f;
                    n->velocity.z *= f;
                }
            }
        }

        // Apply the world-space move in the parent's local space.
        glm::mat3 toLocal(1.0f);
        if (n->parent) toLocal = glm::inverse(glm::mat3(n->parent->worldMatrix()));
        n->transform.position += toLocal * worldDelta;
        me.box = box;

        if (box.max.y < scene.world().fallenPartsHeight) fallen.push_back(n->id);
    }
}

void Physics::collectTouches(Scene& scene, std::vector<TouchEvent>& out) {
    std::set<std::pair<uint64_t, uint64_t>> now;

    auto report = [&](uint64_t part, uint64_t pairKey, uint64_t other) {
        auto key = std::make_pair(part, pairKey);
        if (!now.insert(key).second) return;
        if (!m_touching.count(key)) out.push_back({part, other});
    };

    // Character vs parts (every part, even non-collidable ones like coins).
    Player* player = scene.player();
    SceneNode* charRoot = player ? player->root() : nullptr;
    if (charRoot && !player->isDead()) {
        AABB body = characterBox(player->position()).inflated(0.05f);
        for (const auto& c : m_colliders) {
            if (!body.overlaps(c.box)) continue;
            // Report the limb that actually touched (legs for floors, etc.).
            uint64_t limb = 0;
            AABB hitBox = c.box.inflated(0.1f);
            for (auto& ch : charRoot->children) {
                if (!ch->isPart()) continue;
                if (worldBounds(ch.get()).overlaps(hitBox)) { limb = ch->id; break; }
            }
            if (!limb)
                if (SceneNode* hrp = charRoot->findChild("HumanoidRootPart")) limb = hrp->id;
            report(c.node->id, charRoot->id, limb ? limb : charRoot->id);
        }
    }

    // Unanchored parts vs everything they bump into.
    for (size_t i = 0; i < m_colliders.size(); ++i) {
        if (!m_colliders[i].dynamic) continue;
        AABB a = m_colliders[i].box.inflated(0.02f);
        for (size_t j = 0; j < m_colliders.size(); ++j) {
            if (i == j || !a.overlaps(m_colliders[j].box)) continue;
            uint64_t idA = m_colliders[i].node->id, idB = m_colliders[j].node->id;
            report(idA, idB, idB);
            report(idB, idA, idA);
        }
    }

    m_touching.swap(now);
}
