#include "Camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>

Camera::Camera() = default;

void Camera::orbit(float dx, float dy) {
    yaw   += dx * 0.35f;
    pitch  = std::clamp(pitch + dy * 0.35f, -89.0f, 89.0f);
}

void Camera::pan(float dx, float dy) {
    float speed = distance * 0.0012f;
    glm::mat4 v = view();
    glm::vec3 right = {v[0][0], v[1][0], v[2][0]};
    glm::vec3 up    = {v[0][1], v[1][1], v[2][1]};
    pivot -= right * (dx * speed);
    pivot += up    * (dy * speed);
}

void Camera::zoom(float delta) {
    distance = std::max(0.3f, distance - delta * (distance * 0.15f));
}

void Camera::resize(int w, int h) { m_w = w; m_h = h; }

glm::vec3 Camera::position() const {
    float yr = glm::radians(yaw);
    float pr = glm::radians(pitch);
    return pivot + glm::vec3(
        distance * std::cos(pr) * std::cos(yr),
        distance * std::sin(pr),
        distance * std::cos(pr) * std::sin(yr)
    );
}

glm::mat4 Camera::view() const {
    return glm::lookAt(position(), pivot, glm::vec3(0,1,0));
}

glm::mat4 Camera::projection() const {
    float aspect = (m_h > 0) ? (float)m_w / (float)m_h : 1.0f;
    return glm::perspective(glm::radians(fov), aspect, 0.01f, 1000.0f);
}
