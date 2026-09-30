#include "PlayCamera.h"
#include "../renderer/Camera.h"
#include "../scene/Player.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../core/Settings.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace PlayCamera {

namespace {
constexpr float kSnapIn  = 1.2f;   // closer than this = first person
constexpr float kSnapOut = 1.6f;   // scrolling out of first person lands here
constexpr float kFadeStart = 2.4f; // the character starts fading this close
constexpr float kFadeEnd   = 0.7f; // and is gone by here
}

bool firstPerson(const Camera& cam) { return cam.distance < 0.01f; }
float swimLook(const Camera& cam) {
    const float y = cam.forward().y;
    return std::clamp(firstPerson(cam) ? y : y + 0.4f, -1.0f, 1.0f);
}

void zoom(Camera& cam, float wheel) {
    if (wheel == 0.0f) return;
    if (firstPerson(cam)) {
        if (wheel < 0.0f) cam.distance = kSnapOut;
        return;
    }
    cam.distance -= wheel * std::max(0.4f, cam.distance * 0.15f);
    cam.distance = std::min(cam.distance, kMaxZoom);
    if (cam.distance < kSnapIn) cam.distance = 0.0f;
}

void turn(Camera& cam, float dx, float dy) {
    const GraphicsSettings& gs = GraphicsSettings::get();
    cam.orbit(dx * gs.mouseSensitivity, (gs.invertCamera ? -dy : dy) * gs.mouseSensitivity);
}

void follow(Camera& cam, Player& player, float dt, bool shiftLock) {
    glm::vec3 target = player.focusPoint();
    if (shiftLock && !firstPerson(cam)) {
        // Over the right shoulder (Roblox moves the camera 1.75 studs right).
        glm::vec3 f = cam.forward();
        glm::vec3 right = glm::normalize(glm::cross(f, glm::vec3(0, 1, 0)) + glm::vec3(1e-6f));
        target += right * 0.875f;
        if (!player.isDead() && f.x * f.x + f.z * f.z > 1e-6f) player.faceYaw(glm::degrees(std::atan2(f.x, f.z)));
    }
    if (firstPerson(cam)) {
        cam.pivot = target;   // right in the head, no lag
        if (!player.isDead()) {
            glm::vec3 f = cam.forward();
            if (f.x * f.x + f.z * f.z > 1e-6f) player.faceYaw(glm::degrees(std::atan2(f.x, f.z)));
        }
    } else {
        cam.pivot += (target - cam.pivot) * std::min(1.0f, dt * 12.0f);
    }
}

void fade(Scene& scene, Player& player, const Camera& cam) {
    SceneNode* root = player.root();
    if (!root) return;
    const float d = firstPerson(cam) ? 0.0f : cam.distance;
    float t = std::clamp((kFadeStart - d) / (kFadeStart - kFadeEnd), 0.0f, 1.0f);
    if (player.isDead()) t = 0.0f;   // watch yourself fall apart
    SceneNode* tool = player.equippedTool();
    std::vector<SceneNode*> stack{root};
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        if (n == tool) continue;     // you still see what you're holding
        n->localTransparency = t;
        for (auto& c : n->children) stack.push_back(c.get());
    }
    (void)scene;
}

} // namespace PlayCamera
