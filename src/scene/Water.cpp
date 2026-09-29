#include "Water.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Physics.h"
#include "Player.h"
#include "../core/Audio.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kWaveSpeed = 6.0f;    // how fast ripples spread (units / second)
constexpr float kCalm      = 0.55f;   // how quickly waves die down
constexpr int   kMaxPoints = 96;      // grid points along the longest side

const Attribute* attr(const SceneNode* n, const char* name, Attribute::Type t) {
    const Attribute* a = n->findAttribute(name);
    return a && a->type == t ? a : nullptr;
}

} // namespace

void WaterSystem::begin(Scene& scene) {
    end();
    m_active = true;
    scan(scene);
}

// Find the water parts. Also picks up water scripts make, move (rising tides!) or remove.
void WaterSystem::scan(Scene& scene) {
    std::vector<Body> keep;
    scene.forEach([&](SceneNode* n) {
        if (!Player::isWater(n) || n->internal) return;
        for (const SceneNode* p = n; p; p = p->parent) if (!p->visible) return;
        AABB box = Physics::worldBounds(n);
        glm::vec3 size = box.max - box.min;
        if (size.x < 0.2f || size.z < 0.2f) return;
        Body b;
        if (const Body* old = find(n->id)) b = *old;
        b.id = n->id;
        glm::vec3 oldSize = b.max - b.min;
        b.min = box.min;
        b.max = box.max;
        if (b.h.empty() || glm::length(oldSize - size) > 1e-3f) {   // new, or resized: a fresh, calm grid
            b.cell = std::max(0.3f, std::max(size.x, size.z) / (kMaxPoints - 1));
            b.nx = std::max(2, (int)std::ceil(size.x / b.cell) + 1);
            b.nz = std::max(2, (int)std::ceil(size.z / b.cell) + 1);
            b.h.assign((size_t)b.nx * b.nz, 0.0f);
            b.v.assign(b.h.size(), 0.0f);
        }
        b.swell = 0.0f;
        b.flow = glm::vec3(0.0f);
        if (const Attribute* a = attr(n, "Waves", Attribute::Number)) b.swell = std::max(0.0f, (float)a->n);
        if (const Attribute* a = attr(n, "Flow", Attribute::Vector3)) b.flow = a->v;
        b.color = n->color;
        b.transparency = n->transparency;
        keep.push_back(std::move(b));
    });
    m_bodies.swap(keep);
}

void WaterSystem::end() {
    m_bodies.clear();
    m_lastWet.clear();
    m_charPrev.clear();
    m_charWet.clear();
    m_active = false;
    m_time = 0.0f;
}

const WaterSystem::Body* WaterSystem::find(uint64_t id) const {
    for (const Body& b : m_bodies) if (b.id == id) return &b;
    return nullptr;
}

const WaterSystem::Body* WaterSystem::bodyAt(const glm::vec3& p, float pad) const {
    for (const Body& b : m_bodies)
        if (p.x >= b.min.x - pad && p.x <= b.max.x + pad && p.z >= b.min.z - pad && p.z <= b.max.z + pad &&
            p.y >= b.min.y - 0.01f && p.y <= b.max.y + b.swell + 2.5f)
            return &b;
    return nullptr;
}

WaterSystem::Body* WaterSystem::bodyAt(const glm::vec3& p, float pad) {
    return const_cast<Body*>(static_cast<const WaterSystem*>(this)->bodyAt(p, pad));
}

float WaterSystem::heightAt(const Body& b, float x, float z) const {
    float fx = std::clamp((x - b.min.x) / b.cell, 0.0f, (float)(b.nx - 1));
    float fz = std::clamp((z - b.min.z) / b.cell, 0.0f, (float)(b.nz - 1));
    int x0 = std::min((int)fx, b.nx - 2), z0 = std::min((int)fz, b.nz - 2);
    float tx = fx - x0, tz = fz - z0;
    auto H = [&](int i, int k) { return b.h[(size_t)k * b.nx + i]; };
    return (H(x0, z0) * (1 - tx) + H(x0 + 1, z0) * tx) * (1 - tz) + (H(x0, z0 + 1) * (1 - tx) + H(x0 + 1, z0 + 1) * tx) * tz;
}

float WaterSystem::swellAt(const Body& b, float x, float z) const {
    if (b.swell <= 0.0f) return 0.0f;
    const float t = m_time;
    // A few long waves rolling in from different directions.
    return b.swell * (0.5f * std::sin(x * 0.31f + z * 0.12f + t * 1.1f) +
                      0.3f * std::sin(z * 0.43f - x * 0.2f + t * 1.7f) +
                      0.2f * std::sin((x + z) * 0.77f + t * 2.3f));
}

float WaterSystem::surfaceOf(const SceneNode* water, float x, float z) const {
    const Body* b = water ? find(water->id) : nullptr;
    if (!b) return water ? Physics::worldBounds(water).max.y : 0.0f;
    return b->max.y + heightAt(*b, x, z) + swellAt(*b, x, z);
}

bool WaterSystem::at(const glm::vec3& p, float* surface, glm::vec3* flow) const {
    const Body* b = bodyAt(p);
    if (!b) return false;
    float s = b->max.y + heightAt(*b, p.x, p.z) + swellAt(*b, p.x, p.z);
    if (p.y > s) return false;
    if (surface) *surface = s;
    if (flow) *flow = b->flow;
    return true;
}

