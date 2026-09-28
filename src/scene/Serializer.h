#pragma once
#include <memory>
#include <string>

class Scene;
class SceneNode;
struct Environment;

// Turns scenes and objects into JSON text and back. Used for:
//  * File > Save / Open (".gbscene" files)
//  * Undo / Redo (a snapshot of the scene before each change)
//  * Play mode (the world is snapshotted on Play and restored on Stop)
//  * Copy / Paste / Duplicate and script Clone()
namespace Serializer {

std::string saveScene(Scene& scene, bool pretty = false);
bool        loadScene(Scene& scene, const std::string& text, std::string* error = nullptr);

std::string                nodeToString(const SceneNode& node);
// freshIds = true gives every object a brand-new id (needed for copies).
std::unique_ptr<SceneNode> nodeFromString(const std::string& text, bool freshIds);
std::unique_ptr<SceneNode> clone(const SceneNode& node);

std::string environmentToString(const Environment& env);
void        environmentFromString(Environment& env, const std::string& text);

bool writeFile(const std::string& path, const std::string& text);
bool readFile (const std::string& path, std::string& out);

} // namespace Serializer
