#include "Premades.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../renderer/MeshLibrary.h"

#include <memory>
#include <string>

namespace {

// ---------------------------------------------------------------------------
// Scripts that ship inside the premade objects. They're written to be read!
// ---------------------------------------------------------------------------

const char* kKillBrick = R"(-- Kill Brick: touching this part knocks out the player.
local part = script.Parent

part.Touched:Connect(function(hit)
    -- 'hit' is the body part that touched us (like "Left Leg").
    -- Its Parent is the character, which has a Humanoid.
    local humanoid = hit.Parent:FindFirstChild("Humanoid")
    if humanoid then
        humanoid.Health = 0
    end
end)
)";

const char* kSpinner = R"(-- Spinner: turns around and around, forever.
local part = script.Parent
local speed = 90   -- degrees per second (try a negative number!)

-- Heartbeat runs this function every frame. 'dt' is the time since the last one.
game:GetService("RunService").Heartbeat:Connect(function(dt)
    part.Orientation = part.Orientation + Vector3.new(0, speed * dt, 0)
end)
)";

const char* kJumpPad = R"(-- Jump Pad: launches the player high into the air.
local pad = script.Parent
local launchSpeed = 20   -- bigger number = higher jump

pad.Touched:Connect(function(hit)
    local character = hit.Parent
    local root = character:FindFirstChild("HumanoidRootPart")
    if root and character:FindFirstChild("Humanoid") then
        root.AssemblyLinearVelocity = Vector3.new(0, launchSpeed, 0)
    end
end)
)";

const char* kCoin = R"(-- Coin: spins, and disappears when you collect it.
-- _G is shared by every script, so all the coins add to the same counter.
local coin = script.Parent
_G.coins = _G.coins or 0
Gui.Label("Coins", "Coins: " .. _G.coins)

local collected = false

coin.Touched:Connect(function(hit)
    if collected then return end
    if hit.Parent:FindFirstChild("Humanoid") then
        collected = true
        _G.coins = _G.coins + 1
        Gui.Label("Coins", "Coins: " .. _G.coins)
        coin:Destroy()
    end
end)

-- Spin until we get collected (a destroyed coin has no Parent).
while coin.Parent do
    coin.Orientation = coin.Orientation + Vector3.new(0, 3, 0)
    wait()
end
)";

const char* kDisappearing = R"(-- Disappearing Platform: fades away when stepped on, then comes back.
local part = script.Parent
local busy = false   -- a "debounce" so it only runs once at a time

part.Touched:Connect(function(hit)
    if busy or not hit.Parent:FindFirstChild("Humanoid") then return end
    busy = true

    for i = 1, 10 do
        part.Transparency = i / 10
        wait(0.08)
    end
    part.CanCollide = false   -- now you fall through!

    wait(3)
    part.CanCollide = true
    part.Transparency = 0
    busy = false
end)
)";

const char* kMoving = R"(-- Moving Platform: slides back and forth. Stand on it to ride along!
local part = script.Parent
local start = part.Position
local distance = 8     -- how far it travels
local speed = 0.4      -- how fast

game:GetService("RunService").Heartbeat:Connect(function()
    local offset = math.sin(time() * speed * math.pi) * distance / 2
    part.Position = start + Vector3.new(offset, 0, 0)
end)
)";

const char* kSpeedPad = R"(-- Speed Pad: makes the player run fast for a few seconds.
local pad = script.Parent
local boosted = false

pad.Touched:Connect(function(hit)
    local humanoid = hit.Parent:FindFirstChild("Humanoid")
    if humanoid and not boosted then
        boosted = true
        local normal = humanoid.WalkSpeed
        humanoid.WalkSpeed = normal * 2.5
        Gui.Message("Speed boost!", 2)
        wait(5)
        humanoid.WalkSpeed = normal
        boosted = false
    end
end)
)";

const char* kClickButton = R"(-- Click Button: click it with the mouse while playing.
local button = script.Parent
local presses = 0

