#include "Player.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Physics.h"
#include "PlayerModel.h"
#include "../renderer/MeshLibrary.h"
#include "../core/Audio.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <algorithm>
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
    SceneNode* r = buildRig(*m_scene, "Player", m_spawn);
    m_rootId = r->id;
    setHat(m_hat, m_hatTint);
    m_scene->markDirty();
}

SceneNode* Player::buildRig(Scene& scene, const std::string& name, const glm::vec3& feet) {
    // Container node for the whole character (origin at the feet).
    auto model = std::make_unique<SceneNode>(name, NodeKind::Model);
    model->transform.position = feet;
    SceneNode* r = scene.insert(std::move(model));

    auto addPart = [&](SceneNode* parent, const std::string& partName, PrimitiveType shape,
                       glm::vec3 pos, glm::vec3 scale, glm::vec3 col,
                       bool internal = false, bool visible = true) -> SceneNode* {
        auto node = std::make_unique<SceneNode>(partName);
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
    // Then give them the default character model's shapes (assets/models/player.obj).
    for (auto& c : r->children) usePlayerModel(*c);

    addFace(head);
    scene.markDirty();
    return r;
}

// The classic smiley on the front (+Z) of the head, in head-local space:
// two small oval eyes and a smooth U-shaped smile.
void Player::addFace(SceneNode* head) {
    auto add = [&](const char* name, PrimitiveType shape, glm::vec3 pos, glm::vec3 scale, glm::vec3 rotDeg) {
        auto n = std::make_unique<SceneNode>(name);
        n->primitiveType      = shape;
        n->mesh               = MeshLibrary::get(shape);
        n->transform.position = pos;
        n->transform.scale    = scale;
        n->transform.rotation = rotDeg;
        n->color    = kBlack;
        n->internal = true;
        head->addChild(std::move(n));
    };
    // Sit on the round head: z follows the cylinder, and each piece turns to face out.
    auto surfaceZ = [](float x) { return std::sqrt(std::max(0.0f, 0.25f - x * x)) - 0.005f; };
    auto yawAt = [](float x) { return glm::degrees(std::asin(std::clamp(x / 0.5f, -1.0f, 1.0f))); };
    for (float x : {-0.1f, 0.1f})
        add(x < 0 ? "Eye.L" : "Eye.R", PrimitiveType::Sphere, {x, 0.16f, surfaceZ(x)}, {0.065f, 0.13f, 0.05f}, {0, yawAt(x), 0});
    // The smile: short bars along the curve, each turned along it (ends high, round at the bottom).
    auto curve = [](float x) { float t = std::abs(x) / 0.2f; return -0.27f + 0.22f * std::pow(t, 1.7f); };
    const int n = 10;
    for (int i = 0; i < n; ++i) {
        float x0 = -0.2f + 0.4f * i / n, x1 = -0.2f + 0.4f * (i + 1) / n;
        float y0 = curve(x0), y1 = curve(x1);
        float xm = (x0 + x1) * 0.5f, ym = (y0 + y1) * 0.5f;
        float len = std::hypot(x1 - x0, y1 - y0) + 0.035f;   // overlap a little so there are no gaps
        float roll = glm::degrees(std::atan2(y1 - y0, x1 - x0));
        add("Smile", PrimitiveType::Cube, {xm, ym, surfaceZ(xm)}, {len, 0.055f, 0.05f}, {0, yawAt(xm), roll});
    }
}

void Player::upgradeFace() {
    // Characters saved with the old block face (5 smile blocks): swap in the new one.
    SceneNode* r = root();
    SceneNode* head = nullptr;
    if (r) for (auto& c : r->children) if (c->name == "Head") head = c.get();
    if (!head) return;
    int smiles = 0;
    std::vector<SceneNode*> old;
    for (auto& c : head->children)
        if (c->name == "Smile" || c->name == "Eye.L" || c->name == "Eye.R") {
            old.push_back(c.get());
            if (c->name == "Smile") ++smiles;
        }
    if (smiles != 5) return;   // already the new face (or a custom one)
    for (SceneNode* o : old) m_scene->removeNode(o);
    addFace(head);
    m_scene->markDirty();
}

bool Player::usePlayerModel(SceneNode& part) {
    glm::vec3 pos, size;
    if (part.primitiveType == PrimitiveType::Mesh && PlayerModel::nameOf(part.editMesh.get())) return false;   // already
    if (!PlayerModel::placement(part.name, pos, size) || !PlayerModel::apply(part)) return false;
    part.transform.position = pos;
    part.transform.scale = size;
    return true;
}

void Player::upgradeRig(SceneNode* rig) {
    // Characters saved before the new model: swap the blocky parts for the model's.
    // (The face still fits: both heads are round with the same radius.)
    if (!rig) return;
    for (auto& c : rig->children)
        if (c->primitiveType == PrimitiveType::Cube || c->primitiveType == PrimitiveType::Cylinder) usePlayerModel(*c);
}

glm::vec3 Player::position() const {
    SceneNode* r = root();
    return r ? r->transform.position : glm::vec3(0.0f);
}

glm::vec3 Player::focusPoint() const {
    if (m_dead && m_ragdoll.active()) return m_ragdoll.center();
    if (m_dead)
        if (SceneNode* t = part("Torso")) return glm::vec3(t->worldMatrix()[3]);
    // Like Roblox, the camera looks at (and in first person, sits in) the head.
    // (Where the head is on the body, not how it bobs, so the view stays steady.)
    if (SceneNode* r = root())
        if (SceneNode* h = part("Head")) {
            auto it = m_rest.find(h->id);
            glm::vec3 local = it != m_rest.end() ? it->second.position : h->transform.position;
            return glm::vec3(r->worldMatrix() * glm::vec4(local, 1.0f));
        }
    return position() + glm::vec3(0.0f, 2.3f, 0.0f);
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
    if (SceneNode* r = root()) applyColors(r, c);
}

void Player::applyColors(SceneNode* r, const BodyColors& c) {
    auto set = [&](const char* n, glm::vec3 v) { if (SceneNode* p = r->findChild(n)) p->color = v; };
    set("Head", c.head);      set("Torso", c.torso);
    set("Left Arm", c.leftArm); set("Right Arm", c.rightArm);
    set("Left Leg", c.leftLeg); set("Right Leg", c.rightLeg);
    set("HumanoidRootPart", c.torso);
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
        case HatStyle::Ponytail: return "Ponytail";
        default:               return "None";
    }
}

