#include "GameSession.h"
#include "../scene/Serializer.h"
#include "../scripting/LuaApi.h"   // SignalKind (tool events)
#include "../scene/Scene.h"
#include "../scene/Player.h"
#include "../core/Audio.h"

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
    // Sounds marked "Autoplay" (e.g. background music) start with the game.
    m_scene->forEach([](SceneNode* n) {
        if (!n->isSound() || !n->autoplay) return;
        glm::vec3 at = n->parent ? glm::vec3(n->parent->worldMatrix()[3]) : glm::vec3(0.0f);
        bool in3d = n->parent && n->parent->isPart();
        n->audioHandle = Audio::play(n->soundId, n->volume, n->pitch, n->looped, in3d ? &at : nullptr);
    });
    // Animations played by scripts pose the character from how it was built
    // (not mid-step), and leave the walking arms and legs to the walk cycle.
    Anim::Animator& an = m_scene->animator();
    an.clear();
    an.restFor = [this](uint64_t rig) -> const Anim::RestPose* {
        Player* p = m_scene->player();
        return p && p->rootId() == rig && !p->restPose().empty() ? &p->restPose() : nullptr;
    };
    an.drivenElsewhere = [this](uint64_t rig, const SceneNode* part) {
        Player* p = m_scene->player();
        return p && p->rootId() == rig && p->drivesPart(part);
    };
    if (m_role != Role::Client) setupTools();   // before the scripts, so tools' scripts start with the rest
    if (m_role != Role::Client) m_scripts.start();
}

// ---------------------------------------------------------------------------
// Tools
// ---------------------------------------------------------------------------

void GameSession::setupTools() {
    m_starterPack.clear();
    m_noPickupUntil.clear();
    m_toolDown = false;
    Player* p = m_runOnly ? nullptr : m_scene->player();
    // StarterPack tools are templates: everyone gets a copy each time they spawn.
    std::vector<SceneNode*> starters;
    m_scene->forEach([&](SceneNode* n) {
        if (n->isTool() && n->starterTool && !m_scene->isCharacterPart(n)) starters.push_back(n);
    });
    for (SceneNode* t : starters)
        if (auto owned = m_scene->detach(t)) m_starterPack.push_back(std::move(owned));
    if (!p) return;
    p->onToolEquip = [this](uint64_t tool, bool equipped) {
        if (m_scripts.running()) m_scripts.fireTool(equipped ? SignalKind::Equipped : SignalKind::Unequipped, tool);
    };
    giveStarterTools();
    p->consumeRespawned();
}

void GameSession::giveStarterTools() {
    Player* p = m_scene->player();
    if (!p) return;
    for (const auto& tpl : m_starterPack) {
        SceneNode* copy = m_scene->insert(Serializer::clone(*tpl));
        if (!p->give(copy)) { m_scene->removeNode(copy); continue; }
        m_scripts.runScriptsIn(copy);   // (does nothing before the scripts have started)
    }
}

void GameSession::pickUpTools(const std::vector<TouchEvent>& touches) {
    Player* p = m_scene->player();
    SceneNode* me = p ? p->root() : nullptr;
    if (!me || p->isDead()) return;
    for (const TouchEvent& t : touches) {
        SceneNode* part = m_scene->findById(t.partId);
        SceneNode* other = m_scene->findById(t.otherId);
        if (!part || !other || part->name != "Handle" || !part->parent || !part->parent->isTool()) continue;
        if (other != me && other->parent != me) continue;             // only your own character picks things up
        SceneNode* tool = part->parent;
        if (m_scene->isCharacterPart(tool)) continue;                  // someone's already got it
        if (auto it = m_noPickupUntil.find(tool->id); it != m_noPickupUntil.end() && m_time < it->second) continue;
        p->give(tool);
    }
}

void GameSession::reachCheckpoints(const std::vector<TouchEvent>& touches) {
    Player* p = m_scene->player();
    SceneNode* me = p ? p->root() : nullptr;
    if (!me || p->isDead()) return;
    for (const TouchEvent& t : touches) {
        SceneNode* part = m_scene->findById(t.partId);
        SceneNode* other = m_scene->findById(t.otherId);
        if (!part || !other || part->name != "Checkpoint" || p->checkpoint() == part->id) continue;
        if (other != me && other->parent != me) continue;
        p->setCheckpoint(part->id);
        glm::vec3 at(part->worldMatrix()[3]);
        Audio::play("coin", 0.5f, 1.4f, false, &at);
        if (m_scripts.gui().messageTime <= 0.0f) {   // don't talk over the game's own messages
            m_scripts.gui().message = "Checkpoint!";
            m_scripts.gui().messageTime = 1.5f;
        }
    }
}

