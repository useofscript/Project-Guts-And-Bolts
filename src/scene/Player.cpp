#include "Player.h"
#include "Scene.h"
#include "SceneNode.h"
#include "../renderer/Primitives.h"

#include <glm/glm.hpp>
#include <cmath>
#include <memory>

void Player::build(Scene* scene) {
    m_root_scene = scene;

    // Container node for the whole character (origin at the feet).
    m_root = scene->addNode("Player", PrimitiveType::None, nullptr);
    m_root->transform.position = m_spawn;

    auto box = Primitives::createCube();   // one cube mesh shared by every part

    auto addPart = [&](SceneNode* parent, const std::string& name,
                       glm::vec3 pos, glm::vec3 scale, glm::vec3 col,
                       bool internal = false, bool visible = true) -> SceneNode* {
        auto node = std::make_unique<SceneNode>(name);
        node->primitiveType = PrimitiveType::Cube;
        node->mesh          = box;
        node->transform.position = pos;
        node->transform.scale    = scale;
        node->color    = col;
        node->internal = internal;
        node->visible  = visible;
        return parent->addChild(std::move(node));
    };

    const glm::vec3 kYellow = {1.00f, 0.84f, 0.30f};   // head + arms
    const glm::vec3 kBlue   = {0.20f, 0.45f, 0.85f};   // torso
    const glm::vec3 kGreen  = {0.32f, 0.60f, 0.26f};   // legs
    const glm::vec3 kBlack  = {0.05f, 0.05f, 0.05f};   // face

    // Roblox part order. HumanoidRootPart is an invisible reference part.
    addPart(m_root, "HumanoidRootPart", {0.0f, 1.5f, 0.0f}, {1.0f, 1.0f, 0.5f}, kBlue, false, false);
    addPart(m_root, "Torso",            {0.0f, 1.5f, 0.0f}, {1.0f, 1.0f, 0.5f}, kBlue);
    SceneNode* head =
    addPart(m_root, "Head",             {0.0f, 2.325f, 0.0f}, {0.65f, 0.65f, 0.65f}, kYellow);
    addPart(m_root, "Left Arm",         {-0.75f, 1.5f, 0.0f}, {0.5f, 1.0f, 0.5f}, kYellow);
    addPart(m_root, "Right Arm",        { 0.75f, 1.5f, 0.0f}, {0.5f, 1.0f, 0.5f}, kYellow);
    addPart(m_root, "Left Leg",         {-0.25f, 0.5f, 0.0f}, {0.5f, 1.0f, 0.5f}, kGreen);
    addPart(m_root, "Right Leg",        { 0.25f, 0.5f, 0.0f}, {0.5f, 1.0f, 0.5f}, kGreen);

    // --- Smiley face on the front (+Z) of the head, in head-local space ---
    addPart(head, "Eye.L",  {-0.18f, 0.12f, 0.5f}, {0.13f, 0.16f, 0.06f}, kBlack, true);
    addPart(head, "Eye.R",  { 0.18f, 0.12f, 0.5f}, {0.13f, 0.16f, 0.06f}, kBlack, true);

    // Smile: small cubes along an upward-opening curve.
    const float sx[5] = {-0.24f, -0.12f, 0.0f, 0.12f, 0.24f};
    for (int i = 0; i < 5; ++i) {
        float x = sx[i];
        float y = -0.20f + 0.10f * (x / 0.24f) * (x / 0.24f);   // middle lowest
        addPart(head, "Smile", {x, y, 0.5f}, {0.08f, 0.09f, 0.06f}, kBlack, true);
    }
}

glm::vec3 Player::position() const {
    return m_root ? m_root->transform.position : glm::vec3(0.0f);
}

void Player::update(float dt, const glm::vec3& moveDir, bool jump) {
    if (!m_root) return;
    glm::vec3 pos = m_root->transform.position;

    // Horizontal movement.
    glm::vec3 horiz = {moveDir.x, 0.0f, moveDir.z};
    float len = glm::length(horiz);
    if (len > 1e-4f) {
        horiz /= len;
        pos.x += horiz.x * m_humanoid.walkSpeed * dt;
        pos.z += horiz.z * m_humanoid.walkSpeed * dt;
        if (m_humanoid.autoRotate)
            m_root->transform.rotation.y = glm::degrees(std::atan2(horiz.x, horiz.z));
    }

    // Gravity + jumping.
    const float gravity = -15.0f;
    if (m_grounded && jump) { m_velocity.y = m_humanoid.jumpPower; m_grounded = false; }
    m_velocity.y += gravity * dt;
    pos.y += m_velocity.y * dt;

    // Ground collision (the feet rest on the y = 0 plane).
    if (pos.y <= 0.0f) { pos.y = 0.0f; m_velocity.y = 0.0f; m_grounded = true; }

    m_root->transform.position = pos;
}

void Player::reset() {
    if (m_root) {
        m_root->transform.position = m_spawn;
        m_root->transform.rotation = {0.0f, 0.0f, 0.0f};
    }
    m_velocity = {0.0f, 0.0f, 0.0f};
    m_grounded = false;
    m_humanoid.health = m_humanoid.maxHealth;
}
