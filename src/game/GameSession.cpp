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
    m_scripts.start();
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

    // 1. Scripts: wake up waits, keyboard events, Heartbeat.
    m_scripts.update(dt);

    // 2. Physics for loose (unanchored) parts.
    m_physics.gather(*m_scene);
    std::vector<uint64_t> fallen;
    m_physics.stepParts(*m_scene, dt, fallen);
    for (uint64_t id : fallen)
        if (SceneNode* n = m_scene->findById(id)) m_scene->removeNode(n);
    if (!fallen.empty()) m_physics.gather(*m_scene);

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
        if (p->consumeDied()) m_scripts.fireDied();
    }

    // Blood, oil, sparks, smoke...
    m_scene->particles().update(dt, m_scene->world().gravity, m_physics);

    // 4. Touched events (after everything has moved).
    std::vector<TouchEvent> touches;
    m_physics.collectTouches(*m_scene, touches);
    for (const TouchEvent& t : touches) {
        if (!m_scripts.running()) break;
        m_scripts.fireTouched(t.partId, t.otherId);
    }
}

void GameSession::click(uint64_t partId) {
    if (m_running) m_scripts.fireClicked(partId);
}