void GameSession::selectToolSlot(int slot) {
    if (!m_running || m_role == Role::Client) return;
    if (Player* p = m_scene->player(); p && !p->isDead()) p->toggleSlot(slot);
}

void GameSession::dropTool() {
    if (!m_running || m_role == Role::Client) return;
    Player* p = m_scene->player();
    if (!p) return;
    if (SceneNode* t = p->drop()) m_noPickupUntil[t->id] = m_time + 2.0;
}

void GameSession::stop() {
    m_scripts.stop();
    m_scene->animator().clear();
    if (Player* p = m_scene->player()) p->onToolEquip = nullptr;
    m_starterPack.clear();
    Audio::stopAll();
    if (Player* p = m_scene->player()) p->endPlay();
    m_physics.reset();
    m_scene->particles().clear();
    m_running = false;
}

void GameSession::update(float dt, float cameraYaw, bool acceptInput) {
    if (!m_running) return;
    dt = std::min(dt, 1.0f / 30.0f);   // big hitches would let things tunnel

    const bool client = m_role == Role::Client;
    m_time += dt;

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
    if (Player* p = m_runOnly ? nullptr : m_scene->player()) {
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
            // Touch thumbstick: partly pushed = walk slower.
            move += right * m_touchMove.x + fwd * m_touchMove.y;
            jump = jump || m_touchJump;
        }
        // Tools: 1-9 picks a slot (again puts it away), Backspace drops the held one.
        if (!client && acceptInput && !ImGui::GetIO().WantTextInput) {
            for (int i = 0; i < Player::kMaxTools; ++i)
                if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + i), false)) selectToolSlot(i);
            if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) dropTool();
        }
        if (m_toolDown && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            m_toolDown = false;
            if (SceneNode* t = p->equippedTool()) m_scripts.fireTool(SignalKind::Deactivated, t->id);
        }
        p->update(dt, move, jump, m_physics);
        if (p->isDead()) m_scene->animator().stopRig(p->rootId(), false, *m_scene);   // the body falls apart instead
        if (p->consumeDied()) m_scripts.fireDied(p->rootId());
        if (!client && p->consumeRespawned()) {
            // Like Roblox: you come back with just the StarterPack tools.
            for (SceneNode* t : p->tools()) m_scripts.stopScripts(t);
            p->clearTools();
            giveStarterTools();
        }
    }

    // Animations (after the character has walked, so they win where they pose).
    m_scene->animator().update(dt, *m_scene);
    if (!client) m_scripts.fireAnimationEvents();
    else m_scene->animator().events.clear();

    // Sounds inside moving parts follow them.
    m_scene->forEach([](SceneNode* n) {
        if (n->isSound() && n->audioHandle && n->parent && n->parent->isPart())
            Audio::setPosition(n->audioHandle, glm::vec3(n->parent->worldMatrix()[3]));
    });

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
    pickUpTools(touches);
    reachCheckpoints(touches);
}

void GameSession::guiEvents(const std::vector<GameGui::Event>& events) {
    if (!m_running) return;
    for (const GameGui::Event& e : events) {
        if (m_role == Role::Client) {   // the host runs the scripts: tell it about clicks
            if (e.kind == GameGui::EventKind::Click && onGuiClick) onGuiClick(e.id);
            continue;
        }
        SignalKind kind = e.kind == GameGui::EventKind::Click ? SignalKind::GuiClick
                        : e.kind == GameGui::EventKind::Enter ? SignalKind::GuiEnter : SignalKind::GuiLeave;
        m_scripts.fireGui(kind, e.id);
    }
}

void GameSession::click(uint64_t partId) {
    if (!m_running) return;
    if (m_role == Role::Client) { if (onClick && partId) onClick(partId); return; }
    m_scripts.fireClicked(partId);
    // Holding a tool: clicking uses it (tool.Activated), like Roblox.
    if (Player* p = m_runOnly ? nullptr : m_scene->player())
        if (SceneNode* t = p->equippedTool(); t && t->enabled && !p->isDead()) {
            m_scripts.fireTool(SignalKind::Activated, t->id);
            p->swingTool();
            m_toolDown = true;
        }
}