void Player::setHat(HatStyle style, glm::vec3 tint) {
    m_hat = style;
    m_hatTint = tint;
    if (SceneNode* r = root()) applyHat(*m_scene, r, style, tint);
}

void Player::applyHat(Scene& scene, SceneNode* r, HatStyle style, glm::vec3 tint) {
    const bool tinted = tint.x >= 0.0f;
    auto main = [&](glm::vec3 normal) { return tinted ? tint : normal; };
    // Remove the old hat pieces.
    std::vector<SceneNode*> old;
    for (auto& c : r->children)
        if (startsWith(c->name, "Hat")) old.push_back(c.get());
    for (auto* o : old) scene.removeNode(o);

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
    // Top of the head, and how much narrower it is than the hats were made for (0.72 wide).
    glm::vec3 headPos{0.0f, 2.325f, 0.0f}, headSize{0.72f, 0.65f, 0.72f};
    PlayerModel::placement("Head", headPos, headSize);
    const float top = headPos.y + headSize.y * 0.5f;
    const float w = headSize.x / 0.72f;
    auto addW = [&](const char* name, PrimitiveType shape, glm::vec3 pos, glm::vec3 scale, glm::vec3 col,
                    Material mat = Material::Plastic) {
        add(name, shape, {pos.x * w, pos.y, pos.z * w}, {scale.x * w, scale.y, scale.z * w}, col, mat);
    };
    switch (style) {
        case HatStyle::TopHat:
            addW("Hat Brim", PrimitiveType::Cylinder, {0, top + 0.025f, 0}, {1.0f, 0.05f, 1.0f}, main(kBlack));
            addW("Hat",      PrimitiveType::Cylinder, {0, top + 0.325f, 0}, {0.62f, 0.55f, 0.62f}, main(kBlack));
            addW("Hat Band", PrimitiveType::Cylinder, {0, top + 0.11f, 0}, {0.64f, 0.1f, 0.64f}, {0.75f, 0.12f, 0.12f});
            break;
        case HatStyle::Cap:
            addW("Hat",       PrimitiveType::Sphere, {0, top - 0.05f, 0}, {0.78f, 0.5f, 0.78f}, main({0.85f, 0.15f, 0.15f}));
            addW("Hat Visor", PrimitiveType::Cube,   {0, top - 0.03f, 0.46f}, {0.52f, 0.04f, 0.36f}, main({0.85f, 0.15f, 0.15f}));
            break;
        case HatStyle::Crown:
            addW("Hat",       PrimitiveType::Cylinder, {0, top + 0.14f, 0}, {0.74f, 0.28f, 0.74f}, main({1.0f, 0.78f, 0.2f}), Material::Metal);
            addW("Hat Jewel", PrimitiveType::Cube, {0, top + 0.14f, 0.37f}, {0.12f, 0.12f, 0.05f}, {0.9f, 0.1f, 0.2f}, Material::Neon);
            break;
        case HatStyle::Ponytail: {   // hair: a cap over the top and back of the head, a bun and a tail
            const glm::vec3 hair = main({0.86f, 0.2f, 0.62f});
            addW("Hat",       PrimitiveType::Sphere, {0, top - 0.1f, -0.05f}, {0.8f, 0.46f, 0.82f}, hair);
            addW("Hat Back",  PrimitiveType::Cube,   {0, top - 0.3f, -0.3f}, {0.74f, 0.42f, 0.16f}, hair);
            addW("Hat Bun",   PrimitiveType::Sphere, {0, top + 0.12f, -0.3f}, {0.36f, 0.34f, 0.36f}, hair);
            addW("Hat Tail",  PrimitiveType::Sphere, {0, top - 0.14f, -0.5f}, {0.26f, 0.56f, 0.26f}, hair);
            break;
        }
        default: break;
    }
    scene.markDirty();
}

