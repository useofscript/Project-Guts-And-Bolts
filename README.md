# GameEngine

A small Roblox-Studio-style OpenGL scene editor, built on GLFW, GLEW, GLM, Dear
ImGui (docking branch) and ImGuizmo. The window has a top **toolbar**, a bottom
**status/help bar**, and three docked panels:

- **Viewport** — renders the scene to an off-screen framebuffer with a lit
  shader, an origin grid and coloured axes. Click an object to select it and
  drag the move / rotate / scale gizmo to transform it.
- **Outliner** — scene hierarchy; click to select, right-click for
  hide/show/delete.
- **Properties** — inspector for the selected node: name, visibility,
  transform (position / rotation / scale) and base colour.

Add primitives from the toolbar (`+ Cube`, `+ Sphere`, …) or the **Add** menu.

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
./build/GameEngine.exe
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
  Application.*        window / GL / ImGui bootstrap and main loop
  renderer/            Shader, Mesh, Camera, Framebuffer, Primitives
  scene/               Scene graph (Scene, SceneNode, Transform)
  editor/
    Editor.*           dockspace, menu bar, toolbar, status bar, shortcuts
    EditorState.h      shared UI state (active tool, gizmo space, snapping)
    panels/            Viewport (gizmos + picking), Outliner, Properties
```
