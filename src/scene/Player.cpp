#include "Player.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Physics.h"
#include "../renderer/MeshLibrary.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <memory>
#include <random>

namespace {

const glm::vec3 kYellow = {1.00f, 0.84f, 0.30f};
const glm::vec3 kBlue   = {0.20f, 0.45f, 0.85f};
const glm::vec3 kGreen  = {0.32f, 0.60f, 0.26f};
const glm::vec3 kBlack  = {0.05f, 0.05f, 0.05f};

const char* kLimbs[] = {"Left Arm", "Right Arm", "Left Leg", "Right Leg"};

bool startsWith(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

float approach(float cur, float target, float rate, float dt) {
    return cur + (target - cur) * std::min(1.0f, rate * dt);
}

std::mt19937& rng() { static std::mt19937 r{1234u}; return r; }
float rand01() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng()); }

} // namespace

// ---------------------------------------------------------------------------
// Building the rig
// ---------------------------------------------------------------------------

void Player::resetSettings() {
    m_humanoid = Humanoid{};
    m_hat = HatStyle::None;
}

SceneNode* Player::root() const {
    return m_rootId ? m_scene->findById(m_rootId) : nullptr;
}

SceneNode* Player::part(const char* name) const {
    SceneNode* r = root();
    return r ? r->findChild(name) : nullptr;
}

void Player::build() {
    if (SceneNode* old = root()) m_scene->removeNode(old);

    // Container node for the whole character (origin at the feet).
    auto model = std::make_unique<SceneNode>("Player", NodeKind::Model);
    model->transform.position = m_spawn;
    SceneNode* r = m_scene->insert(std::move(model));
    m_rootId = r->id;

    auto addPart = [&](SceneNode* parent, const std::string& name, PrimitiveType shape,
                       glm::vec3 pos, glm::vec3 scale, glm::vec3 col,
                       bool internal = false, bool visible = true) -> SceneNode* {
        auto node = std::make_unique<SceneNode>(name);
        node->primitiveType      = shape;
        node->mesh               = MeshLibrary::get(shape);
        node->transform.position = pos;
        node->transform.scale    = scale;
        node->color    = col;
        node->internal = internal;
        node->visible  = visible;
        return parent->addChild(std::move(node));
    };
    const auto Cube = PrimitiveType::Cube;

    // Roblox part order. HumanoidRootPart is an invisible reference part.
    addPart(r, "HumanoidRootPart", Cube, {0.0f, 1.5f, 0.0f}, {1.0f, 1.0f, 0.5f}, kBlue, false, false);
    addPart(r, "Torso",            Cube, {0.0f, 1.5f, 0.0f}, {1.0f, 1.0f, 0.5f}, kBlue);
    // The classic Roblox head is a rounded cylinder.
    SceneNode* head =
    addPart(r, "Head", PrimitiveType::Cylinder, {0.0f, 2.325f, 0.0f}, {0.72f, 0.65f, 0.72f}, kYellow);
    addPart(r, "Left Arm",         Cube, {-0.75f, 1.5f, 0.0f}, {0.5f, 1.0f, 0.5f}, kYellow);
    addPart(r, "Right Arm",        Cube, { 0.75f, 1.5f, 0.0f}, {0.5f, 1.0f, 0.5f}, kYellow);
    addPart(r, "Left Leg",         Cube, {-0.25f, 0.5f, 0.0f}, {0.5f, 1.0f, 0.5f}, kGreen);
    addPart(r, "Right Leg",        Cube, { 0.25f, 0.5f, 0.0f}, {0.5f, 1.0f, 0.5f}, kGreen);

    // --- Smiley face on the front (+Z) of the head, in head-local space ---
    addPart(head, "Eye.L", Cube, {-0.18f, 0.12f, 0.5f}, {0.13f, 0.16f, 0.06f}, kBlack, true);
    addPart(head, "Eye.R", Cube, { 0.18f, 0.12f, 0.5f}, {0.13f, 0.16f, 0.06f}, kBlack, true);

    // Smile: small cubes along an upward-opening curve.
    const float sx[5] = {-0.24f, -0.12f, 0.0f, 0.12f, 0.24f};
    for (int i = 0; i < 5; ++i) {
        float x = sx[i];
        float y = -0.20f + 0.10f * (x / 0.24f) * (x / 0.24f);   // middle lowest
        // Follow the curve of the cylinder so the smile sits on the surface.
        float z = std::sqrt(std::max(0.0f, 0.25f - x * x)) + 0.01f;
        addPart(head, "Smile", Cube, {x, y, z}, {0.08f, 0.09f, 0.06f}, kBlack, true);
    }

    setHat(m_hat);
    m_scene->markDirty();
}