CharacterPose Player::capturePose(const SceneNode* r) {
    CharacterPose pose;
    pose.root = r->transform;
    pose.forceField = r->hasForceField();
    for (auto& c : r->children)
        if (c->isPart()) pose.parts.push_back({c->name, c->transform});
    return pose;
}

void Player::applyPose(SceneNode* r, const CharacterPose& pose) {
    r->transform = pose.root;
    // Show / hide the ForceField to match.
    if (pose.forceField && !r->hasForceField()) {
        r->addChild(std::make_unique<SceneNode>("ForceField", NodeKind::ForceField));
    } else if (!pose.forceField && r->hasForceField()) {
        for (auto& c : r->children)
            if (c->kind == NodeKind::ForceField) { r->removeChild(c.get()); break; }
    }
    for (const auto& [name, t] : pose.parts)
        if (SceneNode* c = r->findChild(name)) c->transform = t;
}

// ---------------------------------------------------------------------------
// Play mode
// ---------------------------------------------------------------------------

namespace {
// A random spot on top of the first visible "SpawnLocation" part (random so
// players in multiplayer don't all appear inside each other).
bool findSpawnLocation(SceneNode* node, glm::vec3& out) {
    if (node->name == "SpawnLocation" && node->isPart() && node->visible) {
        AABB b = Physics::worldBounds(node);
        glm::vec3 c = (b.min + b.max) * 0.5f;
        glm::vec3 half = glm::max((b.max - b.min) * 0.5f - glm::vec3(0.6f), glm::vec3(0.0f));
        out = {c.x + (rand01() * 2 - 1) * half.x, b.max.y + 0.001f, c.z + (rand01() * 2 - 1) * half.z};
        return true;
    }
    for (auto& c : node->children)
        if (findSpawnLocation(c.get(), out)) return true;
    return false;
}
} // namespace

void Player::beginPlay() {
    m_checkpoint = 0;
    m_rest.clear();
    m_debris.clear();
    SceneNode* r = root();
    if (r) {
        m_rootRest = r->transform;
        for (auto& c : r->children) m_rest[c->id] = c->transform;
    }
    m_dead = m_diedFlag = false;
    respawn(true);
}

void Player::endPlay() {
    footsteps(false, glm::vec3(0.0f));
    m_checkpoint = 0;
    m_dead = false;
    m_debris.clear();
    m_rest.clear();
}

void Player::respawn(bool firstSpawn) {
    SceneNode* r = root();
    if (!r) return;
    for (auto& c : r->children) {
        auto it = m_rest.find(c->id);
        if (it != m_rest.end()) c->transform = it->second;
    }
    glm::vec3 spawnAt = m_spawn;
    findSpawnLocation(m_scene->root(), spawnAt);
    if (SceneNode* cp = m_checkpoint ? m_scene->findById(m_checkpoint) : nullptr) {   // a checkpoint you reached
        AABB b = Physics::worldBounds(cp);
        spawnAt = glm::vec3((b.min.x + b.max.x) * 0.5f, b.max.y + 0.05f, (b.min.z + b.max.z) * 0.5f);
    }
    r->transform = m_rootRest;
    r->transform.position = spawnAt;

    m_humanoid.health = m_humanoid.maxHealth;
    m_respawnedFlag = true;
    {   // a splat when you arrive in the game, a bass hit when you come back to life
        glm::vec3 at = r->transform.position + glm::vec3(0, 2, 0);
        Audio::play(firstSpawn ? "spawn" : "respawn", 0.7f, 1.0f, false, &at);
    }
    m_velocity  = glm::vec3(0.0f);
    m_grounded  = false;
    m_groundId  = 0;
    m_walkPhase = m_swing = m_airBlend = 0.0f;
    m_dead      = false;
    m_debris.clear();
    m_ragdoll.stop();
    m_pendingForce = 0.0f;
    m_pendingImpulse = glm::vec3(0.0f);
    m_lastHealth = m_humanoid.health;

    // A few seconds of ForceField after spawning.
    if (SceneNode* old = m_scene->findById(m_spawnFF)) m_scene->removeNode(old);
    m_spawnFF = 0;
    m_spawnFFTime = m_scene->world().spawnForceField;
    if (m_spawnFFTime > 0.0f) {
        auto ff = std::make_unique<SceneNode>("ForceField", NodeKind::ForceField);
        m_spawnFF = m_scene->insert(std::move(ff), r)->id;
    }
}

bool Player::hasForceField() const {
    SceneNode* r = root();
    return r && r->hasForceField();
}

void Player::kill(float force, const glm::vec3& impulse) {
    if (m_dead) return;
    m_pendingForce   = std::max(m_pendingForce, glm::clamp(force, 0.0f, 1.0f));
    m_pendingImpulse += impulse;
    m_humanoid.health = 0.0f;
}

