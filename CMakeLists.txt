cmake_minimum_required(VERSION 3.20)
project(GutsAndBolts VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows resource compilation (embeds the application icon). The -I lets the
# resource compiler resolve "engine.ico" referenced from resources/app.rc.
if(WIN32)
    enable_language(RC)
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I \"${CMAKE_CURRENT_SOURCE_DIR}/resources\"")
endif()

include(FetchContent)

# GB_MOBILE builds the phone / tablet version of the Player (SDL2 + OpenGL ES 3,
# touch controls). It's on automatically for Android; turning it on for Linux
# lets you try the phone version on a computer.
option(GB_MOBILE "Build the phone/tablet Player (SDL2 + OpenGL ES 3)" OFF)
if(ANDROID)
    set(GB_MOBILE ON)
endif()

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

# miniaudio — sound on every platform, in a single header.
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.25
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  cmake-build-disabled
)
FetchContent_MakeAvailable(miniaudio)

# JSON (scene files, undo history, copy / paste).
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(json)

# Monocypher — small, audited crypto: signs accounts, official badges and
# catalog items so they can't be faked.
FetchContent_Declare(
    monocypher
    GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git
    GIT_TAG        4.0.3
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  cmake-build-disabled
)
FetchContent_MakeAvailable(monocypher)
add_library(monocypher STATIC ${monocypher_SOURCE_DIR}/src/monocypher.c)
target_include_directories(monocypher PUBLIC ${monocypher_SOURCE_DIR}/src)

if(GB_MOBILE)
    # SDL2: window, OpenGL ES context, multi-touch, the Android activity.
    FetchContent_Declare(
        SDL2
        URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    if(ANDROID)
        set(SDL_SHARED ON CACHE BOOL "" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    else()
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
    endif()
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL2)
    if(ANDROID)
        set(GB_SDL_LIBS SDL2::SDL2 SDL2::SDL2main)
    else()
        set(GB_SDL_LIBS SDL2::SDL2-static SDL2::SDL2main)
    endif()

    # glm is header-only; phones don't have it installed.
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        1.0.1
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  cmake-build-disabled
    )
    FetchContent_MakeAvailable(glm)
    add_library(gb_glm INTERFACE)
    target_include_directories(gb_glm INTERFACE ${glm_SOURCE_DIR})
    set(GB_GLM gb_glm)

    if(ANDROID)
        set(GB_GL_LIBS GLESv3 EGL android log)
    else()
        set(GB_GL_LIBS GLESv2 EGL)
    endif()
else()
    find_package(glfw3  REQUIRED)
    find_package(GLEW   REQUIRED)
    find_package(glm    REQUIRED)
    find_package(OpenGL REQUIRED)
    set(GB_GLM glm::glm)
endif()

# ---------------------------------------------------------------------------
# Dear ImGui + ImGuizmo (UI for both apps)
# ---------------------------------------------------------------------------
add_library(gb_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp
)
if(GB_MOBILE)
    target_sources(gb_imgui PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp)
    target_compile_definitions(gb_imgui PUBLIC IMGUI_IMPL_OPENGL_ES3)
else()
    target_sources(gb_imgui PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp)
endif()
target_include_directories(gb_imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imguizmo_SOURCE_DIR}/src
)
if(GB_MOBILE)
    target_link_libraries(gb_imgui PUBLIC ${GB_SDL_LIBS} ${GB_GL_LIBS})
else()
    target_link_libraries(gb_imgui PUBLIC glfw OpenGL::GL)
endif()

# Version.h: which commit this build is (for the update checker).
set(GB_GENERATED "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GB_GENERATED}")
add_custom_target(gb_version ALL
    COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -DOUT=${GB_GENERATED}
            -DVERSION=${PROJECT_VERSION} -P ${CMAKE_SOURCE_DIR}/cmake/GenVersion.cmake
    BYPRODUCTS "${GB_GENERATED}/Version.h"
    COMMENT "Checking version")

# ---------------------------------------------------------------------------
# The engine: scene, physics, scripting, rendering, game session.
# Shared by the editor (GutsAndBolts) and the player app (GutsAndBoltsPlayer).
# ---------------------------------------------------------------------------
add_library(gb_engine STATIC
    src/core/CrashHandler.cpp
    src/core/Log.cpp
    src/core/Settings.cpp
    src/core/Paths.cpp
    src/core/Audio.cpp
    src/core/UpdateChecker.cpp
    src/core/Account.cpp
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
    src/scene/RigidBodies.cpp
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
    src/game/UpdateToast.cpp
    src/game/Badges.cpp
    src/game/Catalog.cpp
    src/game/TouchControls.cpp
    src/net/Socket.cpp
    src/net/NetGame.cpp
    src/editor/Theme.cpp
)
if(GB_MOBILE)
    target_sources(gb_engine PRIVATE src/core/AppWindowSDL.cpp)
    target_compile_definitions(gb_engine PUBLIC GB_MOBILE GB_GLES)
else()
    target_sources(gb_engine PRIVATE src/core/AppWindow.cpp)
endif()
target_include_directories(gb_engine PUBLIC src/ ${miniaudio_SOURCE_DIR} ${GB_GENERATED})
add_dependencies(gb_engine gb_version)
find_package(Threads REQUIRED)
target_link_libraries(gb_engine PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(APPLE)
    target_link_libraries(gb_engine PUBLIC "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
endif()
target_link_libraries(gb_engine PUBLIC
    gb_imgui
    ${GB_GLM}
    lua_static
    nlohmann_json::nlohmann_json
    monocypher
)
if(NOT GB_MOBILE)
    target_link_libraries(gb_engine PUBLIC glfw GLEW::GLEW OpenGL::GL)
endif()
target_compile_definitions(gb_engine PUBLIC GLM_ENABLE_EXPERIMENTAL)
if(WIN32)
    target_link_libraries(gb_engine PUBLIC dbghelp ws2_32 bcrypt)
endif()

if(GB_MOBILE)
    # Phones and tablets get the Player only. On Android it's a library that
    # SDL's Java activity loads (it must be called "main").
    set(PLAYER_SOURCES src/player/main.cpp src/player/PlayerApp.cpp)
    if(ANDROID)
        add_library(main SHARED ${PLAYER_SOURCES})
        target_link_libraries(main PRIVATE gb_engine)
    else()
        add_executable(GutsAndBoltsPlayer ${PLAYER_SOURCES})
        target_link_libraries(GutsAndBoltsPlayer PRIVATE gb_engine)
        add_custom_command(TARGET GutsAndBoltsPlayer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/games"
                    "$<TARGET_FILE_DIR:GutsAndBoltsPlayer>/games"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/catalog"
                    "$<TARGET_FILE_DIR:GutsAndBoltsPlayer>/catalog"
            VERBATIM)
    endif()
    return()
endif()

# ---------------------------------------------------------------------------
# GutsAndBolts — the editor (Studio)
# ---------------------------------------------------------------------------
set(EDITOR_SOURCES
    src/main.cpp
    src/Application.cpp
    src/editor/Editor.cpp
    src/editor/Premades.cpp
    src/editor/TeamCreate.cpp
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

# Ship the sample games and official catalog items next to the programs.
foreach(app GutsAndBolts GutsAndBoltsPlayer)
    add_custom_command(TARGET ${app} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${app}>/games"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/games"
                "$<TARGET_FILE_DIR:${app}>/games"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${app}>/catalog"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/catalog"
                "$<TARGET_FILE_DIR:${app}>/catalog"
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