button.Clicked:Connect(function()
    presses = presses + 1
    button.Color = Color3.fromHSV(math.random(), 0.8, 1)
    Gui.Message("Button pressed " .. presses .. " times!", 2)
end)
)";

const char* kDayNight = R"(-- Day / Night Cycle: makes time pass. A full day takes about a minute.
local Lighting = game:GetService("Lighting")
Lighting.ClockTime = 7   -- 7 in the morning

while true do
    Lighting.ClockTime = Lighting.ClockTime + 0.02
    wait(0.05)
end
)";

const char* kDisco = R"(-- Disco Floor: every tile (and the light) changes colour to the beat.
local floor = script.Parent

while true do
    for _, tile in ipairs(floor:GetChildren()) do
        if tile:IsA("BasePart") then
            tile.Color = Color3.fromHSV(math.random(), 0.9, 1)
        end
    end
    floor.PartyLight.Color = Color3.fromHSV(math.random(), 0.8, 1)
    wait(0.4)
end
)";

const char* kLandmine = R"(-- Landmine: explodes when the player steps on it.
local mine = script.Parent
local armed = true

mine.Touched:Connect(function(hit)
    if armed and hit.Parent:FindFirstChild("Humanoid") then
        armed = false
        Explode(mine.Position, 8)    -- position, radius
        mine:Destroy()
    end
end)

-- Blink the light so you can (just about) see it.
while mine.Parent do
    mine.Color = Color3.fromRGB(255, 40, 40)
    wait(0.5)
    mine.Color = Color3.fromRGB(60, 10, 10)
    wait(0.5)
end
)";

const char* kSawBlade = R"(-- Saw Blade: spins fast and slices whoever touches it.
local saw = script.Parent

game:GetService("RunService").Heartbeat:Connect(function(dt)
    saw.Orientation = saw.Orientation + Vector3.new(0, 0, 720 * dt)
end)

saw.Touched:Connect(function(hit)
    local character = hit.Parent
    if character:FindFirstChild("Humanoid") then
        Effects.Sparks(saw.Position, 30)
        character:BreakJoints()      -- a very messy death
    end
end)
)";

const char* kSpikeTrap = R"(-- Spike Trap: the spikes shoot up every few seconds.
local trap = script.Parent
local spikes = trap:FindFirstChild("Spikes")
local down = spikes.Position
local up = down + Vector3.new(0, 1, 0)

spikes.Touched:Connect(function(hit)
    local humanoid = hit.Parent:FindFirstChild("Humanoid")
    if humanoid and spikes.Position.Y > down.Y + 0.5 then
        humanoid:TakeDamage(45)
    end
end)

while true do
    wait(2)
    spikes.Position = up      -- stab!
    wait(0.6)
    spikes.Position = down
end
)";

const char* kBarrel = R"(-- Exploding Barrel: click it, or knock it over, and BOOM.
local barrel = script.Parent
local done = false

local function boom()
    if done then return end
    done = true
    Explode(barrel.Position, 10, 1.5)
    barrel:Destroy()
end

barrel.Clicked:Connect(boom)
barrel.Touched:Connect(function(hit)
    -- Blow up if something slams into it fast.
    if hit.AssemblyLinearVelocity.Magnitude > 12 then boom() end
end)
)";

SceneNode* addPart(Scene& scene, const char* name, PrimitiveType shape, glm::vec3 pos,
                   glm::vec3 size, glm::vec3 color, Material mat = Material::Plastic) {
    SceneNode* n = scene.addNode(name, shape, MeshLibrary::get(shape));
    n->transform.position = pos;
    n->transform.scale    = size;
    n->color              = color;
    n->material           = mat;
    return n;
}

void addScript(Scene& scene, SceneNode* parent, const char* source) {
    auto s = std::make_unique<SceneNode>("Script", NodeKind::Script);
    s->source = source;
    scene.insert(std::move(s), parent);
}

} // namespace