void Player::hurt(float damage, float force, const glm::vec3& impulse) {
    if (m_dead || damage <= 0.0f) return;
    if (hasForceField()) { launch(m_velocity + impulse * 0.5f); return; }   // shielded: just a shove
    m_humanoid.health = std::max(0.0f, m_humanoid.health - damage);
    if (m_humanoid.health <= 0.0f) kill(force, impulse);
    else launch(m_velocity + impulse);
}

// Little squirts of blood / oil when the character gets hurt.
void Player::bleed(float damage) {
    if (!m_scene->goreEnabled()) return;
    SceneNode* t = part("Torso");
    if (!t) return;
    glm::vec3 at = glm::vec3(t->worldMatrix()[3]);
    int n = std::clamp((int)(damage * 0.5f), 3, 30);
    m_scene->particles().spray(m_scene->goreKind(), at, glm::vec3(0, 0.5f, 0), n, 2.5f);
    if (m_scene->goreKind() == GoreKind::Oil) m_scene->particles().sparks(at, n / 2);
}

bool Player::consumeRespawned() {
    bool r = m_respawnedFlag;
    m_respawnedFlag = false;
    return r;
}

// ---------------------------------------------------------------------------
// Tools
// ---------------------------------------------------------------------------

uint64_t Player::backpackId() {
    SceneNode* r = root();
    if (!r) return 0;
    if (SceneNode* b = r->findChild("Backpack")) return b->id;
    auto b = std::make_unique<SceneNode>("Backpack", NodeKind::Model);
    b->visible = false;      // tools in here aren't drawn and don't collide
    b->internal = true;      // (and don't show in the Explorer)
    SceneNode* raw = r->addChild(std::move(b));
    m_scene->markDirty();
    return raw->id;
}

SceneNode* Player::equippedTool() const {
    SceneNode* r = root();
    if (!r) return nullptr;
    for (auto& c : r->children) if (c->isTool()) return c.get();
    return nullptr;
}

void Player::syncSlots() {
    // Keep the hotbar order stable: tools keep their slot while they're held,
    // new ones go at the end, gone ones leave a gap that closes up.
    std::vector<uint64_t> have;
    if (SceneNode* t = equippedTool()) have.push_back(t->id);
    if (SceneNode* b = m_scene->findById(backpackId()))
        for (auto& c : b->children) if (c->isTool()) have.push_back(c->id);
    m_slots.erase(std::remove_if(m_slots.begin(), m_slots.end(), [&](uint64_t id) {
        return std::find(have.begin(), have.end(), id) == have.end();
    }), m_slots.end());
    for (uint64_t id : have)
        if (std::find(m_slots.begin(), m_slots.end(), id) == m_slots.end()) m_slots.push_back(id);
}

std::vector<SceneNode*> Player::tools() {
    syncSlots();
    std::vector<SceneNode*> out;
    for (uint64_t id : m_slots) if (SceneNode* t = m_scene->findById(id)) out.push_back(t);
    return out;
}

bool Player::give(SceneNode* tool) {
    if (!tool || !tool->isTool() || !root()) return false;
    if ((int)tools().size() >= kMaxTools) return false;
    SceneNode* bag = m_scene->findById(backpackId());
    if (!bag || tool->parent == bag) return false;
    auto owned = m_scene->detach(tool);
    if (!owned) return false;
    owned->transform = Transform{};
    bag->addChild(std::move(owned));
    m_scene->markDirty();
    syncSlots();
    return true;
}

void Player::equip(uint64_t toolId) {
    SceneNode* r = root();
    if (!r || m_dead) return;
    SceneNode* held = equippedTool();
    if (held && held->id == toolId) return;
    SceneNode* bag = m_scene->findById(backpackId());
    if (held) {   // put the one in your hand away first
        uint64_t id = held->id;
        auto owned = r->detachChild(held);
        owned->transform = Transform{};
        bag->addChild(std::move(owned));
        if (onToolEquip) onToolEquip(id, false);
    }
    if (toolId) {
        SceneNode* t = m_scene->findById(toolId);
        if (t && t->isTool() && t->parent == bag) {
            r->addChild(bag->detachChild(t));
            updateGrip();
            if (onToolEquip) onToolEquip(toolId, true);
        }
    }
    m_scene->markDirty();
}

void Player::toggleSlot(int slot) {
    auto list = tools();
    if (slot < 0 || slot >= (int)list.size()) return;
    SceneNode* held = equippedTool();
    equip(held == list[(size_t)slot] ? 0 : list[(size_t)slot]->id);
}

