# Guts and Bolts

A Roblox-style game engine and platform with a Happy Wheels streak. It comes
as two apps:

- **Guts and Bolts Studio** (`GutsAndBolts`) is the editor. You build worlds out
  of parts, write Lua scripts, set up lighting and test with **Play**.
- **Guts&Bolts Player** (`GutsAndBoltsPlayer`) is the platform. You browse
  games, dress up your avatar, and play alone or with friends (with chat).

## Installing (the easy way)

1. Download this repository (the green **Code** button, then **Download ZIP**)
   and unzip it.
2. Double-click the installer for your computer:
   - **Windows:** `Install.bat`
   - **Mac:** `Install.command` (if macOS blocks it, right-click it and choose **Open**)
   - **Linux:** run `./install.sh` in a terminal
3. Press **Install** in the window that opens.

The installer works out which operating system you have and sets everything
up for you:

| Your computer | What it does |
| --- | --- |
| Windows | Installs MSYS2 (a free compiler kit) with CMake, GLFW, GLEW and GLM |
| macOS | Uses Apple's developer tools and Homebrew (`cmake ninja glfw glew glm`) |
| Linux | Uses apt, dnf, pacman or zypper, asking for your password in a popup |

Then it downloads the rest of the engine (Dear ImGui, Lua, ...), builds both
apps and adds Desktop / menu shortcuts. The first build takes a few minutes.

> No window? Run `python3 install.py --cli` for the text version.
> `python3 install.py --check` just shows what it detected.

## Guts and Bolts Studio (the editor)

The window has a toolbar, a status bar and docked panels:

- **Viewport**: the 3D view. Click to select, drag the move / rotate / scale gizmo.
- **Explorer**: everything in your game. Drag an object onto another to group
  it, and double-click a Script to edit it.
- **Toolbox**: parts, scripts, lights and **ready-made** objects that already
  contain scripts: kill brick, coin, jump pad, spinner, moving and fading
  platforms, speed pad, click button, lamp post, disco floor, landmine, saw
  blade, spike trap, exploding barrel and a day/night cycle.
- **Properties**: colour, material (Plastic, Metal, Neon, Wood, Glass,
  Concrete, Ice), size, anchored, can-collide and so on.
- **Lighting**: sun, shadows, sky light, clouds, stars, fog, exposure,
  bloom, ambient occlusion and colour grading, plus a **time of day** slider
  and presets (Day, Sunset, Night, Overcast, Horror).
- **Player**: walk speed, jump power, health, outfit and hat, plus the game's
  **Death & Gore** rules.
- **Script Editor**: Lua code with a live mistake checker and an
  **Insert code...** menu of ready-to-use snippets.
- **Output**: what your scripts `print()`, plus any errors (in red, with the
  line number).

**File** menu: New, Open, Save (games go in the `games` folder next to the
app), Game Settings (the title and description shown in the Player app) and
**Play in Guts&BoltsPlayer**. **Edit** menu: Undo / Redo, Copy / Paste,
Duplicate, Delete. **View > Settings** covers the frame rate and graphics.

### Playtesting

Press **Play** (or **F5**). Your scripts start, and you walk with **WASD**,
jump with **Space**, look around with the right mouse button and zoom with
the wheel. Press **F5** or **Esc** to stop, and everything goes back exactly
how it was.

### Controls

| Action | Input |
| --- | --- |
| Select / Move / Rotate / Scale tool | `Q` / `W` / `E` / `R` |
| Orbit / pan / zoom camera | Middle-drag / Shift + middle-drag / wheel |
| Focus on selection | `F` |
| Undo / Redo | `Ctrl+Z` / `Ctrl+Y` |
| Copy / Paste / Duplicate / Delete | `Ctrl+C` / `Ctrl+V` / `Ctrl+D` / `Del` |
| Save / Open / New | `Ctrl+S` / `Ctrl+O` / `Ctrl+N` |
| Play / Stop | `F5` |

## Scripting (Lua)

Scripts are written in Lua and work a lot like Roblox scripts. Put a Script
inside a part and `script.Parent` is that part:

```lua
-- Make this part a kill brick
script.Parent.Touched:Connect(function(hit)
    local humanoid = hit.Parent:FindFirstChild("Humanoid")
    if humanoid then
        humanoid.Health = 0
    end
end)
```

What you can use:

- **Objects:** `workspace`, `script`, `Instance.new("Part" | "Ball" | "Cylinder" | "Model" | "PointLight" | "SpotLight", parent)`,
  `:FindFirstChild`, `:WaitForChild`, `:GetChildren`, `:GetDescendants`, `:Clone`, `:Destroy`, `:IsA`
- **Part properties:** `Name`, `Parent`, `Position`, `Orientation`, `Size`, `CFrame`, `Color`, `Transparency`,
  `Material`, `Shape`, `Anchored`, `CanCollide`, `Velocity`
