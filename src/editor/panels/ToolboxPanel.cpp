#include "ToolboxPanel.h"

#include <imgui.h>

ToolboxPanel::ToolboxPanel(SpawnFn spawn) : m_spawn(std::move(spawn)) {}

void ToolboxPanel::render() {
    ImGui::Begin("Toolbox");

    ImGui::TextDisabled("Insert a part");
    ImGui::Spacing();

    struct Item { const char* label; PrimitiveType type; };
    static const Item items[] = {
        { "Cube",     PrimitiveType::Cube     },
        { "Sphere",   PrimitiveType::Sphere   },
        { "Plane",    PrimitiveType::Plane    },
        { "Cylinder", PrimitiveType::Cylinder },
    };

    // Two equal-width columns of chunky buttons.
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float btnW    = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;

    for (int i = 0; i < IM_ARRAYSIZE(items); ++i) {
        if (ImGui::Button(items[i].label, ImVec2(btnW, 46)) && m_spawn)
            m_spawn(items[i].type);
        if (i % 2 == 0) ImGui::SameLine();
    }

    ImGui::End();
}
