#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class Scene;
class ScriptEngine;

// Studio plugins: Lua files in the "plugins" folder (next to Studio) that add
// buttons to the PLUGINS tab. A plugin runs once when Studio starts:
//
//   plugin:Button("Paint Red", "Make the selected parts red", function()
//       for _, part in ipairs(Selection:Get()) do
//           part.Color = Color3.new(1, 0, 0)
//       end
//   end)
//
// Inside a plugin you get everything scripts have (workspace, Instance.new,
// Vector3...) plus `plugin` and `Selection` (:Get() and :Set({...})).
// Anything a button changes can be undone with Ctrl+Z.
class Plugins {
public:
    struct Button { std::string label, tip; int ref = -1; };
    struct Plugin {
        std::filesystem::path file;
        std::string name, error;
        std::unique_ptr<ScriptEngine> engine;
        std::vector<Button> buttons;
    };

    explicit Plugins(Scene* scene);
    ~Plugins();
    void reload();                                   // (re)load every plugin file
    void click(size_t plugin, size_t button);
    std::vector<std::unique_ptr<Plugin>>& list() { return m_plugins; }
    static std::filesystem::path folder();

private:
    Scene* m_scene;
    std::vector<std::unique_ptr<Plugin>> m_plugins;
};
