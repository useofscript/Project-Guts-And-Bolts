#pragma once
#include <glm/glm.hpp>
#include <vector>

class Scene;
class SceneNode;

// Ready-made objects for the Toolbox. Most come with a Script inside, so they
// work straight away when you press Play — and double as scripting examples.
enum class Premade {
    SpawnLocation, KillBrick, Spinner, JumpPad, Coin, DisappearingPlatform,
    MovingPlatform, SpeedPad, ClickButton, FallingBall, DayNightCycle, LampPost, DiscoFloor,
    Landmine, SawBlade, SpikeTrap, ExplodingBarrel,
};

struct PremadeInfo {
    Premade     kind;
    const char* name;
    const char* tip;
};

const std::vector<PremadeInfo>& premadeList();
SceneNode* buildPremade(Scene& scene, Premade kind, const glm::vec3& at);

// Code snippets offered by the script editor's "Insert" menu.
struct Snippet {
    const char* name;
    const char* code;
};
const std::vector<Snippet>& snippetList();