glm::vec3 Player::position() const {
    SceneNode* r = root();
    return r ? r->transform.position : glm::vec3(0.0f);
}

glm::vec3 Player::focusPoint() const {
    if (m_dead)
        if (SceneNode* t = part("Torso")) return glm::vec3(t->worldMatrix()[3]);
    return position() + glm::vec3(0.0f, 1.6f, 0.0f);
}

// ---------------------------------------------------------------------------
// Appearance
// ---------------------------------------------------------------------------

BodyColors Player::bodyColors() const {
    auto col = [&](const char* n, glm::vec3 fallback) {
        SceneNode* p = part(n);
        return p ? p->color : fallback;
    };
    return {col("Head", kYellow), col("Torso", kBlue), col("Left Arm", kYellow),
            col("Right Arm", kYellow), col("Left Leg", kGreen), col("Right Leg", kGreen)};
}

void Player::setBodyColors(const BodyColors& c) {
    auto set = [&](const char* n, glm::vec3 v) { if (SceneNode* p = part(n)) p->color = v; };
    set("Head", c.head);      set("Torso", c.torso);
    set("Left Arm", c.leftArm); set("Right Arm", c.rightArm);
    set("Left Leg", c.leftLeg); set("Right Leg", c.rightLeg);
    if (SceneNode* hrp = part("HumanoidRootPart")) hrp->color = c.torso;
}

std::vector<std::pair<const char*, BodyColors>> Player::colorPresets() {
    const glm::vec3 skin  = {0.96f, 0.80f, 0.65f};
    const glm::vec3 grey  = {0.64f, 0.64f, 0.66f};
    const glm::vec3 dark  = {0.12f, 0.12f, 0.14f};
    return {
        {"Classic Noob", {kYellow, kBlue, kYellow, kYellow, kGreen, kGreen}},
        {"Guest",        {grey, dark, grey, grey, {0.85f,0.85f,0.87f}, {0.85f,0.85f,0.87f}}},
        {"Builder",      {skin, {0.93f,0.55f,0.16f}, skin, skin, {0.35f,0.27f,0.20f}, {0.35f,0.27f,0.20f}}},
        {"Ninja",        {dark, dark, dark, dark, dark, dark}},
        {"Red Team",     {skin, {0.80f,0.18f,0.16f}, skin, skin, dark, dark}},
        {"Blue Team",    {skin, {0.16f,0.36f,0.85f}, skin, skin, dark, dark}},
        {"Robot",        {{0.72f,0.74f,0.78f}, {0.45f,0.48f,0.53f}, {0.72f,0.74f,0.78f},
                          {0.72f,0.74f,0.78f}, {0.45f,0.48f,0.53f}, {0.45f,0.48f,0.53f}}},
    };
}

const char* Player::hatName(HatStyle s) {
    switch (s) {
        case HatStyle::TopHat: return "Top Hat";
        case HatStyle::Cap:    return "Cap";
        case HatStyle::Crown:  return "Crown";
        default:               return "None";
    }
}

void Player::setHat(HatStyle style) {
    m_hat = style;
    SceneNode* r = root();
    if (!r) return;

    // Remove the old hat pieces.
    std::vector<SceneNode*> old;
    for (auto& c : r->children)
        if (startsWith(c->name, "Hat")) old.push_back(c.get());
    for (auto* o : old) m_scene->removeNode(o);

    auto add = [&](const char* name, PrimitiveType shape, glm::vec3 pos, glm::vec3 scale,
                   glm::vec3 col, Material mat = Material::Plastic) {
        auto n = std::make_unique<SceneNode>(name);
        n->primitiveType      = shape;
        n->mesh               = MeshLibrary::get(shape);
        n->transform.position = pos;
        n->transform.scale    = scale;
        n->color              = col;
        n->material           = mat;
        r->addChild(std::move(n));
    };
    const float top = 2.65f;   // top of the head
    switch (style) {
        case HatStyle::TopHat:
            add("Hat Brim", PrimitiveType::Cylinder, {0, top + 0.025f, 0}, {1.0f, 0.05f, 1.0f}, kBlack);
            add("Hat",      PrimitiveType::Cylinder, {0, top + 0.325f, 0}, {0.62f, 0.55f, 0.62f}, kBlack);
            add("Hat Band", PrimitiveType::Cylinder, {0, top + 0.11f, 0}, {0.64f, 0.1f, 0.64f}, {0.75f, 0.12f, 0.12f});
            break;
        case HatStyle::Cap:
            add("Hat",       PrimitiveType::Sphere, {0, top - 0.05f, 0}, {0.78f, 0.5f, 0.78f}, {0.85f, 0.15f, 0.15f});
            add("Hat Visor", PrimitiveType::Cube,   {0, top - 0.03f, 0.46f}, {0.52f, 0.04f, 0.36f}, {0.85f, 0.15f, 0.15f});
            break;
        case HatStyle::Crown:
            add("Hat",       PrimitiveType::Cylinder, {0, top + 0.14f, 0}, {0.74f, 0.28f, 0.74f}, {1.0f, 0.78f, 0.2f}, Material::Metal);
            add("Hat Jewel", PrimitiveType::Cube, {0, top + 0.14f, 0.37f}, {0.12f, 0.12f, 0.05f}, {0.9f, 0.1f, 0.2f}, Material::Neon);
            break;
        default: break;
    }
    m_scene->markDirty();
}

