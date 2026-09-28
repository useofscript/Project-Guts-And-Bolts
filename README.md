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
| Focus on selection | `F` |
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

To keep everyone's accounts, Bolts, badges and uploads in one place, run the
**Guts&Bolts server** on a computer:

1. **Start it.** Double-click `tools/Start Server.bat` (Windows) or
   `tools/Start Server.command` (Mac), or run `python3 install.py --server`.
   A window opens and says which address and port (7780) it's on. Keep that
   window open while people play. Everything it stores goes in the
   `server_data` folder; back that folder up to keep it safe.
2. **Connect the apps.** On the site, click the server button on the banner
   (on phones, **Offline** in the nav bar). In Studio, use **File >
   Guts&Bolts Server...**. Type the server's address, like `192.168.1.20`
   (same Wi-Fi) or `myserver.com:7780`, and click **Connect**. Leave it empty
   to play offline.
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
  - **plugins** (Studio's Marketplace);
  - **games** (Studio's **File > Publish to Guts&Bolts**; they show up under
    **Online Games** on the site's home page).
- **Who owns what.**

On the site, the **Create** page uploads hats, shirts, pants, audio and
plugins:

- **Verified creators:** free, with no limit. They can set a price, and they
  get 70% of every sale.
- **Everyone else:** a small fee (10 Bolts for clothes, 20 for audio and
  plugins), 5 uploads a day, and everything they make is free.

The **Staff** page gets a server section where you can search for people and
**Verify** / **Unverify** them with one click. The official account can also
make people **Staff**, give or take Bolts, and ban.

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
