#include "PropertiesPanel.h"
#include "../../scene/Scene.h"
#include "../../scene/SceneNode.h"
#include "../../renderer/MeshLibrary.h"
#include "../../core/Audio.h"
#include "../../core/FileDialog.h"
#include "../../core/Paths.h"
#include "../../renderer/Textures.h"
#include "../../scene/EditMesh.h"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <misc/cpp/imgui_stdlib.h>

PropertiesPanel::PropertiesPanel(Scene* scene, std::function<void(SceneNode*)> openScript)
    : m_scene(scene), m_openScript(std::move(openScript)) {}

void PropertiesPanel::render() {
    ImGui::Begin("Properties");

    SceneNode* node = m_scene->selected();
    if (!node) {
        ImGui::TextDisabled("No object selected.");
        ImGui::TextDisabled("Pick one in the Explorer or Viewport.");
        ImGui::End();
        return;
    }

    const char* cls = node == m_scene->root()          ? "Workspace"
                    : node->kind == NodeKind::Script   ? "Script"
                    : node->kind == NodeKind::Light    ? (node->lightType == LightType::Spot ? "SpotLight" : "PointLight")
                    : node->kind == NodeKind::Sound    ? "Sound"
                    : node->kind == NodeKind::Attachment ? "Attachment"
                    : node->kind == NodeKind::Constraint ? "Constraint"
                    : node->kind == NodeKind::ForceField ? "ForceField"
                    : node->kind == NodeKind::Tool       ? "Tool"
                    : node->kind == NodeKind::Value      ? node->valueClass()
                    : node->kind == NodeKind::Decal      ? "Decal"
                    : node->kind == NodeKind::Animation  ? "Animation"
                    : node->kind == NodeKind::FluidSystem  ? "FluidSystem"
                    : node->kind == NodeKind::FluidEmitter ? "FluidEmitter"
                    : node->kind == NodeKind::Gui        ? kGuiClassNames[(int)node->gui.type]
                    : node->kind == NodeKind::Model    ? "Model" : "Part";
    ImGui::TextDisabled("%s", cls);

    renderProperties(node);
    if (node != m_scene->root()) renderAttributes(node);
    ImGui::End();
}

// Game UI objects: sizes and places as UDim2 (a fraction of the parent + pixels).
namespace {
bool editUDim2(const char* label, UDim2& u, float scaleStep = 0.005f) {
    bool changed = false;
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
    changed |= ImGui::DragFloat2("X (scale, pixels)", &u.xs, 1.0f, 0, 0, "%.3f");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
    changed |= ImGui::DragFloat2("Y (scale, pixels)", &u.ys, 1.0f, 0, 0, "%.3f");
    (void)scaleStep;
    ImGui::PopID();
    return changed;
}
} // namespace

