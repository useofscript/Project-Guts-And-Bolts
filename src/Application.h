#pragma once
#include <memory>

struct GLFWwindow;
class Scene;
class Editor;

class Application {
public:
    Application();
    ~Application();
    void run();

private:
    void initWindow();
    void initGL();
    void initImGui();
    void cleanup();

    GLFWwindow* m_window = nullptr;
    int m_width = 1280, m_height = 720;

    std::unique_ptr<Scene>  m_scene;
    std::unique_ptr<Editor> m_editor;
};
