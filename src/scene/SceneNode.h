#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include "../renderer/Mesh.h"

struct EditMesh;   // see EditMesh.h

enum class PrimitiveType { None, Cube, Sphere, Plane, Cylinder, Mesh };   // Mesh = built in Modeling mode

// What an object *is*, à la Roblox classes:
//   Part   — a visible, physical shape (Cube / Sphere / Plane / Cylinder)
//   Model  — an empty container used to group other objects
//   Script — Lua code that runs when you press Play (script.Parent = its parent)
//   Light  — a PointLight / SpotLight, usually placed inside a part
//   ForceField — inside a character: a glowing shield (like Roblox's spawn ForceField)
//   Sound  — a sound effect or music (inside a part = it comes from there)
//   Attachment — a point on a part that constraints connect to
//   Constraint — joins two parts: rope, rod, spring, weld or hinge (+ motor)
//   Tool       — something a character can carry and use (a sword, a gun...): a
//                container whose part named "Handle" goes in the character's hand
//   Value      — holds one value (IntValue, NumberValue, StringValue, BoolValue,
//                Vector3Value, Color3Value); leaderstats are made of these
//   Decal      — a picture on one side of the part it's inside
//   Animation  — keyframes that pose a rig's parts (made in Studio's Animation
//                Editor, played by scripts: humanoid:LoadAnimation(anim):Play())
enum class NodeKind { Part, Model, Script, Light, ForceField, Sound, Attachment, Constraint, Tool, Value, Decal, Animation, Gui,
                      FluidSystem, FluidEmitter };

// Game UI (kind == Gui), like Roblox's: a ScreenGui holds Frames, labels,
// buttons and pictures, laid out with UDim2 (a fraction of the parent plus pixels).
enum class GuiType { ScreenGui, Frame, TextLabel, TextButton, ImageLabel, ImageButton, UICorner, UIStroke };
inline constexpr int kGuiTypeCount = 8;
inline const char* const kGuiClassNames[kGuiTypeCount] = {"ScreenGui", "Frame", "TextLabel", "TextButton",
                                                          "ImageLabel", "ImageButton", "UICorner", "UIStroke"};
struct UDim2 {
    float xs = 0.0f, xo = 0.0f, ys = 0.0f, yo = 0.0f;   // X scale, X offset (pixels), Y scale, Y offset
    bool operator==(const UDim2& o) const { return xs == o.xs && xo == o.xo && ys == o.ys && yo == o.yo; }
};
struct GuiProps {
    GuiType   type = GuiType::Frame;
    UDim2     pos;                                  // Position
    UDim2     size{0.0f, 100.0f, 0.0f, 100.0f};     // Size
    glm::vec2 anchor{0.0f};                         // AnchorPoint (0..1: which point of it sits at Position)
    glm::vec3 bg{1.0f};                             // BackgroundColor3
    float     bgTransparency = 0.0f;
    glm::vec3 borderColor{0.11f, 0.16f, 0.2f};      // BorderColor3 (and UIStroke.Color)
    int       border = 1;                           // BorderSizePixel
    int       zIndex = 1;
    bool      clips = false;                        // ClipsDescendants
    // Text (TextLabel / TextButton)
    std::string text;
    glm::vec3 textColor{0.1f};
    float     textSize = 14.0f;
    bool      textScaled = false, textWrapped = false, bold = false;
    int       xAlign = 1, yAlign = 1;               // 0 = Left/Top, 1 = Center, 2 = Right/Bottom
    float     textTransparency = 0.0f;
    glm::vec3 strokeColor{0.0f};                    // TextStrokeColor3
    float     strokeTransparency = 1.0f;            // TextStrokeTransparency (1 = no outline)
    // Pictures (ImageLabel / ImageButton): "gb:<id>" or a file, like a Decal
    std::string image;
    glm::vec3 imageColor{1.0f};
    float     imageTransparency = 0.0f;
    bool      autoButtonColor = true;               // buttons darken when you point at / press them
    int       displayOrder = 0;                     // ScreenGui: higher ones are drawn on top
    UDim2     corner{0.0f, 8.0f, 0.0f, 0.0f};       // UICorner.CornerRadius (xs, xo used)
    float     thickness = 1.0f;                     // UIStroke.Thickness
    // Where it was last drawn (AbsolutePosition / AbsoluteSize). Runtime only.
    glm::vec2 absPos{0.0f}, absSize{0.0f};
};

// Sides of a part, in Roblox's NormalId order.
enum class Face { Right, Top, Back, Left, Bottom, Front };
inline const char* const kFaceNames[6] = {"Right", "Top", "Back", "Left", "Bottom", "Front"};

enum class ConstraintType { Rope, Rod, Spring, Weld, Hinge };
inline const char* const kConstraintNames[5] = {"Rope", "Rod", "Spring", "Weld", "Hinge"};

enum class LightType { Point, Spot };

// Surface look, à la Roblox materials — affects shading in the lit shader.
enum class Material { Plastic, Metal, Neon, Wood, Glass, Concrete, Ice };
inline constexpr int kMaterialCount = 7;
inline const char* const kMaterialNames[kMaterialCount] =
    {"Plastic", "Metal", "Neon", "Wood", "Glass", "Concrete", "Ice"};

