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

## Android (experimental)

The Player also runs on Android phones and tablets (Android 7.0+). Download
`GutsAndBoltsPlayer-Android.apk` from the
[Releases page](https://github.com/useofscript/Project-Guts-And-Bolts/releases),
open it on your phone and allow installing it. It has the thumbstick / jump
button touch controls, swipe scrolling, the Back button, the sample games
built in, and it can host or join games with phones and computers on the same
Wi-Fi. Studio (making games) stays on computers.

To build the APK yourself you need the Android SDK and NDK:

```sh
cd android
./gradlew assembleRelease     # -> app/build/outputs/apk/release/app-release.apk
```

To try the phone version on a Linux computer:
`cmake -S . -B build-mobile -DGB_MOBILE=ON && cmake --build build-mobile`, then
run `GB_TOUCH_SCREEN=1 GB_UI_SCALE=2 ./build-mobile/GutsAndBoltsPlayer`.

## Guts and Bolts Studio (the editor)

Studio is laid out like Roblox Studio, with the same dark theme and a
**ribbon** across the top:

- **HOME**: clipboard, the Select / Move / Scale / Rotate tools, insert a Part
  or any Object, group / lock / anchor, and Play / Play Here / Simulate / Stop.
- **MODEL**: snap-to-grid (studs and degrees), parts, **MeshPart** and
  **Edit Mesh** (Modeling mode), constraints, scripts, **Pivot to middle** for models, and **Align** (line the selection up on X,
  Y or Z by their min, center or max).
- **TEST**: Play, Play Here, Simulate, Pause, Step and Stop, plus the Player
  and Lighting settings.
- **MESH** (only in Modeling mode): pick corners / edges / faces, and the
  shape tools.
- **VIEW**: show or hide each panel, reset the camera, and the shortcut list.

The panels:

- **Viewport**: the 3D view. Click to select, drag the gizmo. Clicking a part
  inside a Model picks the whole Model (hold **Alt** to pick just the part),
  just like Roblox. Locked parts can't be clicked.
- **Explorer**: everything in your game, with a Roblox-style icon for each
  kind of object. Drag an object onto another to put it inside, double-click a
  Script to edit it, and use the **+** button on a row to insert something.
  The **filter box** (`Ctrl+Shift+X`) searches names, and also understands
  Roblox's search words: `c:Script` (kind of object), `is:Script`, `tag:Enemy`,
  `name:Door`, property checks like `Anchored=false` or `Transparency>0.5`, and
  `or` to match either side.
- **Toolbox**: parts, scripts, lights and **ready-made** objects that already
  contain scripts: kill brick, coin, jump pad, spinner, moving and fading
  platforms, speed pad, click button, lamp post, disco floor, landmine, saw
  blade, spike trap, exploding barrel and a day/night cycle. There are also
  physics toys: swinging rope, wrecking ball, windmill, seesaw, a drivable
  **Motor Cart**, domino run, crate pyramid and trampoline.
- **Insert Object** (`Ctrl+I` or the **+** in the Explorer): a searchable list
  of every kind of object you can add.
- **Constraints** (in the Toolbox): pick Rope, Rod, Spring, Weld, Hinge or
  Motor, then click two parts in the Viewport to join them.
- **Properties**: colour, material (Plastic, Metal, Neon, Wood, Glass,
  Concrete, Ice), size, anchored, can-collide and so on.
- **Lighting**: sun, shadows, sky light, clouds, stars, fog, exposure,
  bloom, ambient occlusion and colour grading, plus a **time of day** slider
  and presets (Day, Sunset, Night, Overcast, Horror).
- **Player**: walk speed, jump power, health, outfit and hat, plus the game's
  **Death & Gore** rules.
- **Script Editor**: Lua code with a live mistake checker, an
  **Insert code...** menu of ready-to-use snippets, **autocomplete** (suggestions
  pop up as you type; `Tab` uses one, `Ctrl+Up/Down` picks), **Find / Replace**
  (`Ctrl+F` / `Ctrl+H`, `F3` for the next one) and **Find in All Scripts**
  (`Ctrl+Shift+F`: click a result to jump straight to it).
- **Output**: what your scripts `print()`, plus any errors (in red, with the
  line number).
- **Command Bar**: type a line of Lua and press Enter to run it on your game
  right away, e.g. `workspace.Baseplate.Transparency = 0.5`. Shift+Enter adds
  a new line, Ctrl+Up / Ctrl+Down bring back earlier commands, and it can be
  undone with Ctrl+Z.

**File** menu: New, Open, Save (games go in the `games` folder next to the
app), **Import / Export Roblox** files, Game Settings (the title and
description shown in the Player app) and **Play in Guts&BoltsPlayer**.
**Edit** menu: Undo / Redo, Copy / Paste, Duplicate, Delete. **View >
Settings** covers the frame rate and graphics.

### Roblox files (.rbxl, .rbxlx, .rbxm, .rbxmx)

Open a Roblox place (`.rbxl` / `.rbxlx`) and it becomes a Guts and Bolts
game. Import a model (`.rbxm` / `.rbxmx`) and it's added to your game. Both
the binary and XML kinds work. What comes across: parts (blocks, spheres,
cylinders, wedges, trusses), models and folders, scripts (Roblox's **Luau**
code is converted to plain Lua for you), lights, sounds, attachments and
constraints, value objects, attributes and tags. Things we don't have yet
(meshes, terrain, GUIs) are skipped, and Output tells you what was left out.

**File > Export Roblox** saves your game (or just the selection) as a Roblox
file that Roblox Studio can open, so you can take your work either way.
Roblox characters are twice our size, so everything is scaled to match.

### Modes (like Blender)

The menu in the Viewport's top-left corner (or the **Mode** menu) switches
between four modes:

- **Build** (the normal one): place, move, resize and change objects.
- **Modeling** (`Tab`): reshape a part yourself, like Blender's Edit Mode.
  Select a part and press `Tab`. It becomes a **MeshPart** and you can pick
  its corners (`1`), edges (`2`) or faces (`3`). Click to pick, Shift+click
  to add, or drag a box. Move, turn or stretch what you picked with the
  gizmo. The **MESH** tab has the tools:
  - **Extrude** (`E`): pull faces or edges out into new ones.
  - **Inset** (`I`): put a smaller face inside each picked face.
  - **Subdivide**: cut faces into smaller ones.
  - **Merge** (`M`): squash the picked corners into one.
  - **Fill** (`F`): make a face between picked corners.
  - **Delete** (`X`).
  - **Flip**: turn faces inside out.
  - **Smooth / Flat** shading.
  - **X-Ray** (`Alt+Z`): see and pick through the mesh.

  Press `Tab` again when you're done. To start from scratch, use
  **MODEL > MeshPart** or Insert Object > MeshPart. MeshParts save with your
  game, work in the Player app and collide like any part.
- **Simulate** (`F8`): physics and scripts run live, but there's no
  character. Fly the camera around, click things to see their properties
  change as they move, and drag them with the gizmo while everything keeps
  running. `F6` pauses and `F7` steps one frame at a time.
- **Play** (`F5`): a real playtest. You spawn at a SpawnLocation, walk with
  **WASD**, jump with **Space** and look around with the right mouse button.
  **Play Here** starts you where the camera is looking. `F6` pauses here too.

**Stop** (`Shift+F5` or `Esc`) goes back to Build mode, and everything goes
back exactly how it was before you pressed Simulate or Play.

### Assistant (AI help)

The **Assistant** tab (HOME > Assistant) is Studio's AI helper, like Roblox
Studio's Assistant. Two ways to use it:

- **Chat with Claude.** Paste your Anthropic API key (from
  console.anthropic.com) into Chat settings. It stays on your computer. Then ask
  for things like "make an obby with 10 jumps" or "why doesn't my door script
  work?". The Assistant can look at your game, build, write and fix scripts,
  run Lua, playtest, read the Output and take screenshots to check its work.
  Ctrl+Z undoes anything it does.
- **Connect AI apps with MCP.** Tick "Let AI apps use Studio (MCP)" and apps
  on your computer that speak MCP (the Model Context Protocol) can use the same
  tools: Claude Code, Claude Desktop, and other AI coding tools. Only programs
  on this computer can connect. The tab shows copy-and-paste set-up for each:
  - Claude Code: `claude mcp add --transport http guts-and-bolts http://127.0.0.1:44755/mcp`
  - Claude Desktop and apps that start a program: run `GutsAndBolts --mcp` (it
    passes messages to the Studio that's open).

The tools: `get_game_tree`, `get_object`, `set_property`, `insert_object`,
`delete_object`, `create_script`, `read_script`, `edit_script`, `run_lua`,
`get_output`, `playtest`, `screenshot`, `select` and `undo`.

### Plugins

Plugins are small Lua files that add buttons to Studio's **PLUGINS** tab.
They go in the `plugins` folder next to Studio, and two samples come with it
(**Random Colors** and **Stack Tower**). A plugin can use everything scripts
can, plus `plugin:Button(name, tooltip, function)` to add a button and
`Selection:Get()` / `Selection:Set({...})` for what you've picked:

```lua
plugin:Button("Paint Red", "Make the selected parts red", function()
    for _, part in ipairs(Selection:Get()) do
        part.Color = Color3.new(1, 0, 0)
    end
end)
```

Anything a plugin changes can be undone with Ctrl+Z. With a server,
**PLUGINS > Marketplace** lets you install plugins other people published
(buying them first if they cost Bolts), add uploaded audio to your game, and
publish your own plugins.

Studio's **File** menu also has **Publish to Guts&Bolts...**. It puts your
game on the server so everyone can play it, and later **updates** it with
your new version.

### Controls

These match Roblox Studio. Press **F1** (or **View > Shortcuts**) in Studio
to see them all.

| Action | Input |
| --- | --- |
| Select / Move / Scale / Rotate tool | `Ctrl+1` / `Ctrl+2` / `Ctrl+3` / `Ctrl+4` |
| Local / world gizmo | `Ctrl+L` |
| Fly the camera | Hold right mouse + `W` `A` `S` `D`, `Q` / `E` down / up, `Shift` faster |
| Look / pan / zoom | Right-drag / middle-drag / wheel (Shift + middle-drag orbits) |
| Zoom to the selection (parts, models, tools) | `F` |
| Pick the part inside a Model | `Alt+click` |
| Pick several things | `Ctrl+click` or `Shift+click` |
| Select all / parent / children / nothing | `Ctrl+A` / `Ctrl+Up` / `Ctrl+Down` / `Esc` |
| Expand / collapse selected (everything inside) | `Ctrl+Right` / `Ctrl+Left` |
| Collapse the whole Explorer | `Ctrl+Shift+Left` |
| Search the Explorer | `Ctrl+Shift+X` |
| Insert Object | `Ctrl+I` |
| Rename / hide | `F2` / `H` |
| Group into a Model / ungroup | `Ctrl+G` / `Ctrl+U` |
| Lock / anchor | `Alt+L` / `Alt+A` |
| Copy / Cut / Paste / Paste into | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` / `Ctrl+Shift+V` |
| Duplicate / Delete | `Ctrl+D` / `Del` |
| Undo / Redo | `Ctrl+Z` / `Ctrl+Y` |
| Save / Save as / Open / New | `Ctrl+S` / `Ctrl+Shift+S` / `Ctrl+O` / `Ctrl+N` |
| Play / Simulate / Stop | `F5` / `F8` / `Shift+F5` |
| Pause / step one frame (while testing) | `F6` / `F7` |
| Modeling mode on / off | `Tab` |

Copy, paste, duplicate, delete, hide, lock, anchor and the move tool all work
on everything you've selected at once.

**Snapping, the grid and collisions** (MODEL tab > Snap to Grid, or the
buttons in HOME > Edit):

- **Move** snaps moving (and resizing) to steps of however many studs you type,
  and the floor grid follows that size. **Rotate** snaps turning to steps of
  so many degrees. **Grid** shows or hides the floor grid.
- **Collisions**: parts you move stop flush against other parts instead of
  going through them, and can't be turned or resized into them. (Things
  already overlapping when you start can still move apart.)

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
- **Physics:** `part:ApplyImpulse(v)`, `AssemblyLinearVelocity`, `AssemblyAngularVelocity`, `Density`,
  `Friction`, `Elasticity`, and constraint properties such as `rope.Length` or `hinge.AngularVelocity`
- **Sound:** `Sounds.Play("coin", position)` (built in: jump, coin, oof, explosion, splat, click, hit, win,
  boing, spawn) or a Sound object with `:Play()` / `:Stop()`
- **Attributes:** `obj:SetAttribute("Damage", 25)`, `obj:GetAttribute("Damage")`, `obj:GetAttributes()`,
  `obj.AttributeChanged`, `obj:GetAttributeChangedSignal("Damage")` (you can also add them in the
  **Attributes** section of the Properties panel)
- **Tags:** `obj:AddTag("Lava")`, `:HasTag`, `:RemoveTag`, `:GetTags`, and `CollectionService:GetTagged("Lava")`,
  `:GetInstanceAddedSignal("Lava")` / `:GetInstanceRemovedSignal` (or add tags in the Properties panel)
- **Modules:** `require(workspace.MyModule)` runs a ModuleScript once and hands back what it returns
- **Game rules:** `workspace.Gravity`, `workspace.DeathStyle = "Classic" | "Ragdoll"`, `workspace.Gore = "Off" | "Oil" | "Blood"`

`script:Destroy()` or `script.Disabled = true` stops a script (and all of its
events), just like in Roblox.

A script that loops forever without `wait()` is stopped after 5 seconds with a
friendly error, so it can't freeze your game. Scripts can't touch files on
your computer.

## Decals (pictures on parts)

A **Decal** puts a picture on one side of a part. Insert one inside a part
(*Insert Object > Decal*), then set it up in Properties:

- **Texture:** the picture. It can be a `.png` / `.jpg` in the games folder
  (like `pics/logo.png`), a full path, or `gb:<id>` for one uploaded on the
  site's Create page. **Browse...** picks a file.
- **Face:** which side: Front, Back, Left, Right, Top or Bottom. The picture
  stretches to fill that side.
- **Color3** tints it (white shows it as it is), and **Transparency** fades it.
  See-through parts of a PNG stay see-through.

Scripts can change them too:

```lua
local d = Instance.new("Decal")
d.Texture = "gb:decal-1234abcd"
d.Face = Enum.NormalId.Top
d.Parent = workspace.Sign
```

## Tools (swords, bats, anything you can hold)

A **Tool** is something a character carries, like a Roblox Tool. Insert one
with *Insert Object > Tool*: it comes with a part called **Handle**. That's
what the character holds, with its long side (Y) pointing forward out of
the fist. Add more parts inside the tool (a blade, a guard...) and they
come along.

- **Picking up:** walk into a tool's Handle and it goes in your backpack.
- **Hotbar:** your tools show along the bottom of the screen. Press **1-9**
  (or click / tap a slot) to hold one; press the same number again to put it
  away. **Backspace** drops the one you're holding (unless *CanBeDropped* is
  off).
- **In StarterPack:** tick this and everyone spawns with that tool (and gets
  it again after respawning, like Roblox).
- **Using it:** clicking while you hold a tool swings your arm and fires the
  tool's `Activated` event.

```lua
-- A script inside the tool
local tool = script.Parent
tool.Activated:Connect(function()
    print("Swing!")
end)
tool.Handle.Touched:Connect(function(hit)
    -- the held Handle touching something (only while you hold it)
end)
tool.Equipped:Connect(function() end)      -- also Unequipped, Deactivated
```

Also: `tool.Enabled`, `tool.ToolTip`, `tool.CanBeDropped`, `tool.GripPos`,
`humanoid:EquipTool(tool)`, `humanoid:UnequipTools()` and
`Players.LocalPlayer.Backpack` (parent a tool there to give it). Try the
**Bat** in *Demolition Yard*. Tools work in single player and for the host
of an online game; people who join someone else's game can't carry tools
yet.

## Game UI (ScreenGui, buttons, labels)

Games can have their own on-screen UI, like Roblox's: menus, shop buttons, coin
counters and title screens.

- **Insert** (Ctrl+I) a **ScreenGui**. It goes in the **StarterGui** folder.
  Then insert **Frame**, **TextLabel**, **TextButton**, **ImageLabel** or
  **ImageButton** into it. Put a **UICorner** inside one to round its corners,
  or a **UIStroke** to give it an outline.
- The UI shows in the viewport while you build. Click it to pick it, drag it to
  move it, and drag the blue corner to resize it. Everything else is in
  **Properties**.
- Sizes and positions are **UDim2**, like Roblox: a fraction of the parent plus
  pixels. `UDim2.new(0.5, 0, 1, -60)` means "halfway across, 60 pixels up from
  the bottom". **AnchorPoint** picks which point of it sits there (0.5, 0.5 is
  the middle).

Scripts use the same names as Roblox:

```lua
local gui = Instance.new("ScreenGui")
gui.Parent = game:GetService("StarterGui")

local button = Instance.new("TextButton")
button.Size = UDim2.fromOffset(200, 50)
button.Position = UDim2.new(0.5, 0, 1, -40)
button.AnchorPoint = Vector2.new(0.5, 1)
button.Text = "Clicks: 0"
button.BackgroundColor3 = Color3.fromRGB(40, 180, 80)
button.Parent = gui
Instance.new("UICorner", button)

local clicks = 0
button.MouseButton1Click:Connect(function()
    clicks = clicks + 1
    button.Text = "Clicks: " .. clicks
end)
```

Buttons have `MouseButton1Click` (also called `Activated`), `MouseEnter` and
`MouseLeave`. Text has `TextScaled`, `TextWrapped`, alignment, an outline
(`TextStrokeTransparency`) and bold fonts (`Enum.Font.SourceSansBold`).
`player.PlayerGui` and `game.StarterGui` are the same folder.

In multiplayer, everyone sees the same UI. When anyone clicks a button, the
host's scripts hear about it. Roblox files keep their UI: ScreenGuis in a
place's StarterGui are imported, and exported back to Roblox.

## Animations (the Animation Editor)

Like Roblox's: make an animation in Studio, then play it from a script.

1. **AVATAR tab > Rig Builder** puts a dummy character in the world (or use
   any Model made of parts, like your own character).
2. **AVATAR tab > Animation Editor** opens the timeline along the bottom.
   Select the rig and press **Animate**, then **New** to make an Animation
   (it's saved inside the rig).
3. Move the red playhead to a time, click a body part, and turn it with the
   **Rotate** tool (or move it with Move). That adds a keyframe there. The
   handles sit on the joint, so arms turn at the shoulder, legs at the hip and
   heads at the neck. You can also type exact numbers in the panel on the right.
4. **Play** shows it moving. Drag keyframes to change their timing,
   right-click one for its **easing** (Linear, Constant, Cubic, Elastic,
   Bounce; In / Out / InOut), to name it, copy it or delete it. Tick **Loop**
   for things like idles and dances, and pick a **Priority** (Core, Idle,
   Movement, Action: higher wins when two animations move the same part).

While the editor is open the rig is only *shown* posed: saving, undo and
Play all see it exactly as you built it. Close the editor (or press
**Done**) to put it back.

Arms and legs hang off the torso like Roblox's R6 (and R15) characters, so
turning the Torso takes the arms and head with it. For your own models, a
part inside another part follows it, and an **Attachment named `Pivot`**
inside a part sets where it bends.

Play an animation from a script:

```lua
-- On your character (like a Roblox LocalScript):
local character = game.Players.LocalPlayer.Character
local humanoid = character:WaitForChild("Humanoid")
local animator = humanoid:WaitForChild("Animator")
local wave = animator:LoadAnimation(workspace.Wave)   -- an Animation object
wave:Play()                 -- :Play(fadeTime, weight, speed)
wave.Stopped:Wait()

-- On any rig (an NPC, a door, a machine): model:LoadAnimation(animation)
local dance = workspace.Rig:LoadAnimation(workspace.Rig.Dance)
dance.Looped = true
dance:Play()
dance.KeyframeReached:Connect(function(name) print("reached", name) end)
```

An AnimationTrack has `Play`, `Stop(fadeTime)`, `AdjustSpeed`,
`AdjustWeight`, `GetTimeOfKeyframe`, and `IsPlaying`, `Length`, `Looped`,
`Speed`, `TimePosition`, `Priority`, plus the `Stopped`, `Ended`, `DidLoop`
and `KeyframeReached` events. Animations play on top of walking: an
animation that moves the arms takes over the arms, and the legs keep walking.

## Leaderboard, checkpoints and saved data

**Leaderstats**, the Roblox way: put a folder called `leaderstats` inside a
player, with IntValues (or other values) in it, and they show as columns on
the player list, for everyone in an online game too.

```lua
local Players = game:GetService("Players")
local function setup(player)
    local stats = Instance.new("Folder")
    stats.Name = "leaderstats"
    stats.Parent = player
    local coins = Instance.new("IntValue")
    coins.Name = "Coins"
    coins.Parent = stats
end
setup(Players.LocalPlayer)
Players.PlayerAdded:Connect(setup)          -- people joining an online game
-- later: player.leaderstats.Coins.Value = player.leaderstats.Coins.Value + 1
```

Value objects (**IntValue, NumberValue, StringValue, BoolValue**) can also be
added in Studio (*Insert Object*). Scripts use `.Value` and `.Changed`.

**Checkpoints:** add the ready-made *Checkpoint* pad from the Toolbox (or
name any part `Checkpoint`). Touching it makes it your respawn point. Scripts
can also set `player.RespawnLocation = somePart`.

**Saved data** (`DataStoreService`) keeps things between visits:

```lua
local store = game:GetService("DataStoreService"):GetDataStore("Stats")
local visits = store:IncrementAsync("visits_" .. player.UserId, 1)
store:SetAsync("best", { score = 42 })
print(store:GetAsync("best").score)
```

It also has `UpdateAsync` and `RemoveAsync`. Data is saved per game, in each
player's account folder on the computer that runs the game (the host, online).

### Sound effects

Most built-in sounds are made in code, but a few are recordings in
`assets/sounds` (built into the apps):

| File | Name in scripts | When it plays |
| --- | --- | --- |
| `jump.wav` | `"jump"` | the character jumps |
| `spawn.wav` | `"spawn"` | you arrive in a game (a splat) |
| `respawn.wav` | `"respawn"` | you come back after dying |

Swap in your own .wav files with the same names and rebuild.

### Climbing and swimming

- **Climb:** walk into a **TrussPart** (Insert > TrussPart), or any part called
  *Ladder*, or one with the tag or attribute `Climbable`. You go up hand over
  hand. Let go of the keys to hang on, and press jump to leap off.
- **Swim:** a part called **Water** (Insert > Water: see-through, CanCollide
  off), or one with the tag or attribute `Water`. You float, move a bit slower,
  and hold jump to swim up. No fall damage when you land in water.
- Scripts can check with `humanoid:GetState()`, which returns "Climbing",
  "Swimming", "Freefall", "Running" or "Dead".

### Real water

Water isn't just a see-through box you can swim in. While the game runs:

- **Waves and ripples.** The surface moves. Drop something in and rings of
  ripples spread out and bounce off the sides of the pool. Swimming or walking
  through it leaves a wake.
- **Floating and sinking.** Loose (unanchored) parts float or sink depending on
  their material, like real life: **Wood**, **Ice**, **Plastic** and **Neon**
  float (wood highest), while **Metal**, **Glass** and **Concrete** sink. Set a
  part's Density to choose exactly. A long plank tips over and floats flat, and
  boats rock on the waves.
- **Splashes.** Things (and people) that fall in throw up spray, make a splash
  sound and a dip in the water.
- **Ocean swell.** Give a water part a number attribute called `Waves` (like
  `0.5`) for big rolling waves that lift everything floating on them.
- **Currents.** Give it a Vector3 attribute called `Flow` (like `4, 0, 0`) and
  it carries swimmers and floating things along: rivers, rapids, lazy rivers.

Scripts can make, move or resize water while playing (a rising flood works).

### Flowing water (WaterSource)

Insert > **WaterSource** makes a spout that pours water while the game runs.
The water really flows: it runs downhill, spreads across the floor, fills
holes and pools, piles up behind walls and pours over the edges when they're
full. You can swim in it, things float on it, and fast-moving water carries
you along.

- **Rate** attribute: how much water it pours each second (default 8). Set it
  to 0 from a script to turn the tap off, and back up to turn it on.
- **FloodSize** attribute: how big an area the water can spread over (default
  80 units, up to 200). Water that runs past the edge is gone.
- Any part called WaterSource works, or give one the tag `WaterSource`.

Try a flooding-room obby, a dam you blow up, or a sinking ship.

## NPCs (zombies and other characters)

Any Model built like a character (with a **HumanoidRootPart**, **Torso** and
**Head** inside) becomes an NPC when the game starts, or as soon as a script
puts one in the world (for example, cloning a zombie out of ServerStorage). It
has a Humanoid, stands on the ground, walks with swinging arms and legs, and
falls apart (or ragdolls) when it dies.

The Toolbox has a ready-made **Zombie**. It chases the nearest player, walks
around walls and bites. Open its script to see how it works.

Steer an NPC from a script through its Humanoid, like Roblox:

```lua
local npc = workspace.Guard
local humanoid = npc.Humanoid

humanoid.WalkSpeed = 8
humanoid:MoveTo(Vector3.new(10, 0, 20))   -- walk there
humanoid.MoveToFinished:Wait()            -- true if it got there (it gives up after 8 seconds)
humanoid.Jump = true                      -- hop once
humanoid.Died:Connect(function() print("got him!") end)
```

To walk around walls, ask **PathfindingService** for a route:

```lua
local PathfindingService = game:GetService("PathfindingService")
local path = PathfindingService:CreatePath()
path:ComputeAsync(npc.HumanoidRootPart.Position, target.Position)
if path.Status == Enum.PathStatus.Success then
    for _, point in ipairs(path:GetWaypoints()) do
        if point.Action == Enum.PathWaypointAction.Jump then humanoid.Jump = true end
        humanoid:MoveTo(point.Position)
        humanoid.MoveToFinished:Wait()
    end
end
```

Give the model a **WalkSpeed** or **MaxHealth** attribute to set those without
a script. NPCs whose name has "Zombie" in it (or tagged `Zombie`) walk with
their arms out. In multiplayer the host moves the NPCs, and everyone sees them.

The Command Bar now runs inside the game while you're playing, so you can poke
at NPCs (and anything else) live.

## Physics

Loose (un-anchored) parts are real rigid bodies: they tumble, spin, stack,
slide and bounce. Each part has **Density** (heavier), **Friction** (grippier)
and **Elasticity** (bouncier) in the Properties panel, with sensible defaults
per material (ice is slippery, metal is heavy). Parts that stop moving go to
sleep so big piles stay fast, and your character can shove light things around.

Constraints join parts together through **Attachments** (little points on a part):

| Constraint | What it does |
| --- | --- |
| Rope | Keeps two points no further apart than its length (it can go slack) |
| Rod | Keeps two points exactly the same distance apart |
| Spring | Pulls back towards its length, with stiffness and damping |
| Weld | Glues two parts together |
| Hinge | Lets a part swing around one axis, like a door or a wheel |
| Motor | A hinge that spins by itself (`AngularVelocity`, `MotorMaxTorque`) |

### The character's body

- **Hitbox like Roblox R6:** the body collides as a box as wide as the torso
  (2 studs) and half as deep (1 stud), from the feet to the top of the head, and
  it turns with the character. Arms and legs don't collide, so you fit through
  gaps sideways and brush past corners.
- **Touched per body part:** like Roblox, each part of the body that touches
  something fires `Touched` on its own (`hit` is that arm, leg, head...), and
  the body part's own `Touched` fires too.
- **Walk cycle follows your real speed:** a higher `WalkSpeed` takes quicker,
  longer strides; walking into a wall doesn't run on the spot.
- **Getting hit:** loose parts that crash into you shove you back, harder the
  heavier and faster they are. A big hit knocks you off your feet.
- **Moving platforms:** they carry you, spinning ones turn you with them, and
  jumping off keeps their speed.

### Names and whispers

Everyone's name floats above their head (NPCs too, like "Zombie"), with a
small health bar when they're hurt. Your own name hides when you zoom into
first person.

To send a private message, type `/w PlayerName message` (or `/whisper`) in
chat. Only you and that player see it, with no speech bubble. Capitals don't
matter, and the start of a name is enough if only one player's name starts
that way.

### The play camera

Like Roblox, the camera orbits your character's **head**. Right-drag to look
around and use the mouse wheel to zoom. As the camera comes close your
character fades away so it doesn't block the view. Scroll all the way in for
**first person**: the camera sits in your head, your body is invisible (you
still see the tool you're holding), your character turns to face where you
look, and the mouse looks around by itself with a dot in the middle of the
screen. Scroll out to go back. On phones, pinch to zoom in and out.

**Shift Lock**: press Shift to lock the mouse in the middle of the screen. The
camera moves over your right shoulder and your character always faces where
you look (a ring shows in the middle). Press Shift again to turn it off. It
works in the Player and in Studio's play test, and you can switch it off in
the in-game menu.

### The in-game menu

Press **Esc** (or the Menu button) for a Roblox-style menu with three tabs:

- **Players**: everyone in the server, with Verified and Staff tags.
- **Settings**: Shift Lock Switch, camera sensitivity, invert camera, volume,
  fullscreen, graphics quality, show FPS, blood and gore, touch controls, and
  a button for the advanced graphics settings.
- **Help**: the controls.

Along the bottom: **[R] Reset Character**, **[L] Leave Game** (both ask "Are
you sure?" first) and **[Esc] Resume Game**.

### Player collisions

Players bump into each other like in Roblox, and you can stand on someone's
head. Turn it off in **Game Settings > Damage & Blood > Player Collisions**
(or from a script: `workspace.PlayerCollisions = false`) and everyone walks
straight through each other.

## Death, ragdolls and gore

**Game Settings > Damage & Blood** (also in the Player panel) has all of it:
death style, fall damage on or off, how hard a fall has to be to hurt (Safe
Fall Speed) and how much it hurts (Fall Damage Strength), gore (off, oil &
bolts, or blood), dismemberment, and the blood itself: its **color** (red,
slime green, alien blue, ink, or anything), how much sprays out, and how long
pools last.

Blood is a liquid: drops stretch as they fly, splash into pools that spread
out and join together, and blood that hits a wall runs down it in drips.

Scripts can change these too: `workspace.FallDamage = false`,
`workspace.SafeFallSpeed`, `workspace.FallDamageScale`,
`workspace.BloodColor = Color3.new(0.2, 0.7, 0.1)`, `workspace.BloodAmount`, and
`Effects.Blood(position, amount, direction)` sprays blood (the direction is
optional, e.g. `Vector3.new(0, 2, -8)` to splatter a wall).

Each game picks its own rules in the Player panel (**Death & Gore**):

- **Death style:** *Classic* makes the character fall to pieces like in
  Roblox. *Ragdoll* makes the body go limp and tumble with real joints.
- **Gore:** *Off*, *Oil & Bolts* (robot style) or *Blood*. Blood and oil
  splat on floors and walls.
- **Dismemberment:** explosions, saw blades and long falls can knock limbs off.
- **Fall damage:** landing too hard hurts.

Players can switch blood and gore off in every game with **Settings > Show
blood & gore**.

### The character model

Everyone's character is built from `assets/models/player.obj`: a Blender
model with six objects named `Torso`, `Head`, `Left_Arm`, `Right_Arm`,
`Left_Leg` and `Right_Leg`, at Roblox size (a 2 x 2 x 1 torso, feet at 0).
The model is built into the apps when they're compiled, so to change how
characters look, edit that file (keep the names and sizes) and rebuild.
Games saved with the old blocky character get the new one when they load.

## Guts&Bolts Player

- **Home:** every game in the `games` folder, each with a rendered preview.
  Press **Play** to jump into a **public server** of that game.
- **Servers** (on a game's page, in the app and on the website): cards for
  every running server with the faces of the people in it, how full it is,
  **Join** to hop into that exact server and **Share** to copy a link to it.
- **Create a server** (on a game's page): a **private server** (friends and
  people with its code). The same window lists the servers running now and has
  a box for joining with a code.
- **Always online:** everyone plays on the main Guts&Bolts server; there's no
  offline or local-network play. Without internet the site says *Can't reach
  Guts&Bolts* with a **Try again** button. (Studio still builds and
  play-tests without internet.)
- **Friends:** see "Friends and servers" below.
- **Catalog:** hats, shirts and pants for your avatar. Only the official staff
  account can add items (see below), so it starts out empty.
- **Avatar:** display name, outfits, body colours and hats. You wear these in
  every game. It also shows your **badges** and your **account ID**, and has a
  box for redeeming badge codes.
- **In game:** `/` to chat (speech bubbles show over heads), `Esc` for the
  menu (Resume, Reset Character, Settings, Leave). The player list in the top
  right shows everyone in the game.
- **Touch controls:** on phones and tablets you get a thumbstick (it appears
  under your thumb, bottom-left), a jump button (bottom-right) and Chat / Menu
  buttons (top-left). Drag anywhere else to turn the camera, pinch to zoom and
  tap things to click them. Try them on a computer with **Settings > Touch
  controls > Always on** (the mouse acts as one finger).

### Accounts, badges and staff

Every copy of Guts&Bolts makes its own account key the first time it runs. The
public half is your **account ID** (safe to share). The secret half stays in
`account.key` in your user folder and proves it's you when you join a game.

- **Guts** is the official staff account. Nobody else can use that name, and
  it wears the **Administrator** badge: a red shield with a bolt that floats
  next to its name in the player list and chat.
- **Official badges** (Administrator, Tester, Bug Hunter, Featured Creator,
  Verified, Staff) are signed by the staff account, so they can't be faked or
  copied to someone else. Staff make a badge code on the **Staff** page for a
  player's account ID; the player pastes it into **Avatar > Redeem**. (With a
  server, staff can do it straight from the Staff page instead - see below.)
- **Verified** people get a blue check next to their name everywhere (player
  list, chat, the site, their creations). On a server, Verified creators can
  publish anything: uploading hats, shirts, pants, audio and plugins is
  **free** for them, they have no daily upload limit, and they can **sell**
  what they make for Bolts. Only staff can verify people.
- **Staff** badge: the official Guts account can make other people Staff.
  Staff can verify people too (their Verified badges carry their own signed
  Staff badge, so everyone can check them).
- **Catalog items** are signed files in the `catalog` folder. The app ignores
  any item the staff account didn't sign. Each item is free or has a price in
  **Bolts** (see below).

### Guts&Bolts server (storing things online)

The **official Guts&Bolts server runs on Cloudflare** (with the website), so it's
online even when nobody's computer is on. The apps always use it:
`https://project-guts-and-bolts.pizzadoe173.workers.dev`. (Clicking the status
pill on the site's banner, or Studio's **File > Guts&Bolts Server... >
Reconnect**, tries again if the connection dropped.)

How it works:

- `worker/server.js` is the server, rewritten in JavaScript for Cloudflare. It
  uses the same requests and rules as the C++ server in `src/server`, and keeps
  its data in a Cloudflare Durable Object's own database.
- The apps talk to it over **HTTPS** (normal requests) and **secure WebSockets**
  (multiplayer). They use mbedTLS for that, and trust Mozilla's list of
  certificate authorities (`assets/certs/cacert.pem`).
- **Multiplayer** still runs each game on the host player's computer, and
  Cloudflare only passes messages between players. So no game runs on Cloudflare
  when nobody's playing, and players never see each other's addresses.
- **The staff account:** in the Cloudflare dashboard, open
  **Workers & Pages → project-guts-and-bolts → Settings → Variables and
  Secrets**, and add `OFFICIAL` = your account ID (the Player's Staff page:
  **Copy official ID**). That account is then user #1, **Guts**.
- Everything fits Cloudflare's free plan for a small community: about 100,000
  requests a day, and 5 GB of storage.

For developers, the C++ server can still run on a computer (for example to
test server changes). The apps only use it when started with the `GB_SERVER`
environment variable set to its address:

1. **Start it.** Double-click `tools/Start Server.bat` (Windows) or
   `tools/Start Server.command` (Mac), or run `python3 install.py --server`.
   A window opens and says which address and port (7780) it's on. Keep that
   window open while people play. Everything it stores goes in the
   `server_data` folder; back that folder up to keep it safe.
2. **Connect the apps.** Start them with `GB_SERVER` set to the server's
   address, like `GB_SERVER=192.168.1.20` or `GB_SERVER=myserver.com:7780`.
3. **Let friends outside your house connect** (optional). Either forward port
   7780 on your router to the server computer and give friends your public
   IP, or run the server on a rented Linux server (a "VPS") and give out its
   address. On Linux you can also run `GutsAndBoltsServer --data /path/to/data`
   directly (`--port`, `--name "My Server"` and `--official <staff account ID>`
   work too).

The staff account is found automatically on the computer where you set it up.
On another computer, start the server once with `--official <your account ID>`
(it remembers it in `server_data/server.json`).

**What the server keeps:**

- **Accounts:** names, badges (Verified, Staff...), and a Bolts balance with
  its history.
- **Uploads:**
  - hats, shirts and pants (the online **Catalog**);
  - audio (in Studio's **Marketplace**; sounds play as `gb:<id>`);
  - **decals**, pictures for Decal objects (`.png` or `.jpg`, up to 4 MB;
    used as `gb:<id>`);
  - **plugins** (Studio's Marketplace);
  - **games** (Studio's **File > Publish to Guts&Bolts**; they show up under
    **Online Games** on the site's home page).
- **Who owns what.**

### The website

The site also works in a web browser, hosted free on Cloudflare (see
`website/README.md`). The front page shows what Guts&Bolts is. **Enter the
site** (`/app/`) to do what the Player's site pages do:

- sign up and log in (the same accounts as the apps);
- browse games and see who's playing;
- buy from the catalog;
- upload decals, audio, clothes, plugins and games, and rename your games;
- use friends, people, groups and Bolts;
- change your **avatar** (colours, hat, and clothes from the catalog). It's saved
  on the server, so the app and the website always match;
- **staff:** a Staff page to verify people, make staff, give Bolts and ban.

Games published from Studio get a **picture** (Studio takes it from the spawn
point when you publish), shown on the website's game cards. A red number on
**Friends** means friend requests are waiting.

Playing games still happens in the app. The website's **Play** button opens the
app on that game with a `gutsandbolts://play/<game>` link (the Player sets that
up by itself on Windows and Linux, and the Android app has it built in; on a Mac,
open the app yourself). Visitors who aren't signed in pick **Play As Boy** or
**Play As Girl** first and play as a guest.

Visitors who aren't signed in can see every page. Buying, claiming Bolts,
saving an avatar, adding friends, joining groups and uploading pop up *"You need
to log in"* with Sign Up and Log In buttons.

The website talks to the Guts&Bolts server on Cloudflare (see below). Your
password never leaves the browser.

### The Create page

**Configure** a published game on the website (Create > My Games > Configure):
change its name and description, choose **who can play** (*Public*, *Friends
only* or *Private*), upload a **thumbnail** (cropped to 16:9) and a square
**icon**, or upload a new version. Private and friends-only games are hidden
from everyone else: the server won't list them, send them or start servers
for them.

The site's **Create** page has one tab per kind of thing you make:

- **My Games:** every game you made in Studio (on this computer, plus the ones
  you published). Each has a picture and three buttons:
  - **Edit name** renames it. A published game gets the new name on the
    server too.
  - **Open in Studio** opens it in Studio to keep building.
  - **Play** plays it.
  Studio signs a new game with your username when you save it, so it shows up
  here.
- **Decals, Audio, Hats, Shirts, Pants, Plugins:** an upload form (with a
  **Browse...** button to pick the file, and a preview for pictures and
  clothes), then a list of what you've uploaded. Decals and audio show their
  `gb:<id>`, with a **Copy ID** button: paste it into a Decal's **Texture** or a
  Sound's **File** in Studio.

Uploading costs:

- **Verified creators:** free, with no limit. They can set a price, and they
  get 70% of every sale.
- **Everyone else:** a small fee (5 Bolts for decals, 10 for clothes, 20 for
  audio and plugins), 5 uploads a day, and everything they make is free.

Who can make what:

- **Hats** are for Verified creators only. **Shirts and pants** can be made by
  anyone with an account. Guests can only play games.
- **Decals and audio are always free**, even from Verified creators: they're
  free-use assets anyone can put in their games.

The **Staff** page gets a server section where you can search for people and
**Verify** / **Unverify** them with one click. The official account can also
make people **Staff**, give or take Bolts, and ban. Banning asks for a reason
(sexual content, violent extremism, harassment, hate speech, threats, scams,
sharing personal info, exploiting, spam and so on) and an optional note; the
banned player sees them when they try to sign in.

### The Library (sharing models)

Select some objects in Studio and use **File > Publish Selection to Library**.
Give it a name, and pick **Public** (everyone can find and use it) or
**Private** (only you). Verified creators can make as many models public as
they want; everyone else can make **5 public a week** (private ones don't
count). Studio takes a picture of the model for its thumbnail.

Find things in two places:

- **Studio's Toolbox > Library:** everyone's public models, decals and audio,
  with pictures and a search box. Click a model to insert it, a decal to put
  it on the selected part, or a sound to add it.
- **The website's Create > Library tab:** all public models, decals, audio and
  plugins. **Create > Models** lists yours with a Public / Private switch.

### Limiteds, resale and trading

- **Edit item:** on an item's page on the website, its creator (or staff) can
  change its name, description, price, colour, hat shape or clothing picture.
- **Limited items:** only the Guts account can make an item Limited, and only
  its own **accessories and faces** (never shirts, pants, audio, decals or
  models). A Limited has a fixed stock. Each copy gets a number (#1, #2, ...). While there's stock
  left it sells like normal; once it's **sold out**, the only way to get one
  is from another player.
- **Resale:** owners of a Limited copy can put it up for sale on the item's
  page, at any price. The seller gets 70% of the price. The cheapest copies
  show first under **Resellers**.
- **Trading:** press **Trade** on someone's profile, pick up to 4 of your
  Limiteds and up to 4 of theirs, and send the offer. They accept or decline
  on the **Trades** page. The swap only happens if both of you still have
  everything.

### Accessories and faces

Besides hats there are **hair**, **face**, **neck**, **shoulder** and **waist**
accessories. Verified creators make them in **Studio**:

1. Build the accessory (one Model or part).
2. Open **AVATAR > Accessories**. Press **Add mannequin** to get a character to
   try it on, pick the **type**, select your model and press **Move to the
   spot** (it jumps to where that type sits, like the top of the head).
3. Move, turn and stretch it until it looks right, then **Save position**.
4. Give it a name, description and price and press **Upload**. Studio takes a
   picture of it for the catalog.

A worn hat or hair takes the place of the built-in hat.

**Faces** are pictures drawn on the front of the head. Only the Guts account
can make them: on the website, **Create > Faces**, upload a square .png that's
see-through around the eyes and mouth.

### Changing your username

On the website's **Settings** page you can change your username for **1,000
Bolts**. Your old usernames show on your profile under **Past usernames**.
Usernames are never reused: nobody else can take one of your old names, but
you can switch back to it yourself (for the same price). Your password stays
the same.

The Guts account can also set its **join date** there (for example to when the
project really started); it shows on the profile.

### Clothing templates

Shirts and pants can have a **picture**, like classic Roblox clothing. Get the
template (the website's Create page links **shirt template** and **pants
template**; the app's Create page has **Save the template**; they're also in
`assets/templates/`). It's a 585 x 559 picture with a box for every side of the
torso and each arm (shirts) or leg (pants): **R** and **L** are the
character's own right and left. Paint over the boxes, save it as a .png, and
pick it as the **Picture** when you upload the shirt or pants. Anything you
leave see-through shows the clothing's colour.

`tools/make_clothing_template.py` redraws the templates; the layout lives in
`src/scene/PlayerModel.cpp`.

### Finding games: genres, votes and sorting

On the website, a game's **Configure** page lets its creator pick up to 3
**genres** and how many players fit in one server. The **Games** page has a
button for each genre, a search box that also looks at descriptions and
genres, and sorting by **Most played**, **Playing now**, **Top rated**,
**Newest** and **Recently updated**. Once you've played a game you can give it
a thumbs up or down on its page; cards show how liked each game is.

### Badges: Guts&Bolts badges and game badges

There are two kinds of badges:

- **Guts&Bolts badges** (like **Verified** and **Staff**) are for the whole
  platform. Only staff can give them.
- **Game badges** are made by game creators. On the website, open your game's
  **Configure** page, scroll to **Badges**, and make one (name, description,
  colour). Copy its ID and award it from a script in your game:

  ```lua
  local BadgeService = game:GetService("BadgeService")
  game.Players.PlayerAdded:Connect(function(player)
      BadgeService:AwardBadge(player, "badge-1a2b3c4d5e")
  end)
  ```

  Badges are only given in online servers of the published game (the server
  checks that the request comes from that server's host, for someone who's in
  it). Guests can't earn them. `BadgeService:UserHasBadgeAsync(player, id)`
  knows about badges given during the current server.

Profiles show both kinds in separate boxes, and game pages list their badges.

### Signing up and logging in

When you connect to a Guts&Bolts server, the site asks you to **Sign Up** or
**Log In** first.

**No account? Play as Guest.** Guests get a name like *Guest 4821* and can play
every game, on their own or in public and private servers with other people.
They can't chat (the chat box is greyed out: *"Sign up to chat with other
users!"*), make friends, or buy and earn Bolts. The server tells a game's host
who's a guest, so a guest's messages never reach anyone. Signing up any time
keeps playing on the same device.

- **Usernames** are one of a kind and can never be used again, not even after
  someone's banned. They're 3-20 letters and numbers with at most one `_`.
  Online, your name everywhere (games, chat, the site) is your username, so
  nobody can pretend to be you.
- **User numbers** count up: the first person to sign up is #2, then #3, and so
  on. A number is never given out twice.
- **User #1 is Guts**, the official staff account. On the staff computer,
  open *Avatar > Your account > Set a password* (username `Guts`) so you can
  log in as Guts on your other devices too.
- Search for people by name, `@username` or `#number`.
- **Log out** is on the Avatar page (under *Your account*).

How logging in works, and why your password stays safe: your account is a
secret key on your device. When you sign up, the app locks a copy of that key
with your password and stores the locked copy on the server. Logging in on
another device downloads the copy and unlocks it there. The password itself
never leaves your device. The server only gets a scrambled token made from
it, so it can't unlock your key. After 5 wrong passwords, that username is
locked for 10 minutes.

### Email, forgot password and two-step verification

On the website, **Settings** (top right) has your account settings:

- **Email:** add one, and type the code we email you to confirm it.
- **Forgot your password?** (on the Log In page) emails a code. Type it with a
  new password. That browser takes over the account with a new key, and every
  other device is logged out: log in on them again with the new password. (The
  Guts account can't be reset by email.)
- **Two-step verification:** when it's on, logging in on a new device needs your
  password *and* a code from your email (the app asks for the code too).
- **Change password.**

Emails need an email service, because Cloudflare can't send email to anyone
by itself. The easiest is [Brevo](https://www.brevo.com) (free, 300 emails a
day, and you don't need your own domain):

1. Make a Brevo account. Under *Senders*, add the email address the codes
   should come from and confirm it.
2. Under *SMTP & API > API Keys*, make an API key.
3. In the Cloudflare dashboard, open the Worker: *Settings > Variables and
   Secrets*. Add a **Secret** `BREVO_API_KEY` (the key) and a **Text**
   variable `MAIL_FROM` (the sender address from step 1). You can also run
   `npx wrangler secret put BREVO_API_KEY`.

Resend works too: set `RESEND_API_KEY` instead (it needs a domain you own).
Until one is set, the settings page says email isn't switched on yet. For
testing with `wrangler dev`, put `MAIL_DEBUG=1` in a `.dev.vars` file: codes
are printed in the terminal instead of emailed.

### Friends and servers (no IP addresses)

Online games go **through the Guts&Bolts server**. Someone's computer still
runs the game (the host), but everyone, host included, only ever connects to
the Guts&Bolts server, and it passes the game's messages along. So:

- nobody sees anybody else's IP address, and
- nobody has to open ports on their router.

**Play** asks the server for the fullest public server of that game that still
has room. If nobody's playing, you quietly become the host of a new public
server, and the next person to press Play joins you.

**Private servers** get a 6-letter code (it shows in the pause menu). Your
friends can join straight from their Friends list; anyone else needs the
code. Private servers never show up for strangers, and Play never puts
strangers in them.

The **Friends** page has three tabs:

- **Friends:** who's online, who's playing what (with a **Join** button);
- **Requests:** friend requests to accept or decline, and the ones you sent;
- **Add Friends:** search by name.

Profiles also have an **Add Friend** button. Your friends list is only shown to
you, and nothing on the site shows anyone's address or when they were last on.

### People and Groups

When you're connected to a server, the site gets two more pages:

- **People:** search for players by name. Click someone to see their
  **profile**: their badges (with the blue Verified check), the groups they're
  in, and everything they've published.
- **Groups:** communities anyone can make. Making one costs 50 Bolts (free for
  Verified people). Each group has a colour emblem, an "about" text, a
  **shout** (a message pinned at the top), and a **wall** where members can
  post. Groups can be open (anyone joins) or "ask to join" (an admin lets
  people in).
  - The **Owner** can make people Admins, remove them, hand the group to
    someone else, change the settings, or delete the group.
  - **Admins** can shout, let people in, remove Members, and delete posts.
  - Staff can clean up any group.

Every request the apps send is signed with the player's account key, so the
server always knows who is asking. Nobody can spend someone else's Bolts or
pretend to be staff. The server doesn't use encryption (TLS), so treat
everything on it as public: don't upload anything secret.

### Bolts (the currency)

(With a server, your Bolts live on the server instead, and everything below
works the same, just online.)

**Bolts** are the Guts&Bolts currency, like Robux on the old Roblox site. Your
balance shows in the top-right of the site; click it (or **Bolts** in the nav
bar) to open the Bolts page.

- **Earning:**
  - 100 Bolts to start.
  - 25 Bolts every day (click **Claim**).
  - 5 Bolts for every 5 minutes you spend playing games, up to 50 a day.
  - Bolts codes from the staff.
- **Spending:** catalog items can have a price. Staff set it when they make
  the item, and it's part of the item's signature, so nobody can change it.
  Buy an item once and it's yours to wear whenever you like.
- **Bolts codes:** staff make them on the **Staff** page for a player's account
  ID. Each code works once, and only for that account. Players paste it on the
  Bolts page.

Your Bolts are kept in `bolts.json` next to your account key, signed with that
key. Editing the file by hand breaks the signature, and the app resets it. The
same honest limit as above applies: with no central server, someone who changes
the app's code could still cheat on their own computer.

**Setting up the staff account (project owner only):** double-click
`tools/Staff Setup.bat` (Windows) or `tools/Staff Setup.command` (Mac), or run
`python3 install.py --staff`. That makes your computer's account the "Guts"
account, and the Player then shows **Staff** and **Create Item**. Back up the
`account.key` file it tells you about. To make every copy of the game recognise
you, your public ID goes into `src/core/OfficialKey.h`.

> Honest limits: there's no central server yet, so the host of a game checks
> everyone who joins, and players check the host. A modified copy of the game
> could still lie about *other* players in a game it hosts.

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
can see them. Dear ImGui, ImGuizmo, Lua 5.4, nlohmann/json, miniaudio and Monocypher are
downloaded automatically.

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
android/                the Android app (Gradle project around the same C++ Player)
games/                  sample games (copied next to the apps)
catalog/                official catalog items (signed; copied next to the apps)
tools/                  make_sample_games.py
src/
  core/                 AppWindow, Log, Settings, Paths, CrashHandler, Account (keys / signatures)
  renderer/             SceneRenderer + Shaders (HDR, shadows, post-fx), Mesh, Camera, ...
  scene/                Scene tree, Player (R6 rig), Physics, Ragdoll, Particles,
                        Effects, Serializer (save files / undo)
  scripting/            Lua engine and Roblox-style API
  game/                 GameSession (play mode), Hud, Profile, SettingsWindow, Badges, Catalog,
                        TouchControls
  net/                  TCP sockets and multiplayer host / client
  editor/               Studio: Editor, Premades, panels/
  player/               Guts&BoltsPlayer app
```