SceneNode* Player::drop() {
    SceneNode* r = root();
    SceneNode* held = equippedTool();
    if (!r || !held || !held->canBeDropped) return nullptr;
    uint64_t id = held->id;
    glm::mat4 world = held->worldMatrix();   // keep it where it is, then lay it down in front
    auto owned = r->detachChild(held);
    if (onToolEquip) onToolEquip(id, false);
    float yaw = glm::radians(r->transform.rotation.y);
    glm::vec3 front = r->transform.position + glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw)) * 2.5f;   // (facing = atan2(x, z))
    owned->transform = Transform{};
    owned->transform.position = glm::vec3(front.x, r->transform.position.y + 0.6f, front.z);
    owned->transform.rotation = glm::vec3(90.0f, r->transform.rotation.y, 0.0f);   // lying on its side
    (void)world;
    SceneNode* raw = m_scene->insert(std::move(owned));
    syncSlots();
    return raw;
}

void Player::clearTools() {
    std::vector<SceneNode*> gone = tools();
    for (SceneNode* t : gone) m_scene->removeNode(t);
    m_slots.clear();
}

void Player::updateGrip() {
    SceneNode* r = root();
    SceneNode* tool = equippedTool();
    SceneNode* arm = part("Right Arm");
    if (!r || !tool || !arm) return;
    SceneNode* handle = tool->findChild("Handle");
    if (!handle) { tool->transform = Transform{}; return; }
    // The hand: the bottom of the right arm. The Handle's long side (+Y) points
    // out of the fist, which is forward when the arm is raised.
    glm::mat4 a = arm->worldMatrix();
    glm::vec3 x = glm::normalize(glm::vec3(a[0])), y = glm::normalize(glm::vec3(a[1])), z = glm::normalize(glm::vec3(a[2]));
    float len = glm::length(glm::vec3(a[1]));
    glm::vec3 hand = glm::vec3(a[3]) - y * (len * 0.5f);
    glm::mat4 grip(1.0f);
    grip[0] = glm::vec4(x, 0.0f);
    grip[1] = glm::vec4(-y, 0.0f);
    grip[2] = glm::vec4(-z, 0.0f);
    grip[3] = glm::vec4(hand, 1.0f);
    grip = grip * glm::translate(glm::mat4(1.0f), -tool->gripPos);
    // Where the Handle sits inside the tool (without its size).
    Transform h = handle->transform;
    h.scale = glm::vec3(1.0f);
    glm::mat4 local = glm::inverse(r->worldMatrix()) * grip * glm::inverse(h.matrix());
    float rz, ry, rx;
    glm::extractEulerAngleZYX(local, rz, ry, rx);
    tool->transform.position = glm::vec3(local[3]);
    tool->transform.rotation = glm::degrees(glm::vec3(rx, ry, rz));
    tool->transform.scale = glm::vec3(1.0f);
}

bool Player::consumeDied() {
    bool d = m_diedFlag;
    m_diedFlag = false;
    return d;
}

namespace {
bool flagged(const SceneNode* n, const char* what) {
    if (std::find(n->tags.begin(), n->tags.end(), what) != n->tags.end()) return true;
    const Attribute* a = n->findAttribute(what);
    return a && ((a->type == Attribute::Bool && a->b) || (a->type == Attribute::Number && a->n != 0));
}
bool nameHas(const SceneNode* n, const char* word) { return n->name.find(word) != std::string::npos; }
bool insideBox(const AABB& b, const glm::vec3& p, float pad = 0.0f) {
    return p.x >= b.min.x - pad && p.x <= b.max.x + pad && p.y >= b.min.y - pad && p.y <= b.max.y + pad &&
           p.z >= b.min.z - pad && p.z <= b.max.z + pad;
}
} // namespace

bool Player::isClimbable(const SceneNode* n) {
    return n->isPart() && n->canCollide && (nameHas(n, "Truss") || nameHas(n, "Ladder") || flagged(n, "Climbable"));
}
bool Player::isWater(const SceneNode* n) {
    return n->isPart() && !n->canCollide && (n->name == "Water" || nameHas(n, "Water") || flagged(n, "Water"));
}

