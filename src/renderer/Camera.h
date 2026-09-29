#pragma once
#include <glm/glm.hpp>

class Camera {
public:
    Camera();

    // Blender-style orbit: MMB drag = orbit, Shift+MMB = pan, scroll = zoom
    void orbit(float dx, float dy);
    void pan  (float dx, float dy);
    void zoom (float delta);
    // Roblox Studio style: turn your head (the eye stays put) and fly around.
    void look (float dx, float dy);
    void fly  (float forward, float right, float up);   // world units
    void resize(int w, int h);

    glm::mat4 view()       const;
    glm::mat4 projection() const;
    glm::vec3 position()   const;
    glm::vec3 forward()    const;   // the way the camera looks

    float yaw      = 45.0f;
    float pitch    = 25.0f;
    float distance = 8.0f;
    glm::vec3 pivot = {0, 0, 0};
    float fov = 60.0f;

private:
    int m_w = 1, m_h = 1;
};
