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

namespace {
bool isRotated(const glm::mat4& m) {
    for (int i = 0; i < 3; ++i) {
        glm::vec3 c = glm::vec3(m[i]);
        float len = glm::length(c);
        if (len < 1e-6f) continue;
        c /= len;
        float mx = std::max(std::abs(c.x), std::max(std::abs(c.y), std::abs(c.z)));
        if (mx < 0.9995f) return true;
    }
    return false;
}
} // namespace

OBB Physics::worldOBB(const SceneNode* node) {
    glm::vec3 bmin, bmax;
    localBounds(node->primitiveType, bmin, bmax);
    glm::mat4 m = node->worldMatrix();
    OBB o;
    o.center = glm::vec3(m * glm::vec4((bmin + bmax) * 0.5f, 1.0f));
    for (int i = 0; i < 3; ++i) {
        glm::vec3 col = glm::vec3(m[i]);
        float len = glm::length(col);
        o.axis[i] = len > 1e-8f ? col / len : glm::vec3(i == 0, i == 1, i == 2);
        o.half[i] = len * (bmax[i] - bmin[i]) * 0.5f;
    }
    return o;
}

bool Physics::obbOverlap(const OBB& a, const OBB& b, glm::vec3& normal, float& depth) {
    glm::vec3 axes[15];
    int n = 0;
    for (int i = 0; i < 3; ++i) axes[n++] = a.axis[i];
    for (int i = 0; i < 3; ++i) axes[n++] = b.axis[i];
    int faceAxes = n;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            glm::vec3 c = glm::cross(a.axis[i], b.axis[j]);
            float l = glm::length(c);
            if (l > 1e-3f) axes[n++] = c / l;
        }
    glm::vec3 d = b.center - a.center;
    depth = 1e30f;
    for (int k = 0; k < n; ++k) {
        const glm::vec3& L = axes[k];
        float ra = 0.0f, rb = 0.0f;
        for (int i = 0; i < 3; ++i) {
            ra += a.half[i] * std::abs(glm::dot(a.axis[i], L));
            rb += b.half[i] * std::abs(glm::dot(b.axis[i], L));
        }
        float proj = glm::dot(d, L);
        float overlap = ra + rb - std::abs(proj);
        if (overlap <= 0.0f) return false;
        // Prefer face directions a little: they give steadier pushes than edges.
        float score = k < faceAxes ? overlap : overlap * 1.05f;
        if (score < depth) {
            depth = overlap;
            normal = proj > 0.0f ? -L : L;
        }
    }
    return true;
}

OBB Physics::charOBB(const glm::vec3& f) {
    OBB o;
    o.center = f + glm::vec3(0.0f, kCharHeight * 0.5f, 0.0f);
    o.half = {kCharHalfWidth, kCharHeight * 0.5f, kCharHalfWidth};
    return o;
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
        {
            glm::mat4 m = n->worldMatrix();
            bool rot = isRotated(m);
            m_colliders.push_back({n, worldBounds(n), n->canCollide,
                                   !n->anchored && !hasDynamicAncestor(n), rot,
                                   rot ? worldOBB(n) : OBB{}});
        }
        for (auto& c : n->children) stack.push_back(c.get());
    }
}

bool Physics::blocked(const AABB& box) const {
    OBB ob = OBB::fromAABB(box);
    for (const auto& c : m_colliders) {
        if (!c.solid || !box.overlaps(c.box)) continue;
        if (!c.rotated) return true;
        glm::vec3 n; float d;
        if (obbOverlap(ob, c.obb, n, d)) return true;
    }
    return false;
}

float Physics::pushUp(const AABB& box) const {
    float push = 0.0f;
    float h = box.max.y - box.min.y;
    OBB ob = OBB::fromAABB(box);
    for (const auto& c : m_colliders) {
        if (!c.solid || !box.overlaps(c.box)) continue;
        if (c.rotated) {
            glm::vec3 n; float d;
            if (obbOverlap(ob, c.obb, n, d) && n.y > 0.4f) push = std::max(push, std::min(d / n.y, h + 0.05f));
            continue;
        }
        float d = c.box.max.y - box.min.y;
        if (d > 0.0f && d <= h + 0.05f) push = std::max(push, d);
    }
    return push;
}

bool Physics::solidAt(const glm::vec3& p) const {
    for (const auto& c : m_colliders) {
        if (!(c.solid && p.x > c.box.min.x && p.x < c.box.max.x && p.y > c.box.min.y &&
              p.y < c.box.max.y && p.z > c.box.min.z && p.z < c.box.max.z))
            continue;
        if (!c.rotated) return true;
        glm::vec3 d = p - c.obb.center;
        bool inside = true;
        for (int i = 0; i < 3; ++i)
            if (std::abs(glm::dot(d, c.obb.axis[i])) > c.obb.half[i]) inside = false;
        if (inside) return true;
    }
    return false;
}

