cmake_minimum_required(VERSION 3.20)
project(GutsAndBolts VERSION 0.1.0 LANGUAGES CXX)

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

find_package(glfw3  REQUIRED)
find_package(GLEW   REQUIRED)
find_package(glm    REQUIRED)
find_package(OpenGL REQUIRED)

set(ENGINE_SOURCES
    src/main.cpp
    src/core/CrashHandler.cpp
    src/Application.cpp
    src/renderer/Shader.cpp
    src/renderer/Mesh.cpp
    src/renderer/Camera.cpp
    src/renderer/Framebuffer.cpp
    src/scene/SceneNode.cpp
    src/scene/Scene.cpp
    src/editor/Editor.cpp
    src/editor/Theme.cpp
    src/editor/panels/ViewportPanel.cpp
    src/editor/panels/OutlinerPanel.cpp
    src/editor/panels/PropertiesPanel.cpp
    src/editor/panels/EnvironmentPanel.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp
)

if(WIN32)
    list(APPEND ENGINE_SOURCES resources/app.rc)
endif()

add_executable(GutsAndBolts ${ENGINE_SOURCES})

target_include_directories(GutsAndBolts PRIVATE
    src/
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imguizmo_SOURCE_DIR}/src
)

target_link_libraries(GutsAndBolts PRIVATE
    glfw
    GLEW::GLEW
    OpenGL::GL
)

if(WIN32)
    target_link_libraries(GutsAndBolts PRIVATE dbghelp)
endif()

target_compile_definitions(GutsAndBolts PRIVATE GLM_ENABLE_EXPERIMENTAL)

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
            add_custom_command(TARGET GutsAndBolts POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${MINGW_BIN}/${dll}" "$<TARGET_FILE_DIR:GutsAndBolts>"
                VERBATIM)
        endif()
    endforeach()
endif()
