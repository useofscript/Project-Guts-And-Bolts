#pragma once
#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include "../renderer/Mesh.h"

enum class PrimitiveType { None, Cube, Sphere, Plane, Cylinder };

// Surface look, à la Roblox materials — affects shading in the lit shader.
enum class Material { Plastic, Metal, Neon, Wood };

struct Transform {
    glm::vec3 position = {0.0f, 0.0f, 0.0f};
    glm::vec3 rotation = {0.0f, 0.0f, 0.0f}; // Euler angles in degrees (XYZ order)
    glm::vec3 scale    = {1.0f, 1.0f, 1.0f};

    glm::mat4 matrix() const;
};

class SceneNode {
public:
    explicit SceneNode(std::string name);

    std::string           name;
    Transform             transform;
    PrimitiveType         primitiveType = PrimitiveType::None;
    std::shared_ptr<Mesh> mesh;
    glm::vec3             color    = {0.65f, 0.65f, 0.80f};
    bool                  selected = false;
    bool                  visible  = true;

    // Appearance
    float    transparency = 0.0f;            // 0 = opaque, 1 = invisible
    Material material      = Material::Plastic;

    // Behaviour (reserved for the future physics / shadow systems)
    bool     anchored   = true;
    bool     canCollide = true;
    bool     castShadow = true;

    SceneNode*                              parent = nullptr;
    std::vector<std::unique_ptr<SceneNode>> children;

    SceneNode* addChild(std::unique_ptr<SceneNode> child);
    void       removeChild(SceneNode* child);
    glm::mat4  worldMatrix() const;
};
