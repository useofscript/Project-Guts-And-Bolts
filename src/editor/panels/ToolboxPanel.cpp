#include "ToolboxPanel.h"

#include <imgui.h>

ToolboxPanel::ToolboxPanel(Actions actions) : m_do(std::move(actions)) {}

void ToolboxPanel::render() {
    ImGui::Begin("Toolbox");

    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float btnW    = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;

    if (ImGui::CollapsingHeader("Parts", ImGuiTreeNodeFlags_DefaultOpen)) {
        struct Item { const char* label; PrimitiveType type; };
        static const Item items[] = {
            { "Cube",     PrimitiveType::Cube     },
            { "Sphere",   PrimitiveType::Sphere   },
            { "Plane",    PrimitiveType::Plane    },
            { "Cylinder", PrimitiveType::Cylinder },
        };
        for (int i = 0; i < IM_ARRAYSIZE(items); ++i) {
            if (ImGui::Button(items[i].label, ImVec2(btnW, 40)) && m_do.spawnPart)
                m_do.spawnPart(items[i].type);
            if (i % 2 == 0) ImGui::SameLine();
        }
    }

    if (ImGui::CollapsingHeader("Objects", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("+ Script", ImVec2(btnW, 40)) && m_do.addScript) m_do.addScript();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Adds a Script inside the selected object\n(or the Workspace if nothing is selected)");
        ImGui::SameLine();
        if (ImGui::Button("+ Model", ImVec2(btnW, 40)) && m_do.addModel) m_do.addModel();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("An empty folder for grouping objects");
    }

    if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Point Light", ImVec2(btnW, 36)) && m_do.addLight) m_do.addLight(LightType::Point);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shines in every direction, like a light bulb.\nGoes inside the selected part.");
        ImGui::SameLine();
        if (ImGui::Button("Spot Light", ImVec2(btnW, 36)) && m_do.addLight) m_do.addLight(LightType::Spot);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shines in a cone (downwards - rotate it to aim).\nGoes inside the selected part.");
    }

    if (ImGui::CollapsingHeader("Ready-made", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("These already have scripts inside.");
        int i = 0;
        for (const PremadeInfo& p : premadeList()) {
            if (ImGui::Button(p.name, ImVec2(btnW, 36)) && m_do.spawnPremade)
                m_do.spawnPremade(p.kind);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.tip);
            if (i++ % 2 == 0) ImGui::SameLine();
        }
        if (i % 2 == 1) ImGui::NewLine();
    }

    ImGui::End();
}