const std::vector<PremadeInfo>& premadeList() {
    static const std::vector<PremadeInfo> list = {
        {Premade::SpawnLocation,        "Spawn Location",  "Where the player appears when you press Play"},
        {Premade::KillBrick,            "Kill Brick",      "Red neon block - touching it knocks the player out"},
        {Premade::Spinner,              "Spinner",         "A bar that spins around forever"},
        {Premade::JumpPad,              "Jump Pad",        "Launches the player into the air"},
        {Premade::Coin,                 "Coin",            "Spins, and counts up when collected"},
        {Premade::DisappearingPlatform, "Fading Platform", "Fades away when you stand on it"},
        {Premade::MovingPlatform,       "Moving Platform", "Slides back and forth, carrying the player"},
        {Premade::SpeedPad,             "Speed Pad",       "Makes the player run faster for 5 seconds"},
        {Premade::ClickButton,          "Click Button",    "Click it with the mouse during Play"},
        {Premade::FallingBall,          "Falling Ball",    "An unanchored ball that drops with gravity"},
        {Premade::DayNightCycle,        "Day/Night Cycle", "A script that makes time pass"},
        {Premade::Landmine,             "Landmine",        "Explodes when stepped on"},
        {Premade::SawBlade,             "Saw Blade",       "A spinning blade. Touch it and lose limbs"},
        {Premade::SpikeTrap,            "Spike Trap",      "Spikes shoot up every few seconds"},
        {Premade::ExplodingBarrel,      "Exploding Barrel","Click it (or bump it) to blow it up"},
        {Premade::LampPost,             "Lamp Post",       "A street lamp with a real light (try it at night)"},
        {Premade::DiscoFloor,           "Disco Floor",     "Tiles and a light that change colour"},
    };
    return list;
}

