#pragma once
#include <imgui.h>
#include <cstdint>
#include <vector>

class Scene;
class SceneNode;

// Game UI made of ScreenGui / Frame / TextLabel / TextButton / ImageLabel /
// ImageButton objects (with UICorner, UIStroke, UIShadow and UIBlur inside them), drawn over the
// 3D view like Roblox's. Used by the Player app, Studio's Play and Studio's
// viewport (so you see the UI while you build it).
namespace GameGui {

// What the pointer is doing with the UI (kept between frames by whoever shows the game).
struct Input {
    uint64_t hovered = 0;   // the button under the pointer
    uint64_t pressed = 0;   // the button the mouse went down on
};

enum class EventKind { Click, Enter, Leave };
struct Event { EventKind kind; uint64_t id; };

// Draw every enabled ScreenGui in the scene over [min, max]. Also works out each
// object's AbsolutePosition / AbsoluteSize. `selected` gets a highlight (Studio).
void draw(ImDrawList* dl, ImVec2 min, ImVec2 max, Scene& scene, const Input* input = nullptr, uint64_t selected = 0);

// Pointer input. `down` = the button went down this frame, `up` = it came up,
// `tapped` = a finger tapped (touch screens). Returns true when the pointer is
// over the UI, so the click shouldn't also reach the 3D world.
bool handle(Scene& scene, ImVec2 min, ImVec2 max, ImVec2 pointer, bool inside, bool down, bool up, bool tapped,
            Input& input, std::vector<Event>& events);

// The topmost UI object under `p` (Studio picks these like parts), or null.
SceneNode* pick(Scene& scene, ImVec2 min, ImVec2 max, ImVec2 p, bool buttonsOnly = false);

// Work out every object's AbsolutePosition / AbsoluteSize now, on the screen last
// drawn to (scripts ask right after making things, before the next frame).
void refresh(Scene& scene);

// UIBlur: does anything on screen blur the world behind it? If so, whoever shows the
// game makes SceneRenderer::makeBackdrop() after drawing the world and hands the
// blurred pictures (a little blurry first, very blurry last) to setBackdrop() before
// draw(). Without them UIBlur draws nothing.
bool needsBackdrop(Scene& scene);
void setBackdrop(const unsigned* levels, int count);

// Its rectangle on screen as last drawn (min, max). False if it isn't on screen.
bool rectOf(Scene& scene, ImVec2 min, ImVec2 max, const SceneNode* node, ImVec2& a, ImVec2& b);

} // namespace GameGui