void Player::update(float dt, const glm::vec3& moveDir, bool jump, Physics& physics) {
    SceneNode* r = root();
    if (!r) return;

    if (m_dead) { updateDeath(dt, physics); return; }

    if (m_spawnFF && (m_spawnFFTime -= dt) <= 0.0f) {
        if (SceneNode* ff = m_scene->findById(m_spawnFF)) m_scene->removeNode(ff);
        m_spawnFF = 0;
    }

    // Took damage since last frame (scripts, traps, explosions)?
    if (m_humanoid.health < m_lastHealth - 0.5f) {
        bleed(m_lastHealth - m_humanoid.health);
        if (m_humanoid.health > 0.0f) {
            glm::vec3 at = r->transform.position + glm::vec3(0, 1.5f, 0);
            Audio::play("hit", 0.6f, 1.0f, false, &at);
        }
    }
    m_lastHealth = m_humanoid.health;
    if (m_humanoid.health <= 0.0f) { startDeath(); return; }

    glm::vec3 pos = r->transform.position;

    // Ride moving platforms: whatever we stood on last frame carries us, sliding
    // and spinning (a turntable turns you with it), and we keep its speed.
    m_platformVel = glm::vec3(0.0f);
    if (m_groundId) {
        if (SceneNode* g = m_scene->findById(m_groundId)) {
            glm::mat4 change = g->worldMatrix() * glm::inverse(m_groundPrevM);
            glm::vec3 carried = glm::vec3(change * glm::vec4(pos, 1.0f));
            if (dt > 0.0f) m_platformVel = (carried - pos) / dt;
            pos = carried;
            r->transform.rotation.y += glm::degrees(std::atan2(change[2][0], change[2][2]));
        }
    }

    // Horizontal movement.
    glm::vec3 horiz = {moveDir.x, 0.0f, moveDir.z};
    float len = glm::length(horiz);
    bool moving = len > 1e-4f;
    if (moving) {
        horiz /= len;
        float amount = std::min(1.0f, len);   // a half-pushed thumbstick walks slower
        if (m_humanoid.autoRotate && !m_faceLock) {
            // Turn smoothly towards the direction of travel (shortest way round).
            float target = glm::degrees(std::atan2(horiz.x, horiz.z));
            float cur    = r->transform.rotation.y;
            float diff   = std::fmod(target - cur + 540.0f, 360.0f) - 180.0f;
            r->transform.rotation.y = cur + diff * std::min(1.0f, dt * 14.0f);
        }
        horiz *= amount;
    }

    // First person: the body faces wherever the camera looks.
    if (m_faceLock) { r->transform.rotation.y = m_faceYaw; m_faceLock = false; }

    // Trusses / ladders in front of us, and water around us.
    const float yaw = glm::radians(r->transform.rotation.y);
    const glm::vec3 facing(std::sin(yaw), 0.0f, std::cos(yaw));
    const glm::vec3 ahead = moving ? glm::normalize(glm::vec3(horiz.x, 0.0f, horiz.z)) : facing;
    bool truss = false, water = false;
    float waterTop = -1e9f;
    glm::vec3 current(0.0f);   // water flowing along (a "Flow" attribute)
    WaterSystem& waves = m_scene->water();
    m_climbCooldown = std::max(0.0f, m_climbCooldown - dt);
    m_scene->forEach([&](SceneNode* n) {
        if (!n->isPart() || m_scene->isCharacterPart(n)) return;
        const bool climbable = m_climbCooldown <= 0.0f && isClimbable(n), wet = isWater(n);
        if (!climbable && !wet) return;
        AABB b = Physics::worldBounds(n);
        if (climbable && !truss)
            for (float h : {0.4f, 1.3f, 2.2f})
                if (insideBox(b, pos + glm::vec3(0.0f, h, 0.0f) + ahead * 0.75f, 0.05f)) { truss = true; break; }
        if (!wet) return;
        // While playing, the surface moves with the waves.
        const glm::vec3 chest = pos + glm::vec3(0.0f, 1.2f, 0.0f);
        const float top = waves.active() ? waves.surfaceOf(n, chest.x, chest.z) : b.max.y;
        AABB wetBox = b;
        wetBox.max.y = top;
        if (insideBox(wetBox, chest)) {
            water = true;
            waterTop = std::max(waterTop, top);
            if (const WaterSystem::Body* wb = waves.find(n->id)) current = wb->flow;
        }
    });
    // Flowing water from a WaterSource (floods, rivers).
    if (!water && waves.active()) {
        float top;
        glm::vec3 flow;
        if (waves.at(pos + glm::vec3(0.0f, 1.2f, 0.0f), &top, &flow)) { water = true; waterTop = top; current = flow; }
    }
    // Climb when walking into a truss (or when already on one and still touching it).
    const bool wasClimbing = m_climbing;
    m_climbing = truss && (moving || (m_climbing && !m_grounded));
    m_swimming = water && !m_climbing;
    float speed = m_humanoid.walkSpeed * (m_swimming ? 0.75f : 1.0f);

    // Gravity + jumping.
    if (m_climbing) {
        if (jump && wasClimbing && !m_grounded) {   // leap off backwards
            m_climbing = false;
            m_climbCooldown = 0.35f;
            m_velocity = -ahead * 10.0f + glm::vec3(0.0f, m_humanoid.jumpPower * 0.7f, 0.0f);
        } else {
            m_velocity.y = moving ? m_humanoid.walkSpeed * 0.9f : 0.0f;   // stop pushing = hang on
            m_climbPhase += dt * (moving ? 9.0f : 0.0f);
            horiz *= 0.3f;   // (the truss is in the way anyway)
        }
    } else if (m_swimming) {
        const float g = m_scene->world().gravity;
        m_velocity.y -= g * 0.12f * dt;                       // almost floating
        if (pos.y + 1.6f < waterTop) m_velocity.y += 5.0f * dt;   // deep: drift up
        if (jump) m_velocity.y = std::min(m_velocity.y + 45.0f * dt, 9.0f);   // swim up (and jump out at the top)
        m_velocity.y *= std::max(0.0f, 1.0f - 2.5f * dt);     // water slows everything
        m_climbPhase += dt * 6.0f;
    } else {
        if (m_grounded && jump) {
            // Jumping off something moving keeps its speed (off a train, you fly forward).
            m_velocity.x += m_platformVel.x;
            m_velocity.z += m_platformVel.z;
            m_velocity.y = m_humanoid.jumpPower + std::max(0.0f, m_platformVel.y);
            m_grounded = false;
            glm::vec3 at = pos + glm::vec3(0, 1, 0);
            Audio::play("jump", 0.35f, 1.0f, false, &at);
        }
        m_velocity.y -= m_scene->world().gravity * dt;
    }

    // Walking plus any leftover push (from jump pads, etc.), which fades out.
    glm::vec3 delta = horiz * speed * dt + current * (m_swimming ? dt : 0.0f);
    delta.x += m_velocity.x * dt;
    delta.z += m_velocity.z * dt;
    delta.y = m_velocity.y * dt;
    float drag = std::max(0.0f, 1.0f - (m_grounded ? 8.0f : 0.8f) * dt);
    m_velocity.x *= drag;
    m_velocity.z *= drag;

    Physics::MoveResult res = physics.moveCharacter(pos, delta, m_grounded, r->transform.rotation.y);
    // Walking into loose parts pushes them (heavier = harder, handled by the solver).
    for (auto& [node, dir] : res.pushed) {
        glm::vec3 want = dir * m_humanoid.walkSpeed * 0.9f;
        if (glm::dot(node->velocity, dir) < glm::dot(want, dir)) node->velocity += dir * (m_humanoid.walkSpeed * 0.15f);
        Physics::wake(node);
    }

    // Fall damage: landing hard hurts, landing very hard is fatal (and messy).
    const WorldSettings& world = m_scene->world();
    float impact = -m_velocity.y;
    if (res.grounded && !m_grounded && world.fallDamage && impact > world.fallDamageSpeed &&
        !hasForceField() && !m_swimming) {
        float over = impact - world.fallDamageSpeed;
        float damage = over * 7.0f * world.fallDamageScale;
        m_humanoid.health = std::max(0.0f, m_humanoid.health - damage);
        if (m_humanoid.health <= 0.0f)
            kill(std::clamp(over / 15.0f, 0.0f, 1.0f), glm::vec3(m_velocity.x, 2.0f, m_velocity.z));
    }
    m_grounded = res.grounded;
    if (res.grounded && m_velocity.y < 0.0f) m_velocity.y = 0.0f;
    if (res.hitCeiling && m_velocity.y > 0.0f) m_velocity.y = 0.0f;
    m_groundId = res.groundId;
    if (m_groundId)
        if (SceneNode* g = m_scene->findById(m_groundId))
            m_groundPrevM = g->worldMatrix();

    // Loose parts that crash into us knock us back (a rolling boulder, a swinging
    // wrecking ball), harder the heavier and faster they are.
    for (const Physics::Shove& s : physics.shoves(res.position, r->transform.rotation.y, kBodyMass)) {
        glm::vec3 dir(s.dir.x, 0.0f, s.dir.z);
        float l = glm::length(dir);
        if (l < 1e-3f) continue;
        dir /= l;
        float have = glm::dot(glm::vec3(m_velocity.x, 0.0f, m_velocity.z), dir);
        if (s.speed > have) m_velocity += dir * (s.speed - have);
        if (s.speed > 6.0f && !m_climbing) {   // a big hit throws you off your feet
            m_velocity.y = std::max(m_velocity.y, s.speed * 0.35f);
            m_grounded = false;
            m_groundId = 0;
        }
    }

    r->transform.position = res.position;

    // Fell off the world.
    if (res.position.y < m_scene->world().fallenPartsHeight) m_humanoid.health = 0.0f;

    // How fast we really moved (not how hard the keys are pushed): walking into a
    // wall doesn't run on the spot, and a slower WalkSpeed takes slower steps.
    const float moved = dt > 0.0f ? glm::length(glm::vec2(res.position.x - pos.x, res.position.z - pos.z)) / dt : 0.0f;
    m_groundSpeed = approach(m_groundSpeed, moving ? std::min(moved, m_humanoid.walkSpeed * 2.0f + 2.0f) : 0.0f, 12.0f, dt);
    animate(dt, m_groundSpeed, m_grounded);
    footsteps(m_groundSpeed > 0.5f && m_grounded && !m_swimming && !m_climbing, res.position);
    updateGrip();
}