SceneNode* buildPremade(Scene& scene, Premade kind, const glm::vec3& at) {
    const auto Cube = PrimitiveType::Cube;
    SceneNode* n = nullptr;
    switch (kind) {
        case Premade::SpawnLocation:
            n = addPart(scene, "SpawnLocation", Cube, at + glm::vec3(0, 0.1f, 0), {3, 0.2f, 3}, {0.55f, 0.57f, 0.60f});
            break;
        case Premade::KillBrick:
            n = addPart(scene, "KillBrick", Cube, at + glm::vec3(0, 0.25f, 0), {3, 0.5f, 3}, {1.0f, 0.15f, 0.1f}, Material::Neon);
            addScript(scene, n, kKillBrick);
            break;
        case Premade::Spinner:
            n = addPart(scene, "Spinner", Cube, at + glm::vec3(0, 0.6f, 0), {8, 0.4f, 0.4f}, {0.95f, 0.55f, 0.1f});
            addScript(scene, n, kSpinner);
            break;
        case Premade::JumpPad:
            n = addPart(scene, "JumpPad", PrimitiveType::Cylinder, at + glm::vec3(0, 0.1f, 0), {2.5f, 0.2f, 2.5f}, {0.2f, 0.9f, 0.3f}, Material::Neon);
            addScript(scene, n, kJumpPad);
            break;
        case Premade::Coin:
            n = addPart(scene, "Coin", PrimitiveType::Cylinder, at + glm::vec3(0, 1.2f, 0), {0.9f, 0.12f, 0.9f}, {1.0f, 0.8f, 0.15f}, Material::Metal);
            n->transform.rotation = {90, 0, 0};
            n->canCollide = false;
            addScript(scene, n, kCoin);
            break;
        case Premade::DisappearingPlatform:
            n = addPart(scene, "FadingPlatform", Cube, at + glm::vec3(0, 2.0f, 0), {4, 0.4f, 4}, {0.3f, 0.75f, 0.95f});
            addScript(scene, n, kDisappearing);
            break;
        case Premade::MovingPlatform:
            n = addPart(scene, "MovingPlatform", Cube, at + glm::vec3(0, 1.0f, 0), {4, 0.4f, 4}, {0.6f, 0.45f, 0.95f});
            addScript(scene, n, kMoving);
            break;
        case Premade::SpeedPad:
            n = addPart(scene, "SpeedPad", Cube, at + glm::vec3(0, 0.05f, 0), {3, 0.1f, 3}, {0.1f, 0.6f, 1.0f}, Material::Neon);
            addScript(scene, n, kSpeedPad);
            break;
        case Premade::ClickButton:
            n = addPart(scene, "ClickButton", Cube, at + glm::vec3(0, 0.5f, 0), {1.5f, 1, 1.5f}, {0.9f, 0.2f, 0.5f});
            addScript(scene, n, kClickButton);
            break;
        case Premade::FallingBall:
            n = addPart(scene, "Ball", PrimitiveType::Sphere, at + glm::vec3(0, 6.0f, 0), {1.5f, 1.5f, 1.5f}, {0.95f, 0.95f, 0.95f});
            n->anchored = false;
            break;
        case Premade::Landmine:
            n = addPart(scene, "Landmine", PrimitiveType::Cylinder, at + glm::vec3(0, 0.05f, 0), {0.9f, 0.1f, 0.9f}, {0.3f, 0.05f, 0.05f}, Material::Neon);
            n->canCollide = false;
            addScript(scene, n, kLandmine);
            break;
        case Premade::SawBlade:
            n = addPart(scene, "SawBlade", PrimitiveType::Cylinder, at + glm::vec3(0, 1.6f, 0), {3.0f, 0.12f, 3.0f}, {0.75f, 0.77f, 0.8f}, Material::Metal);
            n->transform.rotation = {90, 0, 0};
            addScript(scene, n, kSawBlade);
            break;
        case Premade::SpikeTrap: {
            n = scene.insert(std::make_unique<SceneNode>("SpikeTrap", NodeKind::Model));
            SceneNode* base = addPart(scene, "Base", PrimitiveType::Cube, at + glm::vec3(0, 0.05f, 0), {3, 0.1f, 3}, {0.2f, 0.2f, 0.22f}, Material::Metal);
            scene.reparent(base, n);
            SceneNode* spikes = addPart(scene, "Spikes", PrimitiveType::Cube, at + glm::vec3(0, -0.45f, 0), {2.6f, 1.0f, 2.6f}, {0.7f, 0.7f, 0.75f}, Material::Metal);
            scene.reparent(spikes, n);
            addScript(scene, n, kSpikeTrap);
            break;
        }
        case Premade::ExplodingBarrel:
            n = addPart(scene, "ExplodingBarrel", PrimitiveType::Cylinder, at + glm::vec3(0, 0.75f, 0), {1.0f, 1.5f, 1.0f}, {0.8f, 0.12f, 0.08f}, Material::Metal);
            n->anchored = false;
            addScript(scene, n, kBarrel);
            break;
        case Premade::LampPost: {
            n = addPart(scene, "LampPost", PrimitiveType::Cylinder, at + glm::vec3(0, 2.0f, 0), {0.25f, 4.0f, 0.25f}, {0.15f, 0.15f, 0.17f}, Material::Metal);
            auto bulb = std::make_unique<SceneNode>("Bulb");
            bulb->primitiveType = PrimitiveType::Sphere;
            bulb->mesh = MeshLibrary::get(PrimitiveType::Sphere);
            bulb->transform.position = {0, 0.56f, 0};       // in the pole's (scaled) space
            bulb->transform.scale    = {2.4f, 0.15f, 2.4f};
            bulb->color = {1.0f, 0.85f, 0.55f};
            bulb->material = Material::Neon;
            bulb->castShadow = false;
            SceneNode* b = scene.insert(std::move(bulb), n);
            auto light = std::make_unique<SceneNode>("PointLight", NodeKind::Light);
            light->color = {1.0f, 0.8f, 0.55f};
            light->brightness = 3.0f;
            light->range = 16.0f;
            light->transform.position = {0, -1.0f, 0};
            scene.insert(std::move(light), b);
            break;
        }
        case Premade::DiscoFloor: {
            auto model = std::make_unique<SceneNode>("DiscoFloor", NodeKind::Model);
            n = scene.insert(std::move(model));
            for (int x = 0; x < 4; ++x)
                for (int z = 0; z < 4; ++z) {
                    auto t = std::make_unique<SceneNode>("Tile");
                    t->primitiveType = PrimitiveType::Cube;
                    t->mesh = MeshLibrary::get(PrimitiveType::Cube);
                    t->transform.position = at + glm::vec3(x * 2.0f - 3.0f, 0.05f, z * 2.0f - 3.0f);
                    t->transform.scale = {1.9f, 0.1f, 1.9f};
                    t->material = Material::Neon;
                    t->color = {0.5f, 0.2f, 0.9f};
                    scene.insert(std::move(t), n);
                }
            auto light = std::make_unique<SceneNode>("PartyLight", NodeKind::Light);
            light->transform.position = at + glm::vec3(0, 5, 0);
            light->brightness = 4.0f;
            light->range = 20.0f;
            light->color = {1, 0.3f, 0.8f};
            scene.insert(std::move(light), n);
            addScript(scene, n, kDisco);
            break;
        }
        case Premade::DayNightCycle: {
            auto s = std::make_unique<SceneNode>("DayNightCycle", NodeKind::Script);
            s->source = kDayNight;
            n = scene.insert(std::move(s));
            break;
        }
    }
    return n;
}