// ---------------------------------------------------------------------------
// Play mode
// ---------------------------------------------------------------------------

namespace {
// Top of the first visible "SpawnLocation" part, if there is one.
bool findSpawnLocation(SceneNode* node, glm::vec3& out) {
    if (node->name == "SpawnLocation" && node->isPart() && node->visible) {
        AABB b = Physics::worldBounds(node);
        out = {(b.min.x + b.max.x) * 0.5f, b.max.y + 0.001f, (b.min.z + b.max.z) * 0.5f};
        return true;
    }
    for (auto& c : node->children)
        if (findSpawnLocation(c.get(), out)) return true;
    return false;
}
} // namespace

void Player::beginPlay() {
    m_rest.clear();
    m_debris.clear();
    SceneNode* r = root();
    if (r) {
        m_rootRest = r->transform;
        for (auto& c : r->children) m_rest[c->id] = c->transform;
    }
    m_dead = m_diedFlag = false;
    respawn();
}

void Player::endPlay() {
    m_dead = false;
    m_debris.clear();
    m_rest.clear();
}

void Player::respawn() {
    SceneNode* r = root();
    if (!r) return;
    for (auto& c : r->children) {
        auto it = m_rest.find(c->id);
        if (it != m_rest.end()) c->transform = it->second;
    }
    glm::vec3 spawnAt = m_spawn;
    findSpawnLocation(m_scene->root(), spawnAt);
    r->transform = m_rootRest;
    r->transform.position = spawnAt;

    m_humanoid.health = m_humanoid.maxHealth;
    m_velocity  = glm::vec3(0.0f);
    m_grounded  = false;
    m_groundId  = 0;
    m_walkPhase = m_swing = m_airBlend = 0.0f;
    m_dead      = false;
    m_debris.clear();
}

bool Player::consumeDied() {
    bool d = m_diedFlag;
    m_diedFlag = false;
    return d;
}

void Player::update(float dt, const glm::vec3& moveDir, bool jump, Physics& physics) {
    SceneNode* r = root();
    if (!r) return;

    if (m_dead) { updateDeath(dt, physics); return; }
    if (m_humanoid.health <= 0.0f) { startDeath(); return; }

    glm::vec3 pos = r->transform.position;

    // Ride moving platforms: follow whatever we stood on last frame.
    if (m_groundId) {
        if (SceneNode* g = m_scene->findById(m_groundId)) {
            glm::vec3 now = glm::vec3(g->worldMatrix()[3]);
            pos += now - m_groundPrev;
        }
    }

    // Horizontal movement.
    glm::vec3 horiz = {moveDir.x, 0.0f, moveDir.z};
    float len = glm::length(horiz);
    bool moving = len > 1e-4f;
    if (moving) {
        horiz /= len;
        if (m_humanoid.autoRotate) {
            // Turn smoothly towards the direction of travel (shortest way round).
            float target = glm::degrees(std::atan2(horiz.x, horiz.z));
            float cur    = r->transform.rotation.y;
            float diff   = std::fmod(target - cur + 540.0f, 360.0f) - 180.0f;
            r->transform.rotation.y = cur + diff * std::min(1.0f, dt * 14.0f);
        }
    }

    // Gravity + jumping.
    if (m_grounded && jump) { m_velocity.y = m_humanoid.jumpPower; m_grounded = false; }
    m_velocity.y -= m_scene->world().gravity * dt;

    // Walking plus any leftover push (from jump pads, etc.), which fades out.
    glm::vec3 delta = horiz * m_humanoid.walkSpeed * dt;
    delta.x += m_velocity.x * dt;
    delta.z += m_velocity.z * dt;
    delta.y = m_velocity.y * dt;
    float drag = std::max(0.0f, 1.0f - (m_grounded ? 8.0f : 0.8f) * dt);
    m_velocity.x *= drag;
    m_velocity.z *= drag;

    Physics::MoveResult res = physics.moveCharacter(pos, delta, m_grounded);
    m_grounded = res.grounded;
    if (res.grounded && m_velocity.y < 0.0f) m_velocity.y = 0.0f;
    if (res.hitCeiling && m_velocity.y > 0.0f) m_velocity.y = 0.0f;
    m_groundId = res.groundId;
    if (m_groundId)
        if (SceneNode* g = m_scene->findById(m_groundId))
            m_groundPrev = glm::vec3(g->worldMatrix()[3]);

    r->transform.position = res.position;

    // Fell off the world.
    if (res.position.y < m_scene->world().fallenPartsHeight) m_humanoid.health = 0.0f;

    animate(dt, moving, m_grounded);
}

