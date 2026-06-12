#pragma once

class Scene;

// Inspector for the currently selected node: edit its name, transform, base
// colour and visibility. Shows a hint when nothing is selected.
class PropertiesPanel {
public:
    explicit PropertiesPanel(Scene* scene);
    void render();

private:
    Scene* m_scene;
};