void Player::footsteps(bool running, const glm::vec3& at) {
    if (!running) {
        if (m_stepSound) Audio::stop(m_stepSound);
        m_stepSound = 0;
        return;
    }
    glm::vec3 feet = at + glm::vec3(0.0f, 0.5f, 0.0f);
    float pitch = std::clamp(m_humanoid.walkSpeed / 6.0f, 0.6f, 1.6f);   // faster walkers step faster (6 = normal)
    if (m_stepSound && Audio::isPlaying(m_stepSound)) {
        Audio::setPosition(m_stepSound, feet);
        Audio::setPitch(m_stepSound, pitch);
        return;
    }
    m_stepSound = Audio::play("footsteps", 0.35f, pitch, true, &feet);
}

bool Player::isLimb(const SceneNode* p) {
    for (const char* n : kLimbs) if (p->name == n) return true;
    return false;
}

bool Player::drivesPart(const SceneNode* p) const {
    for (const char* n : kLimbs) if (p->name == n) return true;
    return false;
}

void Player::animate(float dt, float groundSpeed, bool grounded) {
    m_swing    = approach(m_swing, grounded ? strideSwing(groundSpeed) : 0.0f, 10.0f, dt);
    m_airBlend = approach(m_airBlend, grounded ? 0.0f : 1.0f, 10.0f, dt);
    m_walkPhase += dt * strideRate(groundSpeed);

    m_holdBlend = approach(m_holdBlend, equippedTool() ? 1.0f : 0.0f, 12.0f, dt);
    float s = std::sin(m_walkPhase) * m_swing;
    // Positive angle swings the bottom of a limb backwards.
    float rightArm = -s * (1 - m_airBlend) + (-165.0f) * m_airBlend;
    // Using a tool: raise the arm, then slash down through the front, then back.
    float hold = -90.0f;
    if (m_toolSwing > 0.0f) {
        float t = 1.0f - m_toolSwing / kToolSwingTime;
        hold = t < 0.35f ? -90.0f + (-170.0f + 90.0f) * (t / 0.35f)
             : t < 0.7f  ? -170.0f + (-40.0f + 170.0f) * ((t - 0.35f) / 0.35f)
             :             -40.0f + (-90.0f + 40.0f) * ((t - 0.7f) / 0.3f);
        m_toolSwing = std::max(0.0f, m_toolSwing - dt);
    }
    float angles[4] = {
        s  * (1 - m_airBlend) + (-165.0f) * m_airBlend,   // Left Arm  (arms up when jumping)
        rightArm * (1 - m_holdBlend) + hold * m_holdBlend,       // Right Arm (straight out when holding a tool)
        -s * (1 - m_airBlend) + (  12.0f) * m_airBlend,   // Left Leg
        s  * (1 - m_airBlend) + ( -12.0f) * m_airBlend,   // Right Leg
    };
    // Climbing: hand over hand, knees up. Swimming: big arm strokes, kicking legs.
    m_climbBlend = approach(m_climbBlend, m_climbing ? 1.0f : 0.0f, 12.0f, dt);
    m_swimBlend = approach(m_swimBlend, m_swimming ? 1.0f : 0.0f, 8.0f, dt);
    const float c = std::sin(m_climbPhase);
    const float climb[4] = {-150.0f + 30.0f * c, -150.0f - 30.0f * c, -35.0f - 25.0f * c, -35.0f + 25.0f * c};
    const float swim[4] = {-90.0f + 85.0f * c, -90.0f - 85.0f * c, 20.0f * std::sin(m_climbPhase * 2.0f),
                           -20.0f * std::sin(m_climbPhase * 2.0f)};
    for (int i = 0; i < 4; ++i)
        angles[i] = angles[i] * (1.0f - m_climbBlend - m_swimBlend) + climb[i] * m_climbBlend + swim[i] * m_swimBlend;

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
    equip(0);   // the tool goes back in the backpack (it'd get in the ragdoll's way)
    footsteps(false, glm::vec3(0.0f));
    if (SceneNode* ff = m_scene->findById(m_spawnFF)) m_scene->removeNode(ff);
    m_spawnFF = 0;
    m_dead = true;
    m_diedFlag = true;
    {
        glm::vec3 at = r->transform.position + glm::vec3(0, 2, 0);
        Audio::play("oof", 0.8f, 1.0f, false, &at);
    }
    m_deadTime = 0.0f;
    m_debris.clear();

    if (m_scene->world().deathStyle == DeathStyle::Ragdoll) {
        glm::vec3 vel(m_velocity.x, std::max(m_velocity.y, -30.0f), m_velocity.z);
        m_ragdoll.start(*m_scene, r, vel, m_pendingImpulse, m_pendingForce);
        m_pendingForce = 0.0f;
        m_pendingImpulse = glm::vec3(0.0f);
        return;
    }

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

    // Classic death with gore on: every piece sprays as it flies.
    if (m_scene->goreEnabled()) {
        GoreKind k = m_scene->goreKind();
        for (auto& d : m_debris)
            if (SceneNode* n = m_scene->findById(d.id))
                m_scene->particles().spray(k, n->transform.position, glm::normalize(d.vel), 8, 3.0f);
        if (m_pendingForce > 0.5f) m_scene->particles().gibs(k, center, glm::vec3(0, 2, 0), 8);
    }
    for (auto& d : m_debris) d.vel += m_pendingImpulse;
    m_pendingForce = 0.0f;
    m_pendingImpulse = glm::vec3(0.0f);
}

void Player::updateDeath(float dt, Physics& physics) {
    m_deadTime += dt;
    if (m_ragdoll.active()) {
        m_ragdoll.update(dt, *m_scene, physics);
        if (m_deadTime >= m_respawnDelay) respawn();
        return;
    }
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
