#pragma once
#include <functional>
#include "../../scene/SceneNode.h"   // PrimitiveType

// A palette of insertable parts (like Roblox's Toolbox). It doesn't own the
// spawn logic — it calls back into the editor to create the object.
class ToolboxPanel {
public:
    using SpawnFn = std::function<void(PrimitiveType)>;

    explicit ToolboxPanel(SpawnFn spawn);
    void render();

private:
    SpawnFn m_spawn;
};
