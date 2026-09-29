#pragma once
class Camera;
class Player;
class Scene;

// The camera while playing, like Roblox's: it orbits the character's head,
// the mouse wheel zooms, and zooming all the way in goes into first person
// (the camera sits in the head). As the camera comes close, your own character
// fades away so it doesn't block the view; in first person it's invisible
// (except the tool you're holding). Used by the Player app and Studio's Play.
namespace PlayCamera {

constexpr float kMaxZoom = 60.0f;

bool firstPerson(const Camera& cam);
// Mouse wheel (or pinch): closer / further, snapping into and out of first person.
void zoom(Camera& cam, float wheel);
// Move the camera with the character (call every frame while playing).
void follow(Camera& cam, Player& player, float dt);
// Fade the character by how close the camera is.
void fade(Scene& scene, Player& player, const Camera& cam);

} // namespace PlayCamera
