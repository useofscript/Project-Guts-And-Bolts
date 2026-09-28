#include "GameSession.h"
#include "../scene/Scene.h"
#include "../scene/Player.h"

#include <imgui.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

GameSession::GameSession(Scene* scene) : m_scene(scene), m_scripts(scene) {}

void GameSession::start() {
    m_physics.reset();
    m_scene->particles().clear();
    if (Player* p = m_scene->player()) p->beginPlay();
    m_running = true;
    if (m_role != Role::Client) m_scripts.start();
}

void GameSession::stop() {
    m_scripts.stop();
    if (Player* p = m_scene->player()) p->endPlay();
    m_physics.reset();
    m_scene->particles().clear();
    m_running = false;
}

void GameSession::update(float dt, float cameraYaw, bool acceptInput) {
    if (!m_running) return;
    dt = std::min(dt, 1.0f / 30.0f);   // big hitches would let things tunnel

    const bool client = m_role == Role::Client;

    // 1. Scripts: wake up waits, keyboard events, Heartbeat.
    if (!client) m_scripts.update(dt);
    else if (m_scripts.gui().messageTime > 0.0f) m_scripts.gui().messageTime -= dt;

    // 2. Physics for loose (unanchored) parts (the host does this for everyone).
    m_physics.gather(*m_scene);
    if (!client) {
        std::vector<uint64_t> fallen;
        m_physics.stepParts(*m_scene, dt, fallen);
        for (uint64_t id : fallen)
            if (SceneNode* n = m_scene->findById(id)) m_scene->removeNode(n);
        if (!fallen.empty()) m_physics.gather(*m_scene);
    }

    // 3. The character, driven by WASD / Space relative to the camera.
    if (Player* p = m_scene->player()) {
        glm::vec3 move(0.0f);
        bool jump = false;
        if (acceptInput && !ImGui::GetIO().WantTextInput) {
            float yaw = glm::radians(cameraYaw);
            glm::vec3 fwd   = glm::normalize(glm::vec3(-std::cos(yaw), 0.0f, -std::sin(yaw)));
            glm::vec3 right = glm::vec3(-fwd.z, 0.0f, fwd.x);
            if (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow))    move += fwd;
            if (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow))  move -= fwd;
            if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) move += right;
            if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow))  move -= right;
            jump = ImGui::IsKeyDown(ImGuiKey_Space);
        }
        p->update(dt, move, jump, m_physics);
        if (p->consumeDied()) m_scripts.fireDied(p->rootId());
    }

    // Blood, oil, sparks, smoke...
    m_scene->particles().update(dt, m_scene->world().gravity, m_physics);

    // 4. Touched events (after everything has moved).
    std::vector<TouchEvent> touches;
    m_physics.collectTouches(*m_scene, touches);
    if (client) {
        // Only our own character's touches matter here; the host runs the scripts.
        Player* p = m_scene->player();
        for (const TouchEvent& t : touches) {
            SceneNode* limb = m_scene->findById(t.otherId);
            if (p && limb && limb->parent && limb->parent->id == p->rootId() && onTouch)
                onTouch(t.partId, limb->name);
        }
        return;
    }
    for (const TouchEvent& t : touches) {
        if (!m_scripts.running()) break;
        m_scripts.fireTouched(t.partId, t.otherId);
    }
}

void GameSession::click(uint64_t partId) {
    if (!m_running) return;
    if (m_role == Role::Client) { if (onClick && partId) onClick(partId); return; }
    m_scripts.fireClicked(partId);
}
