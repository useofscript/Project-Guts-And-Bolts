#pragma once
#include <memory>
#include <string>
#include <glm/glm.hpp>

class SceneNode;
struct EditMesh;

// The default character's body parts, from assets/models/player.obj (baked
// into the program). The model is Roblox-sized (a 2x2x1 torso); our
// characters are half that.
namespace PlayerModel {

// The shape for a rig part ("Torso", "Head", "Left Arm"...), or null.
std::shared_ptr<EditMesh> mesh(const std::string& rigPart);
// Where that part sits (feet at 0) and how big it is, in our units.
bool placement(const std::string& rigPart, glm::vec3& position, glm::vec3& size);
// If `m` is one of the built-in body shapes, its rig part name (saves just store that).
const char* nameOf(const EditMesh* m);
// Give a rig part its body shape (the GPU copy is shared by every character).
bool apply(SceneNode& node);

} // namespace PlayerModel
