cmake_minimum_required(VERSION 3.20)
project(GutsAndBolts VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows resource compilation (embeds the application icon). The -I lets the
# resource compiler resolve "engine.ico" referenced from resources/app.rc.
if(WIN32)
    enable_language(RC)
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I \"${CMAKE_CURRENT_SOURCE_DIR}/resources\"")
endif()

include(FetchContent)

# ImGui (docking branch — includes the full editor UI system)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

# ImGuizmo (move/rotate/scale gizmos drawn over the ImGui viewport).
# SOURCE_SUBDIR points at a non-existent dir so MakeAvailable only downloads the
# sources — it does NOT run ImGuizmo's own CMakeLists (which expects vcpkg).
FetchContent_Declare(
    imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  cmake-build-disabled
)
FetchContent_MakeAvailable(imguizmo)

# Lua 5.4 — the scripting language used by Script objects. The official repo
# has no CMake build, so only download it and compile the core library below.
FetchContent_Declare(
    lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG        v5.4.7
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  cmake-build-disabled
)
FetchContent_MakeAvailable(lua)

file(GLOB LUA_SOURCES "${lua_SOURCE_DIR}/*.c")
list(REMOVE_ITEM LUA_SOURCES
    "${lua_SOURCE_DIR}/lua.c"       # stand-alone interpreter
    "${lua_SOURCE_DIR}/luac.c"      # stand-alone compiler
    "${lua_SOURCE_DIR}/onelua.c"    # amalgamation (would duplicate symbols)
    "${lua_SOURCE_DIR}/ltests.c")   # internal test hooks
add_library(lua_static STATIC ${LUA_SOURCES})
target_include_directories(lua_static PUBLIC ${lua_SOURCE_DIR})
set_target_properties(lua_static PROPERTIES C_STANDARD 99)
if(NOT WIN32)
    target_compile_definitions(lua_static PRIVATE LUA_USE_POSIX)   # safe temp files, etc.
endif()

# JSON (scene files, undo history, copy / paste).
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(json)

find_package(glfw3  REQUIRED)
find_package(GLEW   REQUIRED)
find_package(glm    REQUIRED)
find_package(OpenGL REQUIRED)

# ---------------------------------------------------------------------------
# Dear ImGui + ImGuizmo (UI for both apps)
# ---------------------------------------------------------------------------
add_library(gb_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp
)
target_include_directories(gb_imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imguizmo_SOURCE_DIR}/src
)
target_link_libraries(gb_imgui PUBLIC glfw OpenGL::GL)

# ---------------------------------------------------------------------------
# The engine: scene, physics, scripting, rendering, game session.
# Shared by the editor (GutsAndBolts) and the player app (GutsAndBoltsPlayer).
# ---------------------------------------------------------------------------
add_library(gb_engine STATIC
    src/core/CrashHandler.cpp
    src/core/Log.cpp
    src/core/Settings.cpp
    src/core/Paths.cpp
    src/core/AppWindow.cpp
    src/renderer/Shader.cpp
    src/renderer/Mesh.cpp
    src/renderer/Camera.cpp
    src/renderer/Framebuffer.cpp
    src/renderer/ShadowMap.cpp
    src/renderer/SceneRenderer.cpp
    src/scene/SceneNode.cpp
    src/scene/Scene.cpp
    src/scene/Player.cpp
    src/scene/Physics.cpp
    src/scene/Particles.cpp
    src/scene/Ragdoll.cpp
    src/scene/Effects.cpp
    src/scene/Serializer.cpp
    src/scripting/ScriptEngine.cpp
    src/scripting/LuaTypes.cpp
    src/scripting/LuaInstance.cpp
    src/game/GameSession.cpp
    src/game/Hud.cpp
    src/game/SettingsWindow.cpp
    src/game/Profile.cpp
    src/net/Socket.cpp
    src/net/NetGame.cpp
    src/editor/Theme.cpp
)
target_include_directories(gb_engine PUBLIC src/)
target_link_libraries(gb_engine PUBLIC
    gb_imgui
    glfw
    GLEW::GLEW
    OpenGL::GL
    lua_static
    nlohmann_json::nlohmann_json
)
target_compile_definitions(gb_engine PUBLIC GLM_ENABLE_EXPERIMENTAL)
if(WIN32)
    target_link_libraries(gb_engine PUBLIC dbghelp ws2_32)
endif()

# ---------------------------------------------------------------------------
# GutsAndBolts — the editor (Studio)
# ---------------------------------------------------------------------------
set(EDITOR_SOURCES
    src/main.cpp
    src/Application.cpp
    src/editor/Editor.cpp
    src/editor/Premades.cpp
    src/editor/panels/ViewportPanel.cpp
    src/editor/panels/OutlinerPanel.cpp
    src/editor/panels/PropertiesPanel.cpp
    src/editor/panels/EnvironmentPanel.cpp
    src/editor/panels/ToolboxPanel.cpp
    src/editor/panels/PlayerPanel.cpp
    src/editor/panels/OutputPanel.cpp
    src/editor/panels/ScriptEditorPanel.cpp
)
if(WIN32)
    list(APPEND EDITOR_SOURCES resources/app.rc)
endif()
add_executable(GutsAndBolts ${EDITOR_SOURCES})
target_link_libraries(GutsAndBolts PRIVATE gb_engine)

# ---------------------------------------------------------------------------
# GutsAndBoltsPlayer — the platform app for finding and playing games
# ---------------------------------------------------------------------------
set(PLAYER_SOURCES
    src/player/main.cpp
    src/player/PlayerApp.cpp
)
if(WIN32)
    list(APPEND PLAYER_SOURCES resources/app.rc)
endif()
add_executable(GutsAndBoltsPlayer ${PLAYER_SOURCES})
target_link_libraries(GutsAndBoltsPlayer PRIVATE gb_engine)

# Ship the sample games next to the programs.
foreach(app GutsAndBolts GutsAndBoltsPlayer)
    add_custom_command(TARGET ${app} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${app}>/games"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/games"
                "$<TARGET_FILE_DIR:${app}>/games"
        VERBATIM)
endforeach()

# When building with MinGW, the executable depends on runtime DLLs that live in
# the compiler's bin directory (libstdc++, libgcc, glfw3, glew). Copy them next
# to the binary after each build so it can be launched without a configured PATH.
if(MINGW)
    get_filename_component(MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(RUNTIME_DLLS
        libstdc++-6.dll
        libgcc_s_seh-1.dll
        libwinpthread-1.dll
        glfw3.dll
        glew32.dll
    )
    foreach(dll ${RUNTIME_DLLS})
        if(EXISTS "${MINGW_BIN}/${dll}")
            foreach(app GutsAndBolts GutsAndBoltsPlayer)
            add_custom_command(TARGET ${app} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${MINGW_BIN}/${dll}" "$<TARGET_FILE_DIR:${app}>"
                VERBATIM)
        endforeach()
        endif()
    endforeach()
endif()