void PropertiesPanel::renderGui(SceneNode* node) {
    GuiProps& g = node->gui;
    if (g.type != GuiType::UICorner)
        ImGui::Checkbox(g.type == GuiType::ScreenGui || g.type == GuiType::UIStroke ? "Enabled" : "Visible",
                        g.type == GuiType::ScreenGui || g.type == GuiType::UIStroke ? &node->enabled : &node->visible);
    if (g.type == GuiType::ScreenGui) {
        ImGui::InputInt("DisplayOrder", &g.displayOrder);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("ScreenGuis with a higher number are drawn on top.");
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("A ScreenGui is a layer of UI over the game. Insert Frames, TextLabels, TextButtons and "
                            "ImageLabels inside it. Click them in the viewport to pick them, drag to move, drag the "
                            "blue corner to resize.");
        ImGui::PopTextWrapPos();
        return;
    }
    if (g.type == GuiType::UICorner) {
        ImGui::SeparatorText("UICorner");
        ImGui::DragFloat2("CornerRadius (scale, pixels)", &g.corner.xs, 0.5f, 0.0f, 500.0f, "%.2f");
        ImGui::TextDisabled("Rounds the corners of the object it's in.");
        return;
    }
    if (g.type == GuiType::UIStroke) {
        ImGui::SeparatorText("UIStroke");
        ImGui::ColorEdit3("Color", &g.borderColor.x);
        ImGui::DragFloat("Thickness", &g.thickness, 0.1f, 0.0f, 50.0f, "%.1f");
        ImGui::SliderFloat("Transparency", &g.bgTransparency, 0.0f, 1.0f);
        ImGui::TextDisabled("An outline around the object it's in.");
        return;
    }

    ImGui::SeparatorText("Layout");
    editUDim2("Position", g.pos);
    editUDim2("Size", g.size);
    ImGui::DragFloat2("AnchorPoint", &g.anchor.x, 0.01f, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Which point of it sits at Position: 0,0 = top-left, 0.5,0.5 = middle.");
    ImGui::InputInt("ZIndex", &g.zIndex);
    ImGui::Checkbox("ClipsDescendants", &g.clips);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hide the parts of things inside it that stick out.");
    ImGui::TextDisabled("Scale 1 = the whole of the parent; pixels are added on top.");

    ImGui::SeparatorText("Background");
    ImGui::ColorEdit3("BackgroundColor3", &g.bg.x);
    ImGui::SliderFloat("BackgroundTransparency", &g.bgTransparency, 0.0f, 1.0f);
    ImGui::ColorEdit3("BorderColor3", &g.borderColor.x);
    ImGui::SliderInt("BorderSizePixel", &g.border, 0, 10);

    if (g.type == GuiType::TextLabel || g.type == GuiType::TextButton) {
        ImGui::SeparatorText("Text");
        ImGui::InputTextMultiline("Text", &g.text, ImVec2(-1, ImGui::GetTextLineHeight() * 3));
        ImGui::ColorEdit3("TextColor3", &g.textColor.x);
        ImGui::DragFloat("TextSize", &g.textSize, 0.5f, 4.0f, 200.0f, "%.0f");
        ImGui::Checkbox("TextScaled", &g.textScaled);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Make the text as big as fits in the box.");
        ImGui::SameLine();
        ImGui::Checkbox("TextWrapped", &g.textWrapped);
        ImGui::SameLine();
        ImGui::Checkbox("Bold", &g.bold);
        const char* xs[] = {"Left", "Center", "Right"};
        const char* ys[] = {"Top", "Center", "Bottom"};
        ImGui::Combo("TextXAlignment", &g.xAlign, xs, 3);
        ImGui::Combo("TextYAlignment", &g.yAlign, ys, 3);
        ImGui::SliderFloat("TextTransparency", &g.textTransparency, 0.0f, 1.0f);
        ImGui::ColorEdit3("TextStrokeColor3", &g.strokeColor.x);
        ImGui::SliderFloat("TextStrokeTransparency", &g.strokeTransparency, 0.0f, 1.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = a solid outline around the letters, 1 = none.");
    }
    if (g.type == GuiType::ImageLabel || g.type == GuiType::ImageButton) {
        ImGui::SeparatorText("Image");
        ImGui::InputText("Image", &g.image);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("A picture: a .png / .jpg in the games folder, a full path,\nor gb:<id> for one uploaded on the Create page.");
        if (FileDialog::available()) {
            ImGui::SameLine();
            if (ImGui::Button("Browse...")) {
                std::string path = FileDialog::openImage("Pick a picture");
                if (!path.empty()) { g.image = Paths::relativeToGames(path); m_scene->markDirty(); }
            }
        }
        ImGui::ColorEdit3("ImageColor3", &g.imageColor.x);
        ImGui::SliderFloat("ImageTransparency", &g.imageTransparency, 0.0f, 1.0f);
    }
    if (node->isGuiButton()) {
        ImGui::SeparatorText("Button");
        ImGui::Checkbox("AutoButtonColor", &g.autoButtonColor);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Get darker when the mouse is over it or presses it.");
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("In a script: button.MouseButton1Click:Connect(function() ... end)");
        ImGui::PopTextWrapPos();
    }
}

void PropertiesPanel::renderProperties(SceneNode* node) {
    // --- Name ---
    ImGui::BeginDisabled(m_scene->isProtected(node));
    ImGui::InputText("Name", &node->name);
    ImGui::EndDisabled();

    // --- Scripts: just the code ---
    if (node->isScript()) {
        ImGui::Checkbox("Enabled", &node->enabled);
        ImGui::Spacing();
        if (ImGui::Button("Edit Script", ImVec2(-1, 36)) && m_openScript) m_openScript(node);
        ImGui::Spacing();
        ImGui::TextDisabled("Runs when you press Play.");
        ImGui::TextDisabled("Inside the code, 'script.Parent' is the");
        ImGui::TextDisabled("object this script is inside of.");
        return;
    }

    if (node->isValue()) {
        ImGui::SeparatorText("Value");
        switch (node->value.type) {
            case Attribute::Bool:   ImGui::Checkbox("Value", &node->value.b); break;
            case Attribute::String: ImGui::InputText("Value", &node->value.s); break;
            case Attribute::Vector3: ImGui::DragFloat3("Value", &node->value.v.x, 0.1f); break;
            case Attribute::Color3: ImGui::ColorEdit3("Value", &node->value.v.x); break;
            default:
                if (node->intValue) {
                    long long v = (long long)node->value.n;
                    if (ImGui::InputScalar("Value", ImGuiDataType_S64, &v)) node->value.n = (double)v;
                } else {
                    ImGui::InputDouble("Value", &node->value.n);
                }
        }
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Scripts read and change it with .Value (and hear about changes with .Changed). "
                            "Put IntValues in a folder called leaderstats inside a player to show them on the leaderboard.");
        ImGui::PopTextWrapPos();
        return;
    }

    if (node->isGui()) { renderGui(node); return; }

    if (node->isDecal()) {
        ImGui::SeparatorText("Decal");
        ImGui::InputText("Texture", &node->texture);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("A picture: a .png / .jpg in the games folder, a full path,\nor gb:<id> for one uploaded on the Create page.");
        if (FileDialog::available()) {
            ImGui::SameLine();
            if (ImGui::Button("Browse...")) {
                std::string path = FileDialog::openImage("Pick a picture for the decal");
                if (!path.empty()) { node->texture = Paths::relativeToGames(path); m_scene->markDirty(); }
            }
        }
        int face = (int)node->face;
        if (ImGui::Combo("Face", &face, kFaceNames, 6)) node->face = (Face)face;
        ImGui::ColorEdit3("Color3", &node->color.x);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tints the picture. White shows it as it is.");
        ImGui::SliderFloat("Transparency", &node->transparency, 0.0f, 1.0f);
        if (unsigned tex = Textures::get(node->texture)) {
            int w = 0, h = 0;
            Textures::size(node->texture, w, h);
            float pw = std::min(ImGui::GetContentRegionAvail().x, 160.0f);
            float ph = w > 0 ? pw * (float)h / (float)w : pw;
            ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(pw, ph), ImVec2(0, 1), ImVec2(1, 0));
        } else if (!node->texture.empty()) {
            ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.3f, 1), "Can't find that picture yet.");
        }
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Put a Decal inside a part: the picture is stuck on the side you pick in Face, "
                            "stretched to fill it.");
        ImGui::PopTextWrapPos();
        return;
    }

    if (node->isTool()) {
        ImGui::SeparatorText("Tool");
        ImGui::Checkbox("Enabled", &node->enabled);
        ImGui::InputText("ToolTip", &node->toolTip);
        ImGui::Checkbox("CanBeDropped", &node->canBeDropped);
        ImGui::Checkbox("In StarterPack", &node->starterTool);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Everyone gets this tool in their backpack when they spawn.");
        ImGui::DragFloat3("GripPos", &node->gripPos.x, 0.02f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Where on the Handle the hand holds it (in the Handle's own space).");
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Put a part called Handle inside: that's what the character holds (its long side, Y, "
                            "points forward out of the hand). Players pick tools up by touching them, and press "
                            "1-9 to equip. Scripts inside get tool.Activated when the player clicks.");
        ImGui::PopTextWrapPos();
        return;
    }

    // --- Real liquid ---
    if (node->kind == NodeKind::FluidSystem) {
        ImGui::ColorEdit3("Color", &node->color.x);
        ImGui::SliderFloat("Viscosity", &node->viscosity, 0.0f, 1.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("How thick it is: 0 runs like water, 1 oozes like honey.");
        ImGui::SliderFloat("SurfaceTension", &node->surfaceTension, 0.0f, 1.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("How much it sticks to itself: higher makes beads and strands.");
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("A kind of liquid. FluidEmitters pour it out (set their FluidSystem to this).");
        ImGui::PopTextWrapPos();
        return;
    }
    if (node->kind == NodeKind::FluidEmitter) {
        ImGui::Checkbox("Enabled", &node->enabled);
        ImGui::DragFloat3("Position", &node->transform.position.x, 0.1f);
        ImGui::DragFloat3("Size", &node->transform.scale.x, 0.05f, 0.1f, 100.0f);
        ImGui::DragFloat("Rate", &node->fluidRate, 5.0f, 0.0f, 100000.0f, "%.0f drops/s");
        ImGui::DragFloat3("Velocity", &node->fluidVelocity.x, 0.1f);
        std::string current = "Water (default)";
        std::vector<SceneNode*> systems;
        m_scene->forEach([&](SceneNode* n) { if (n->kind == NodeKind::FluidSystem) systems.push_back(n); });
        for (SceneNode* sys : systems) if (sys->id == node->fluidSystem) current = sys->name;
        if (ImGui::BeginCombo("FluidSystem", current.c_str())) {
            if (ImGui::Selectable("Water (default)", node->fluidSystem == 0)) node->fluidSystem = 0;
            for (SceneNode* sys : systems) {
                ImGui::PushID((int)sys->id);
                if (ImGui::Selectable(sys->name.c_str(), node->fluidSystem == sys->id)) node->fluidSystem = sys->id;
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Pours real liquid while the game runs: Rate drops a second, out of a box "
                            "Size big, moving at Velocity.");
        ImGui::PopTextWrapPos();
        return;
    }

    if (node->isSound()) {
        if (ImGui::BeginCombo("Sound", node->soundId.c_str())) {
            for (const auto& name : Audio::builtinNames())
                if (ImGui::Selectable(name.c_str(), node->soundId == name)) node->soundId = name;
            ImGui::EndCombo();
        }
        ImGui::InputText("File", &node->soundId);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("A built-in name, or a .wav / .mp3 / .flac file in the games folder\n(e.g. music/theme.mp3)");
        ImGui::SliderFloat("Volume", &node->volume, 0.0f, 2.0f);
        ImGui::SliderFloat("Pitch", &node->pitch, 0.25f, 3.0f);
        ImGui::Checkbox("Looped", &node->looped);
        ImGui::SameLine();
        ImGui::Checkbox("Autoplay", &node->autoplay);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Starts playing when the game starts (great for music)");
        static int preview = 0;
        if (ImGui::Button(Audio::isPlaying(preview) ? "Stop preview" : "Preview")) {
            if (Audio::isPlaying(preview)) Audio::stop(preview);
            else preview = Audio::play(node->soundId, node->volume, node->pitch, false);
        }
        ImGui::TextDisabled("Scripts: script.Parent:Play()  /  Sounds.Play(\"coin\")");
        return;
    }

    if (node->isAttachment()) {
        ImGui::DragFloat3("Offset", &node->transform.position.x, 0.01f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Where on the part (in the part's own space)");
        ImGui::DragFloat3("Rotation", &node->transform.rotation.x, 0.5f);
        ImGui::TextDisabled("Its red (X) direction is the hinge axis.");
        return;
    }
    if (node->isConstraint()) {
        int t = (int)node->constraintType;
        if (ImGui::Combo("Type", &t, kConstraintNames, 5)) { node->constraintType = (ConstraintType)t; node->jointReady = false; }
        ImGui::Checkbox("Enabled", &node->enabled);
        ImGui::SameLine();
        ImGui::Checkbox("Visible", &node->visible);
        SceneNode* r0 = m_scene->findById(node->ref0);
        SceneNode* r1 = m_scene->findById(node->ref1);
        auto owner = [](SceneNode* r) { return r ? (r->isAttachment() && r->parent ? r->parent->name : r->name) : std::string("(missing)"); };
        ImGui::TextDisabled("Connects %s  <->  %s", owner(r0).c_str(), owner(r1).c_str());
        ConstraintType ct = node->constraintType;
        if (ct == ConstraintType::Rope || ct == ConstraintType::Rod || ct == ConstraintType::Spring) {
            bool autoLen = node->length < 0.0f;
            if (ImGui::Checkbox("Length as placed", &autoLen)) node->length = autoLen ? -1.0f : 4.0f;
            if (!autoLen) ImGui::DragFloat("Length", &node->length, 0.05f, 0.0f, 500.0f);
        }
        if (ct == ConstraintType::Spring) {
            ImGui::DragFloat("Stiffness", &node->stiffness, 1.0f, 0.0f, 100000.0f);
            ImGui::DragFloat("Damping", &node->damping, 0.1f, 0.0f, 10000.0f);
        }
        if (ct == ConstraintType::Hinge) {
            bool motor = node->motorTorque > 0.0f;
            if (ImGui::Checkbox("Motor", &motor)) node->motorTorque = motor ? 3000.0f : 0.0f;
            if (motor) {
                ImGui::DragFloat("Speed", &node->motorSpeed, 0.05f, -100.0f, 100.0f, "%.2f rad/s");
                ImGui::DragFloat("Strength", &node->motorTorque, 10.0f, 0.0f, 1e7f);
            }
        }
        if (ct != ConstraintType::Weld) {
            ImGui::ColorEdit3("Color", &node->color.x);
            ImGui::SliderFloat("Thickness", &node->thickness, 0.02f, 1.0f);
        }
        return;
    }

    if (node->isLight()) {
        ImGui::Checkbox("Enabled", &node->enabled);
        int t = (int)node->lightType;
        const char* types[] = {"Point", "Spot"};
        if (ImGui::Combo("Type", &t, types, 2)) node->lightType = (LightType)t;
        ImGui::ColorEdit3("Color", &node->color.x);
        ImGui::SliderFloat("Brightness", &node->brightness, 0.0f, 20.0f);
        ImGui::SliderFloat("Range", &node->range, 1.0f, 100.0f);
        if (node->lightType == LightType::Spot) {
            ImGui::SliderFloat("Angle", &node->spotAngle, 5.0f, 170.0f, "%.0f\xc2\xb0");
            ImGui::DragFloat3("Rotation", &node->transform.rotation.x, 0.5f);
            ImGui::TextDisabled("Spot lights point down; rotate to aim.");
        }
        ImGui::DragFloat3("Offset", &node->transform.position.x, 0.05f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Position relative to the part it's inside");
        return;
    }

    if (node == m_scene->root()) {
        ImGui::Separator();
        ImGui::TextDisabled("The Workspace holds everything in your game.");
        ImGui::DragFloat("Gravity", &m_scene->world().gravity, 0.1f, 0.0f, 200.0f);
        return;
    }

    ImGui::Checkbox("Visible", &node->visible);
    ImGui::Separator();

    // --- Transform ---
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position", &node->transform.position.x, 0.05f);
        ImGui::DragFloat3("Rotation", &node->transform.rotation.x, 0.5f);
        ImGui::DragFloat3("Size",     &node->transform.scale.x,    0.05f, 0.001f, 1000.0f);

        if (ImGui::Button("Reset Transform")) {
            node->transform.position = {0, 0, 0};
            node->transform.rotation = {0, 0, 0};
            node->transform.scale    = {1, 1, 1};
        }
    }

    if (node->kind != NodeKind::Part) {
        return;
    }

    // --- Appearance ---
    if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Color", &node->color.x);

        if (node->primitiveType == PrimitiveType::Mesh && node->editMesh) {
            // A MeshPart: its shape was built in Modeling mode.
            ImGui::TextDisabled("Custom mesh: %d corners, %d faces", (int)node->editMesh->verts.size(),
                                (int)node->editMesh->faces.size());
            bool smooth = node->editMesh->smooth;
            if (ImGui::Checkbox("Smooth shading", &smooth)) { MeshEdit::own(*node).smooth = smooth; MeshEdit::refresh(*node); }
            if (ImGui::Button("Edit Mesh (Tab)", ImVec2(-1, 0)) && m_editMesh) m_editMesh(node);
        } else {
            const char* shapes[] = { "Cube", "Sphere", "Plane", "Cylinder" };
            int shape = (int)node->primitiveType - 1;
            if (ImGui::Combo("Shape", &shape, shapes, IM_ARRAYSIZE(shapes))) {
                node->primitiveType = (PrimitiveType)(shape + 1);
                node->mesh = MeshLibrary::get(node->primitiveType);
            }
            if (ImGui::Button("Reshape in Modeling mode (Tab)", ImVec2(-1, 0)) && m_editMesh) m_editMesh(node);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Turns this part into a MeshPart you can reshape");
        }

        int mat = (int)node->material;
        if (ImGui::Combo("Material", &mat, kMaterialNames, kMaterialCount))
            node->material = (Material)mat;

        ImGui::SliderFloat("Transparency", &node->transparency, 0.0f, 1.0f);
    }

    // --- Behavior ---
    if (ImGui::CollapsingHeader("Behavior", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Anchored",    &node->anchored);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Unanchored parts fall with gravity in Play mode");
        ImGui::Checkbox("Can Collide", &node->canCollide);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Turn off to let things pass through");
        ImGui::Checkbox("Cast Shadow", &node->castShadow);
    }

    // --- Physics (how it behaves when unanchored) ---
    if (ImGui::CollapsingHeader("Physics")) {
        auto prop = [&](const char* label, float& v, float def, float mn, float mx, const char* tip) {
            bool custom = v >= 0.0f;
            ImGui::PushID(label);
            if (ImGui::Checkbox("##custom", &custom)) v = custom ? def : -1.0f;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tick to override the material's value");
            ImGui::SameLine();
            ImGui::BeginDisabled(!custom);
            float shown = custom ? v : def;
            if (ImGui::SliderFloat(label, &shown, mn, mx) && custom) v = shown;
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
            ImGui::PopID();
        };
        prop("Density", node->density, 1.0f, 0.05f, 10.0f, "Heavier parts are harder to push");
        prop("Friction", node->friction, 0.5f, 0.0f, 2.0f, "0 = slides like ice");
        prop("Bounciness", node->elasticity, 0.3f, 0.0f, 1.0f, "1 = bounces like a rubber ball");
    }

    if (!m_scene->isCharacterPart(node)) {
        ImGui::Spacing();
        if (ImGui::Button("Add Script inside", ImVec2(-1, 0))) {
            auto s = std::make_unique<SceneNode>("Script", NodeKind::Script);
            s->source = "local part = script.Parent\n\nprint(\"Hello from \" .. part.Name)\n";
            SceneNode* raw = m_scene->insert(std::move(s), node);
            m_scene->select(raw);
            if (m_openScript) m_openScript(raw);
        }
    }
}

// Roblox-style Attributes (your own named values) and Tags, for any object.
void PropertiesPanel::renderAttributes(SceneNode* node) {
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Attributes", ImGuiTreeNodeFlags_DefaultOpen)) {
        int remove = -1;
        for (size_t i = 0; i < node->attributes.size(); ++i) {
            Attribute& a = node->attributes[i];
            ImGui::PushID((int)i);
            if (ImGui::SmallButton("x")) remove = (int)i;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete this attribute");
            ImGui::SameLine();
            switch (a.type) {
                case Attribute::Bool:    ImGui::Checkbox(a.name.c_str(), &a.b); break;
                case Attribute::Number:  ImGui::InputDouble(a.name.c_str(), &a.n, 1.0, 10.0, "%g"); break;
                case Attribute::String:  ImGui::InputText(a.name.c_str(), &a.s); break;
                case Attribute::Vector3: ImGui::DragFloat3(a.name.c_str(), &a.v.x, 0.05f); break;
                case Attribute::Color3:  ImGui::ColorEdit3(a.name.c_str(), &a.v.x); break;
            }
            ImGui::PopID();
        }
        if (remove >= 0) node->attributes.erase(node->attributes.begin() + remove);
        if (node->attributes.empty()) ImGui::TextDisabled("Scripts read these with obj:GetAttribute(\"Name\").");

        if (ImGui::Button("+ Add Attribute", ImVec2(-1, 0))) {
            m_newAttrName.clear();
            m_newAttrError.clear();
            ImGui::OpenPopup("Add Attribute");
        }
        if (ImGui::BeginPopup("Add Attribute")) {
            static const char* kTypes[] = {"Boolean", "Number", "String", "Vector3", "Color3"};
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            bool enter = ImGui::InputTextWithHint("Name", "e.g. Damage", &m_newAttrName,
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::Combo("Type", &m_newAttrType, kTypes, 5);
            if (!m_newAttrError.empty()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", m_newAttrError.c_str());
            if (ImGui::Button("Add") || enter) {
                bool ok = !m_newAttrName.empty() && m_newAttrName.size() <= 100 && m_newAttrName.rfind("RBX", 0) != 0;
                for (char c : m_newAttrName) if (!std::isalnum((unsigned char)c) && c != '_') ok = false;
                if (!ok) m_newAttrError = "Use letters, numbers and _ only.";
                else if (node->findAttribute(m_newAttrName)) m_newAttrError = "There's already one called that.";
                else {
                    Attribute a;
                    a.name = m_newAttrName;
                    a.type = (Attribute::Type)m_newAttrType;
                    if (a.type == Attribute::Color3) a.v = glm::vec3(1.0f);
                    node->attributes.push_back(a);
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    if (ImGui::CollapsingHeader("Tags", ImGuiTreeNodeFlags_DefaultOpen)) {
        int remove = -1;
        float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        for (size_t i = 0; i < node->tags.size(); ++i) {
            ImGui::PushID((int)i);
            std::string chip = node->tags[i] + "  x";
            float w = ImGui::CalcTextSize(chip.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
            if (i > 0) {
                ImGui::SameLine();
                if (ImGui::GetCursorScreenPos().x + w > right) ImGui::NewLine();
            }
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.44f, 0.72f, 1.0f));
            if (ImGui::SmallButton(chip.c_str())) remove = (int)i;
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to remove this tag");
            ImGui::PopID();
        }
        if (remove >= 0) node->tags.erase(node->tags.begin() + remove);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##newtag", "+ Add tag (press Enter)", &m_newTag, ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (!m_newTag.empty() && std::find(node->tags.begin(), node->tags.end(), m_newTag) == node->tags.end())
                node->tags.push_back(m_newTag);
            m_newTag.clear();
            ImGui::SetKeyboardFocusHere(-1);
        }
        if (node->tags.empty()) ImGui::TextDisabled("Find tagged things with CollectionService:GetTagged(\"Tag\").");
    }
}
