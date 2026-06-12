#pragma once
#include <glm/glm.hpp>

class Camera {
public:
    Camera();

    // Blender-style orbit: MMB drag = orbit, Shift+MMB = pan, scroll = zoom
    void orbit(float dx, float dy);
    void pan  (float dx, float dy);
    void zoom (float delta);
    void resize(int w, int h);

    glm::mat4 view()       const;
    glm::mat4 projection() const;
    glm::vec3 position()   const;

    float yaw      = 45.0f;
    float pitch    = 25.0f;
    float distance = 8.0f;
    glm::vec3 pivot = {0, 0, 0};
    float fov = 60.0f;

private:
    int m_w = 1, m_h = 1;
};
