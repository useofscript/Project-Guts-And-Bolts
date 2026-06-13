# Guts and Bolts

A small Roblox-Studio-style OpenGL scene editor, built on GLFW, GLEW, GLM, Dear
ImGui (docking branch) and ImGuizmo. The window has a top **toolbar**, a bottom
**status/help bar**, and docked panels:

- **Viewport** — renders the scene to an off-screen framebuffer with a
  procedural sky, a sun-driven lit shader, **shadow mapping**, distance fog, an
  origin grid and coloured axes. Click an object to select it and drag the
  move / rotate / scale gizmo to transform it.
- **Outliner** — scene hierarchy; click to select, right-click for
  hide/show/delete.
- **Toolbox** — a palette of insertable parts (Cube / Sphere / Plane /
  Cylinder).
- **Properties** — inspector for the selected node: name, visibility,
  transform, **Appearance** (colour, material — Plastic / Metal / Neon / Wood,
  transparency) and **Behavior** (anchored, can-collide, cast-shadow).
- **Environment** — lighting & atmosphere (like Roblox's Lighting service):
  sun direction/colour/intensity, **cast shadows**, ambient sky-light, sky
  gradient colours and fog, with one-click **Day / Sunset / Night / Overcast**
  presets.
- **Player** — the character's Humanoid properties (walk speed, jump power,
  health). The scene ships with a Roblox-style R6 character (HumanoidRootPart,
  Torso, Head with a smiley face, two arms and two legs).

Add primitives from the toolbar (`+ Cube`, `+ Sphere`, …) or the **Add** menu.

### Playtest mode

Press **Play** (toolbar) or **F5** to enter playtest mode, then drive the
character with **WASD** and jump with **Space** (camera-relative movement,
gravity, jumping and ground collision). Press **F5**/**Esc** or **Stop** to
return to editing — the character resets to its spawn.

### Controls

| Action | Input |
| --- | --- |
| Select tool | `Q` |
| Move / Rotate / Scale tool | `W` / `E` / `R` |
| Orbit camera | Middle-mouse drag |
| Pan camera | Shift + middle-mouse drag |
| Zoom | Scroll wheel |
| Pick object | Left-click in viewport |
| Focus camera on selection | `F` |
| Duplicate selection | `Ctrl+D` |
| Delete selection | `Del` |
| Clear selection | `Esc` |
| Start / stop playtest | `F5` |
| Move character (in playtest) | `W` `A` `S` `D` |
| Jump (in playtest) | `Space` |

The toolbar also toggles gizmo orientation (**Local** / **World**) and grid
**Snap**.

## Building

Dependencies are resolved with `find_package` (glfw3, GLEW, glm, OpenGL); Dear
ImGui is fetched automatically via `FetchContent`.

### Windows (MinGW / MSYS2)

The MSYS2 `mingw64` environment provides g++, ninja and all three libraries.
The required runtime DLLs are copied next to the executable automatically after
each build.

```sh
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="C:/msys64/mingw64"
cmake --build build
./build/GutsAndBolts.exe
```

If `cmake`/`ninja`/`g++` are not on your `PATH`, prepend the MSYS2 bin
directory first: `export PATH="/c/msys64/mingw64/bin:$PATH"` (or the PowerShell
equivalent).

### Other platforms

Any toolchain works as long as glfw3, GLEW and glm are discoverable by
`find_package` (e.g. via a package manager or `vcpkg` toolchain file):

```sh
cmake -S . -B build
cmake --build build
```

## Layout

```
src/
  core/                CrashHandler (symbolized backtrace on fault, Windows)
  Application.*        window / GL / ImGui bootstrap and main loop
  renderer/            Shader, Mesh, Camera, Framebuffer, ShadowMap, Primitives
  scene/               Scene graph (Scene, SceneNode, Transform), Environment,
                       Player (R6 rig + Humanoid)
  editor/
    Editor.*           dockspace, menu bar, toolbar, status bar, shortcuts, play
    EditorState.h      shared UI state (active tool, gizmo space, snapping)
    Theme.*            cohesive dark theme + system font loading
    panels/            Viewport (sky/shadows/fog, gizmos, picking), Outliner,
                       Toolbox, Properties, Environment, Player
```
