#pragma once
#include <functional>
#include "../../scene/SceneNode.h"   // PrimitiveType
#include "../Premades.h"

// A palette of insertable things (like Roblox's Toolbox): basic parts, a
// Script / Model, and ready-made game objects that already contain scripts.
class ToolboxPanel {
public:
    struct Actions {
        std::function<void(PrimitiveType)> spawnPart;
        std::function<void()>              addScript;
        std::function<void()>              addModel;
        std::function<void(LightType)>     addLight;
        std::function<void()>              addSound;
        std::function<void(int)>           startConnect;   // ConstraintType (5 = motor)
        std::function<void(Premade)>       spawnPremade;
    };

    explicit ToolboxPanel(Actions actions);
    void render();

private:
    Actions m_do;
};