const std::vector<Snippet>& snippetList() {
    static const std::vector<Snippet> list = {
        {"Print a message",
         "print(\"Hello!\")\n"},
        {"Wait, then do something",
         "wait(2)   -- seconds\nprint(\"2 seconds later...\")\n"},
        {"Loop forever",
         "while true do\n    -- do something here\n    wait(1)   -- always wait inside a loop!\nend\n"},
        {"Repeat 10 times",
         "for i = 1, 10 do\n    print(\"Count:\", i)\nend\n"},
        {"If / else",
         "local number = math.random(1, 10)\nif number > 5 then\n    print(\"Big number:\", number)\nelse\n    print(\"Small number:\", number)\nend\n"},
        {"Make a function",
         "local function sayHi(name)\n    print(\"Hi, \" .. name .. \"!\")\nend\n\nsayHi(\"Builder\")\n"},
        {"When this part is touched",
         "script.Parent.Touched:Connect(function(hit)\n    local humanoid = hit.Parent:FindFirstChild(\"Humanoid\")\n    if humanoid then\n        print(\"The player touched me!\")\n    end\nend)\n"},
        {"When this part is clicked",
         "script.Parent.Clicked:Connect(function()\n    print(\"Clicked!\")\nend)\n"},
        {"Every frame",
         "game:GetService(\"RunService\").Heartbeat:Connect(function(dt)\n    -- runs about 60 times a second\nend)\n"},
        {"When a key is pressed",
         "game:GetService(\"UserInputService\").InputBegan:Connect(function(input)\n    if input.KeyCode == Enum.KeyCode.E then\n        print(\"You pressed E\")\n    end\nend)\n"},
        {"Make a new part",
         "local part = Instance.new(\"Part\")\npart.Size = Vector3.new(2, 2, 2)\npart.Position = Vector3.new(0, 10, 0)\npart.Color = Color3.fromRGB(255, 100, 0)\npart.Parent = workspace   -- it falls, because new parts aren't anchored\n"},
        {"Change colour",
         "script.Parent.Color = Color3.fromRGB(0, 170, 255)\n"},
        {"Move a part",
         "local part = script.Parent\npart.Position = part.Position + Vector3.new(0, 5, 0)   -- 5 up\n"},
        {"Show text on screen",
         "Gui.Label(\"Score\", \"Score: 0\")          -- a line in the corner\nGui.Message(\"Welcome to my game!\", 3)   -- big text for 3 seconds\n"},
        {"Player health and speed",
         "local character = game.Players.LocalPlayer.Character\nlocal humanoid = character:FindFirstChild(\"Humanoid\")\nhumanoid.WalkSpeed = 12\nhumanoid.JumpPower = 12\n"},
        {"When the player dies",
         "local humanoid = game.Players.LocalPlayer.Character.Humanoid\nhumanoid.Died:Connect(function()\n    print(\"Oof!\")\nend)\n"},
    };
    return list;
}