// A custom value on an object, like Roblox Attributes (Properties panel >
// Attributes, or part:SetAttribute("Coins", 5) in a script).
struct Attribute {
    enum Type { Bool, Number, String, Vector3, Color3 };
    std::string name;
    Type        type = Number;
    bool        b = false;
    double      n = 0.0;
    std::string s;
    glm::vec3   v{0.0f};   // Vector3 / Color3
};

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
    std::shared_ptr<EditMesh> editMesh;    // primitiveType == Mesh: the shape's points and faces
    glm::vec3             color    = {0.65f, 0.65f, 0.80f};
    bool                  selected = false;
    bool                  visible  = true;
    bool                  internal = false;  // hidden from the outliner / picking

    // Appearance
    float    transparency = 0.0f;            // 0 = opaque, 1 = invisible
    // Extra see-through-ness only this screen uses (Roblox's LocalTransparencyModifier):
    // your own character fades as the camera comes close. Never saved or sent.
    float    localTransparency = 0.0f;
    float    shownTransparency() const { return 1.0f - (1.0f - transparency) * (1.0f - localTransparency); }
    Material material      = Material::Plastic;

    // Behaviour
    bool     anchored   = true;              // false = falls with gravity in Play
    bool     canCollide = true;              // false = things pass through it
    bool     castShadow = true;

    // Script (kind == Script); an Animation's keyframes (JSON, see Animation.h)
    std::string source;
    bool        enabled = true;           // scripts and lights can be switched off
    bool        isModule = false;         // ModuleScript: only runs when require()d

    // Anything
    std::vector<Attribute>   attributes;
    std::vector<std::string> tags;        // CollectionService-style tags
    bool        locked = false;           // can't be picked in the Viewport (Roblox "Locked")
    const Attribute* findAttribute(const std::string& n) const {
        for (auto& a : attributes) if (a.name == n) return &a;
        return nullptr;
    }

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

    // Decal (kind == Decal): which picture, on which side. `color` tints it
    // (white = as it is), `transparency` fades it.
    std::string texture;                   // "gb:<id>", a file in the games folder, or a path
    Face        face = Face::Front;

    // Game UI (kind == Gui). `visible` is Visible; `enabled` is a ScreenGui's Enabled.
    GuiProps    gui;

    // Value (kind == Value): its type and contents (the name inside isn't used).
    Attribute   value;
    bool        intValue = false;          // an IntValue (Number, whole numbers only)

    // Tool (kind == Tool). `enabled` is Tool.Enabled.
    std::string toolTip;                   // shown when you hover its hotbar slot
    bool        canBeDropped = true;       // Backspace drops it
    bool        starterTool  = false;      // everyone gets one when they spawn (like Roblox's StarterPack)
    glm::vec3   gripPos{0.0f};             // the point on the Handle (Handle's own space) that sits in the hand

    // Real liquid (see Liquid.h). A FluidSystem is a kind of liquid: `color`, how thick
    // it is and how much it sticks together. A FluidEmitter pours it out: its
    // transform's position and scale are its Position and Size; `enabled` turns it on and off.
    float       viscosity      = 0.015f;   // FluidSystem: 0 = runny like water, 1 = thick like honey
    float       surfaceTension = 0.0f;     // FluidSystem: 0..1, how much drops pull together (beads, strands)
    float       fluidRate      = 500.0f;   // FluidEmitter: drops per second
    glm::vec3   fluidVelocity{0.0f, -10.0f, 0.0f};   // FluidEmitter: how fast (and which way) it pours
    uint64_t    fluidSystem    = 0;        // FluidEmitter: its FluidSystem (0 = plain water)

    // Runtime-only physics state (not saved).
    glm::vec3   angularVelocity = {0.0f, 0.0f, 0.0f};
    float       sleepTime = 0.0f;
    bool        jointReady = false;        // weld: rest pose captured
    glm::vec3   jointA{0.0f}, jointB{0.0f};
    glm::vec4   jointRot{0.0f, 0.0f, 0.0f, 1.0f};

    // Runtime-only physics state (not saved).
    glm::vec3   velocity = {0.0f, 0.0f, 0.0f};

    // While Studio's Animation Editor shows this part posed: where it really
    // is (that's what gets saved, copied and undone). Runtime only.
    std::shared_ptr<Transform> restPose;

    SceneNode*                              parent = nullptr;
    std::vector<std::unique_ptr<SceneNode>> children;

    bool isPart()   const { return kind == NodeKind::Part && mesh != nullptr; }
    bool isScript() const { return kind == NodeKind::Script; }
    bool isLight()  const { return kind == NodeKind::Light; }
    bool isSound()  const { return kind == NodeKind::Sound; }
    bool isTool()   const { return kind == NodeKind::Tool; }
    bool isValue()  const { return kind == NodeKind::Value; }
    bool isDecal()  const { return kind == NodeKind::Decal; }
    bool isAnimation() const { return kind == NodeKind::Animation; }
    bool isGui() const { return kind == NodeKind::Gui; }
    // A Frame / label / button / picture (not a ScreenGui, UICorner or UIStroke).
    bool isGuiObject() const {
        return kind == NodeKind::Gui && gui.type != GuiType::ScreenGui && gui.type != GuiType::UICorner && gui.type != GuiType::UIStroke;
    }
    bool isGuiButton() const { return kind == NodeKind::Gui && (gui.type == GuiType::TextButton || gui.type == GuiType::ImageButton); }
    // "IntValue", "StringValue"... (kind == Value)
    const char* valueClass() const {
        switch (value.type) {
            case Attribute::Bool:    return "BoolValue";
            case Attribute::String:  return "StringValue";
            case Attribute::Vector3: return "Vector3Value";
            case Attribute::Color3:  return "Color3Value";
            default:                 return intValue ? "IntValue" : "NumberValue";
        }
    }
    std::string valueText() const;   // for showing it (the leaderboard, Properties)
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