void Player::animate(float dt, bool moving, bool grounded) {
    m_swing    = approach(m_swing, (moving && grounded) ? 45.0f : 0.0f, 10.0f, dt);
    m_airBlend = approach(m_airBlend, grounded ? 0.0f : 1.0f, 10.0f, dt);
    if (moving) m_walkPhase += dt * (2.0f + m_humanoid.walkSpeed * 0.9f);

    float s = std::sin(m_walkPhase) * m_swing;
    // Positive angle swings the bottom of a limb backwards.
    float angles[4] = {
        s  * (1 - m_airBlend) + (-165.0f) * m_airBlend,   // Left Arm  (arms up when jumping)
        -s * (1 - m_airBlend) + (-165.0f) * m_airBlend,   // Right Arm
        -s * (1 - m_airBlend) + (  12.0f) * m_airBlend,   // Left Leg
        s  * (1 - m_airBlend) + ( -12.0f) * m_airBlend,   // Right Leg
    };

    for (int i = 0; i < 4; ++i) {
        SceneNode* limb = part(kLimbs[i]);
        if (!limb) continue;
        auto it = m_rest.find(limb->id);
        if (it == m_rest.end()) continue;
        const Transform& rest = it->second;

        // Rotate around the shoulder / hip (the top of the limb).
        float a = glm::radians(angles[i]);
        float h = rest.scale.y * 0.5f;
        glm::vec3 pivot = rest.position + glm::vec3(0.0f, h, 0.0f);
        limb->transform.position = pivot + glm::vec3(0.0f, -h * std::cos(a), -h * std::sin(a));
        limb->transform.rotation = rest.rotation + glm::vec3(angles[i], 0.0f, 0.0f);
    }
}

void Player::startDeath() {
    SceneNode* r = root();
    if (!r) return;
    m_dead = true;
    m_diedFlag = true;
    m_deadTime = 0.0f;
    m_debris.clear();

    // Move every body part into world space so they can tumble independently.
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
        glm::vec3 vel = out * (1.5f + rand01() * 2.0f) +
                        glm::vec3(rand01() - 0.5f, 3.0f + rand01() * 2.5f, rand01() - 0.5f);
        glm::vec3 spin = (glm::vec3(rand01(), rand01(), rand01()) - 0.5f) * 500.0f;
        m_debris.push_back({c->id, vel, spin});
    }
    r->transform.position = glm::vec3(0.0f);
    r->transform.rotation = glm::vec3(0.0f);
    r->transform.scale    = glm::vec3(1.0f);
}

void Player::updateDeath(float dt, Physics& physics) {
    m_deadTime += dt;
    const float g = m_scene->world().gravity;
    for (auto& d : m_debris) {
        SceneNode* n = m_scene->findById(d.id);
        if (!n) continue;
        d.vel.y -= g * dt;
        n->transform.position += d.vel * dt;
        n->transform.rotation += d.spin * dt;

        float push = physics.pushUp(Physics::worldBounds(n));
        if (push > 0.0f) {
            n->transform.position.y += push;
            if (d.vel.y < 0.0f) d.vel.y = -d.vel.y * 0.25f;   // small bounce
            d.vel.x *= 0.8f;  d.vel.z *= 0.8f;
            d.spin *= 0.8f;
        }
    }
    if (m_deadTime >= m_respawnDelay) respawn();
}