void WaterSystem::disturb(const glm::vec3& p, float amount, float radius) {
    Body* b = bodyAt(p, radius);
    if (!b) return;
    radius = std::max(radius, b->cell * 1.5f);
    int i0 = std::max(0, (int)std::floor((p.x - radius - b->min.x) / b->cell));
    int i1 = std::min(b->nx - 1, (int)std::ceil((p.x + radius - b->min.x) / b->cell));
    int k0 = std::max(0, (int)std::floor((p.z - radius - b->min.z) / b->cell));
    int k1 = std::min(b->nz - 1, (int)std::ceil((p.z + radius - b->min.z) / b->cell));
    for (int k = k0; k <= k1; ++k)
        for (int i = i0; i <= i1; ++i) {
            float dx = b->min.x + i * b->cell - p.x, dz = b->min.z + k * b->cell - p.z;
            float q = 1.0f - (dx * dx + dz * dz) / (radius * radius);
            if (q <= 0.0f) continue;
            float& h = b->h[(size_t)k * b->nx + i];
            h = std::clamp(h - amount * q * q, -2.0f, 2.0f);
        }
}

void WaterSystem::splash(Scene& scene, const glm::vec3& p, float speed, float size) {
    const Body* b = bodyAt(p, 0.5f);
    if (!b) return;
    size = std::clamp(size, 0.2f, 4.0f);
    disturb(p, std::min(0.6f, speed * 0.02f * size), 0.5f + size * 0.7f);
    int count = (int)std::clamp(speed * size * 4.0f, 8.0f, 90.0f);
    scene.particles().waterSpray(p, count, std::clamp(speed * 0.45f, 2.0f, 10.0f), b->color, size * 0.6f);
    if (m_splashSound <= 0.0f) {
        Audio::play("splash", std::clamp(speed * size / 12.0f, 0.15f, 1.0f), std::clamp(1.3f - size * 0.15f, 0.7f, 1.3f), false, &p);
        m_splashSound = 0.08f;
    }
}

void WaterSystem::touching(Scene& scene, uint64_t id, const glm::vec3& p, float downSpeed, float size) {
    // Only the first touch splashes (it counts as "still in the water" for a moment after).
    auto it = m_lastWet.find(id);
    const bool wasWet = it != m_lastWet.end() && m_time - it->second < 0.4f;
    m_lastWet[id] = m_time;
    if (wasWet || downSpeed < 2.5f) return;
    const Body* b = bodyAt(p);
    if (!b) return;
    splash(scene, glm::vec3(p.x, b->max.y + heightAt(*b, p.x, p.z), p.z), downSpeed, size);
}

void WaterSystem::update(float dt, Scene& scene) {
    if (!m_active) return;
    m_time += dt;
    m_splashSound -= dt;
    if ((m_scanTime -= dt) <= 0.0f) { m_scanTime = 0.25f; scan(scene); }

    // People (the player and NPCs) splash when they jump in, and leave a wake.
    std::vector<std::pair<uint64_t, glm::vec3>> people;
    if (Player* pl = scene.player(); pl && pl->root() && !pl->isDead()) people.push_back({pl->rootId(), pl->position()});
    for (const auto& n : scene.npcs().all())
        if (!n->dead)
            if (SceneNode* r = scene.findById(n->rootId)) people.push_back({n->rootId, r->transform.position});
    for (auto& [id, feet] : people) {
        auto prev = m_charPrev.find(id);
        glm::vec3 vel = prev != m_charPrev.end() && dt > 0.0f ? (feet - prev->second) / dt : glm::vec3(0.0f);
        m_charPrev[id] = feet;
        float surface;
        bool wet = at(feet + glm::vec3(0.0f, 0.05f, 0.0f), &surface);
        bool& was = m_charWet[id];
        if (wet && !was && vel.y < -3.0f) splash(scene, glm::vec3(feet.x, surface, feet.z), -vel.y, 1.2f);
        was = wet;
        // Only a body crossing the surface makes waves (not one deep underwater).
        if (wet && surface - feet.y < 3.0f) {
            float speed = glm::length(glm::vec2(vel.x, vel.z));
            if (speed > 0.5f) disturb(glm::vec3(feet.x, surface, feet.z), std::min(0.05f, speed * dt * 0.25f), 0.9f);
        }
    }

    // The waves themselves.
    for (Body& b : m_bodies) {
        const int steps = std::max(1, (int)std::ceil(dt * kWaveSpeed / (0.5f * b.cell)));
        const float hs = dt / steps;
        const float c2 = kWaveSpeed * kWaveSpeed / (b.cell * b.cell);
        const int nx = b.nx, nz = b.nz;
        for (int s = 0; s < steps; ++s) {
            for (int k = 0; k < nz; ++k)
                for (int i = 0; i < nx; ++i) {
                    size_t id = (size_t)k * nx + i;
                    // Sides of the pool reflect waves back (a missing neighbour counts as this point).
                    float c = b.h[id];
                    float l = i > 0 ? b.h[id - 1] : c, r = i < nx - 1 ? b.h[id + 1] : c;
                    float d = k > 0 ? b.h[id - nx] : c, u = k < nz - 1 ? b.h[id + nx] : c;
                    b.v[id] += c2 * (l + r + d + u - 4.0f * c) * hs;
                    b.v[id] *= std::max(0.0f, 1.0f - kCalm * hs);
                }
            for (size_t id = 0; id < b.h.size(); ++id)
                b.h[id] = std::clamp(b.h[id] + b.v[id] * hs, -2.0f, 2.0f) * (1.0f - 0.02f * hs);
        }
    }

    // Forget things that left the water long ago.
    for (auto i = m_lastWet.begin(); i != m_lastWet.end();)
        i = m_time - i->second > 5.0f ? m_lastWet.erase(i) : std::next(i);
}