bool Physics::resolveSphere(glm::vec3& c, float r, glm::vec3* normal) const {
    bool hit = false;
    for (const auto& col : m_colliders) {
        if (!col.solid) continue;
        if (col.rotated) {
            // Work in the box's own coordinates.
            const OBB& o = col.obb;
            glm::vec3 rel = c - o.center;
            glm::vec3 local(glm::dot(rel, o.axis[0]), glm::dot(rel, o.axis[1]), glm::dot(rel, o.axis[2]));
            glm::vec3 cl = glm::clamp(local, -o.half, o.half);
            glm::vec3 dl = local - cl;
            float dist2 = glm::dot(dl, dl);
            if (dist2 > r * r) continue;
            glm::vec3 nl;
            if (dist2 > 1e-10f) {
                float dist = std::sqrt(dist2);
                nl = dl / dist;
                local = cl + nl * r;
            } else {
                glm::vec3 room = o.half - glm::abs(local);
                int ax = room.x < room.y ? (room.x < room.z ? 0 : 2) : (room.y < room.z ? 1 : 2);
                nl = glm::vec3(0.0f);
                nl[ax] = local[ax] >= 0.0f ? 1.0f : -1.0f;
                local[ax] = nl[ax] * (o.half[ax] + r);
            }
            glm::vec3 n = o.axis[0] * nl.x + o.axis[1] * nl.y + o.axis[2] * nl.z;
            c = o.center + o.axis[0] * local.x + o.axis[1] * local.y + o.axis[2] * local.z;
            if (normal) *normal = n;
            hit = true;
            continue;
        }
        glm::vec3 closest = glm::clamp(c, col.box.min, col.box.max);
        glm::vec3 d = c - closest;
        float dist2 = glm::dot(d, d);
        if (dist2 > r * r) continue;
        glm::vec3 n;
        if (dist2 > 1e-10f) {
            float dist = std::sqrt(dist2);
            n = d / dist;
            c += n * (r - dist);
        } else {
            // Centre is inside the box: leave through the nearest face.
            glm::vec3 toMin = c - col.box.min, toMax = col.box.max - c;
            float best = toMax.y; n = {0, 1, 0};
            if (toMin.y < best) { best = toMin.y; n = {0, -1, 0}; }
            if (toMax.x < best) { best = toMax.x; n = {1, 0, 0}; }
            if (toMin.x < best) { best = toMin.x; n = {-1, 0, 0}; }
            if (toMax.z < best) { best = toMax.z; n = {0, 0, 1}; }
            if (toMin.z < best) { best = toMin.z; n = {0, 0, -1}; }
            c += n * (best + r);
        }
        if (normal) *normal = n;
        hit = true;
    }
    return hit;
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
            if (!c.solid || c.rotated) continue;
            AABB box = characterBox(pos);
            if (!box.overlaps(c.box)) continue;

            float rise = c.box.max.y - pos.y;
            if (wasGrounded && rise > 0.0f && rise <= kStepHeight) {
                glm::vec3 up = pos;
                up.y = c.box.max.y + 0.001f;
                if (!blocked(characterBox(up))) { pos = up; continue; }
            }
            if (c.dynamic) {       // shove loose parts out of the way
                glm::vec3 dir(0.0f);
                dir[axis] = delta[axis] > 0.0f ? 1.0f : -1.0f;
                r.pushed.push_back({c.node, dir});
            }
            if (delta[axis] > 0.0f) pos[axis] = c.box.min[axis] - kCharHalfWidth - 1e-4f;
            else                    pos[axis] = c.box.max[axis] + kCharHalfWidth + 1e-4f;
        }
    }

    // Vertical.
    pos.y += delta.y;
    for (const auto& c : m_colliders) {
        if (!c.solid || c.rotated) continue;
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

    // Tilted parts (ramps etc.): push out along the surface. Surfaces that are
    // flat enough to stand on push straight up, so you don't slide down them.
    auto resolveRotated = [&](glm::vec3& p, MoveResult& res) {
        bool touched = false;
        for (int iter = 0; iter < 4; ++iter) {
            bool any = false;
            for (const auto& c : m_colliders) {
                if (!c.solid || !c.rotated) continue;
                glm::vec3 n; float d;
                if (!obbOverlap(charOBB(p), c.obb, n, d)) continue;
                any = touched = true;
                if (n.y > 0.55f) {
                    p.y += std::min(d / n.y, 1.0f);
                    res.grounded = true;
                    res.groundId = c.node->id;
                } else if (n.y < -0.55f) {
                    p += n * d;
                    res.hitCeiling = true;
                } else {
                    glm::vec3 h(n.x, 0.0f, n.z);
                    float l = glm::length(h);
                    if (l > 1e-4f) p += h / l * std::min(d / l, 1.0f);
                }
            }
            if (!any) break;
        }
        return touched;
    };
    resolveRotated(pos, r);

    // Stick to the ground when walking down slopes and steps (instead of
    // flying off every little edge).
    if (wasGrounded && delta.y <= 0.0f && !r.grounded) {
        glm::vec3 probe = pos - glm::vec3(0.0f, 0.4f, 0.0f);
        float bestY = -1e30f;
        uint64_t bestId = 0;
        for (const auto& c : m_colliders) {
            if (!c.solid) continue;
            if (!c.rotated) {
                AABB box = characterBox(probe);
                if (box.overlaps(c.box) && c.box.max.y <= pos.y + 0.01f && c.box.max.y > bestY) {
                    bestY = c.box.max.y;
                    bestId = c.node->id;
                }
            } else {
                glm::vec3 n; float d;
                if (obbOverlap(charOBB(probe), c.obb, n, d) && n.y > 0.55f) {
                    float y = probe.y + d / n.y;
                    if (y <= pos.y + 0.01f && y > bestY) { bestY = y; bestId = c.node->id; }
                }
            }
        }
        if (bestId) {
            pos.y = bestY;
            r.grounded = true;
            r.groundId = bestId;
        }
    }

    r.position = pos;
    return r;
}

// stepParts (rigid bodies + constraints) lives in RigidBodies.cpp.

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
        OBB bodyObb = OBB::fromAABB(body);
        for (const auto& c : m_colliders) {
            if (!body.overlaps(c.box)) continue;
            if (c.rotated) {
                glm::vec3 nrm; float depth;
                if (!obbOverlap(bodyObb, c.obb, nrm, depth)) continue;
            }
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