- **Values:** `Vector3.new`, `Color3.new` / `fromRGB` / `fromHSV` / `fromHex`, `CFrame.new` / `Angles` / `lookAt`
- **Time:** `wait(seconds)`, `spawn(fn)`, `delay(seconds, fn)`, `time()`, `tick()`
- **Events:** `part.Touched`, `part.Clicked`, `RunService.Heartbeat`, `UserInputService.InputBegan` / `InputEnded`,
  `humanoid.Died`, `Players.PlayerAdded` / `PlayerRemoving`, each with `:Connect`, `:Once` and `:Wait`
- **Players:** `game.Players.LocalPlayer.Character`, `Players:GetPlayers()`, `humanoid.Health` / `WalkSpeed` /
  `JumpPower` / `:TakeDamage(n)`, `character:BreakJoints()`
- **Lighting:** `Lighting.ClockTime`, `Brightness`, `FogEnabled`, `FogColor`, `Ambient`, ...
- **Screen text:** `Gui.Label("Score", "Score: 5")`, `Gui.Message("You win!", 3)`
- **Mayhem:** `Explode(position, radius)`, `Effects.Blood(pos, amount)`, `Effects.Oil(...)`, `Effects.Gibs(...)`,
  `Effects.Sparks(...)`
- **Game rules:** `workspace.Gravity`, `workspace.DeathStyle = "Classic" | "Ragdoll"`, `workspace.Gore = "Off" | "Oil" | "Blood"`

A script that loops forever without `wait()` is stopped after 5 seconds with a
friendly error, so it can't freeze your game. Scripts can't touch files on
your computer.

## Death, ragdolls and gore

Each game picks its own rules in the Player panel (**Death & Gore**):

- **Death style:** *Classic* makes the character fall to pieces like in
  Roblox. *Ragdoll* makes the body go limp and tumble with real joints.
- **Gore:** *Off*, *Oil & Bolts* (robot style) or *Blood*. Blood and oil
  splat on floors and walls.
- **Dismemberment:** explosions, saw blades and long falls can knock limbs off.
- **Fall damage:** landing too hard hurts.

Players can switch blood and gore off in every game with **Settings > Show
blood & gore**.

## Guts&Bolts Player

- **Home:** every game in the `games` folder, each with a rendered preview.
  Press **Play** to play alone or **Host** to let friends join.
- **Join a Friend:** type the host's address (the host's chat shows it). The
  default port is 7777, so for play over the internet the host needs to
  forward that port on their router.
- **Avatar:** display name, outfits, body colours and hats. You wear these in
  every game.
- **In game:** `/` to chat (speech bubbles show over heads), `Esc` for the
  menu (Resume, Reset Character, Settings, Leave).

Three sample games come with it: **Obby of Doom**, **Demolition Yard** and
**Night Plaza** (built by `tools/make_sample_games.py`).

## Graphics & performance

**Settings** (in both apps) has VSync, an FPS limit (or unlimited), an FPS
counter, quality presets (Low / Medium / High / Ultra), shadow detail and
style, ambient occlusion, bloom, anti-aliasing and render scale. They're
saved to `settings.json`.

The renderer uses HDR with physically based shading, soft contact-hardening
sun shadows, point and spot lights, sky reflections, SSAO, bloom, ACES tone
mapping and FXAA. It needs OpenGL 4.1, so it runs on Windows, macOS and Linux.

## Building by hand

You need CMake, a C++20 compiler, and GLFW, GLEW and GLM where `find_package`
can see them. Dear ImGui, ImGuizmo, Lua 5.4 and nlohmann/json are downloaded
automatically.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/GutsAndBolts          # the editor
./build/GutsAndBoltsPlayer    # the player app
```

On Windows with MSYS2, add `-DCMAKE_PREFIX_PATH="C:/msys64/mingw64"`. The
runtime DLLs are copied next to the programs automatically.

Handy command-line options: `GutsAndBolts --open file.gbscene --play`,
`GutsAndBoltsPlayer file.gbscene`, `GutsAndBoltsPlayer --join 192.168.1.20`.

## Layout

```
install.py, Install.bat, Install.command, install.sh    the installer
games/                  sample games (copied next to the apps)
tools/                  make_sample_games.py
src/
  core/                 AppWindow, Log, Settings, Paths, CrashHandler
  renderer/             SceneRenderer + Shaders (HDR, shadows, post-fx), Mesh, Camera, ...
  scene/                Scene tree, Player (R6 rig), Physics, Ragdoll, Particles,
                        Effects, Serializer (save files / undo)
  scripting/            Lua engine and Roblox-style API
  game/                 GameSession (play mode), Hud, Profile, SettingsWindow
  net/                  TCP sockets and multiplayer host / client
  editor/               Studio: Editor, Premades, panels/
  player/               Guts&BoltsPlayer app
```
