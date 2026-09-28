#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include "../renderer/Mesh.h"

enum class PrimitiveType { None, Cube, Sphere, Plane, Cylinder };

// What an object *is*, à la Roblox classes:
//   Part   — a visible, physical shape (Cube / Sphere / Plane / Cylinder)
//   Model  — an empty container used to group other objects
//   Script — Lua code that runs when you press Play (script.Parent = its parent)
//   Light  — a PointLight / SpotLight, usually placed inside a part
//   ForceField — inside a character: a glowing shield (like Roblox's spawn ForceField)
//   Sound  — a sound effect or music (inside a part = it comes from there)
//   Attachment — a point on a part that constraints connect to
//   Constraint — joins two parts: rope, rod, spring, weld or hinge (+ motor)
enum class NodeKind { Part, Model, Script, Light, ForceField, Sound, Attachment, Constraint };

enum class ConstraintType { Rope, Rod, Spring, Weld, Hinge };
inline const char* const kConstraintNames[5] = {"Rope", "Rod", "Spring", "Weld", "Hinge"};

enum class LightType { Point, Spot };

// Surface look, à la Roblox materials — affects shading in the lit shader.
enum class Material { Plastic, Metal, Neon, Wood, Glass, Concrete, Ice };
inline constexpr int kMaterialCount = 7;
inline const char* const kMaterialNames[kMaterialCount] =
    {"Plastic", "Metal", "Neon", "Wood", "Glass", "Concrete", "Ice"};

struct Transform {
    glm::vec3 position = {0.0f, 0.0f, 0.0f};
    glm::vec3 rotation = {0.0f, 0.0f, 0.0f}; // Euler angles in degrees (XYZ order)
    glm::vec3 scale    = {1.0f, 1.0f, 1.0f};

    glm::mat4 matrix() const;
};

class SceneNode {
public:
    explicit SceneNode(std::string name, NodeKind kind = NodeKind::Part);

    uint64_t              id;               // unique, stable across undo / save
    std::string           name;
    NodeKind              kind = NodeKind::Part;
    Transform             transform;
    PrimitiveType         primitiveType = PrimitiveType::None;
    std::shared_ptr<Mesh> mesh;
    glm::vec3             color    = {0.65f, 0.65f, 0.80f};
    bool                  selected = false;
    bool                  visible  = true;
    bool                  internal = false;  // hidden from the outliner / picking

    // Appearance
    float    transparency = 0.0f;            // 0 = opaque, 1 = invisible
    Material material      = Material::Plastic;

    // Behaviour
    bool     anchored   = true;              // false = falls with gravity in Play
    bool     canCollide = true;              // false = things pass through it
    bool     castShadow = true;

    // Script (kind == Script)
    std::string source;
    bool        enabled = true;           // scripts and lights can be switched off

    // Light (kind == Light) — uses `color` for its colour.
    LightType   lightType  = LightType::Point;
    float       brightness = 2.0f;
    float       range      = 14.0f;
    float       spotAngle  = 60.0f;       // cone width in degrees (spot lights)

    // Sound (kind == Sound)
    std::string soundId  = "coin";         // built-in name or a file in the games folder
    float       volume   = 0.6f;
    float       pitch    = 1.0f;
    bool        looped   = false;
    bool        autoplay = false;          // start playing when the game starts
    int         audioHandle = 0;           // runtime only

    // Physics (parts). Negative = use the material's value.
    float       density    = -1.0f;
    float       friction   = -1.0f;
    float       elasticity = -1.0f;        // bounciness 0..1

    // Constraint (kind == Constraint). ref0 / ref1 are Attachments (or, for a
    // Weld, the two Parts).
    ConstraintType constraintType = ConstraintType::Rope;
    uint64_t    ref0 = 0, ref1 = 0;
    float       length      = -1.0f;       // rope / rod / spring length (-1 = as placed)
    float       stiffness   = 200.0f;      // spring
    float       damping     = 5.0f;        // spring
    float       motorSpeed  = 0.0f;        // hinge motor, radians / second
    float       motorTorque = 0.0f;        // hinge motor strength (0 = no motor)
    float       thickness   = 0.1f;        // how thick the rope / rod looks

    // Runtime-only physics state (not saved).
    glm::vec3   angularVelocity = {0.0f, 0.0f, 0.0f};
    float       sleepTime = 0.0f;
    bool        jointReady = false;        // weld: rest pose captured
    glm::vec3   jointA{0.0f}, jointB{0.0f};
    glm::vec4   jointRot{0.0f, 0.0f, 0.0f, 1.0f};

    // Runtime-only physics state (not saved).
    glm::vec3   velocity = {0.0f, 0.0f, 0.0f};

    SceneNode*                              parent = nullptr;
    std::vector<std::unique_ptr<SceneNode>> children;

    bool isPart()   const { return kind == NodeKind::Part && mesh != nullptr; }
    bool isScript() const { return kind == NodeKind::Script; }
    bool isLight()  const { return kind == NodeKind::Light; }
    bool isSound()  const { return kind == NodeKind::Sound; }
    bool isAttachment() const { return kind == NodeKind::Attachment; }
    bool isConstraint() const { return kind == NodeKind::Constraint; }
    bool hasForceField() const {
        for (auto& c : children) if (c->kind == NodeKind::ForceField) return true;
        return false;
    }

    SceneNode*                 addChild(std::unique_ptr<SceneNode> child);
    void                       removeChild(SceneNode* child);
    std::unique_ptr<SceneNode> detachChild(SceneNode* child);  // keeps it alive
    SceneNode*                 findChild(const std::string& name, bool recursive = false) const;
    bool                       isAncestorOf(const SceneNode* other) const;
    glm::mat4                  worldMatrix() const;
    std::string                fullName() const;   // e.g. "Workspace.Door.Script"

    // Make sure freshly created nodes never reuse an id loaded from a file.
    static void     reserveId(uint64_t used);
    static uint64_t newId();
};
