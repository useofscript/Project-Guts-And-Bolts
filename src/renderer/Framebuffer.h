#pragma once
#include <GL/glew.h>

class Framebuffer {
public:
    Framebuffer()  = default;
    ~Framebuffer() { destroy(); }

    Framebuffer(const Framebuffer&)            = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    void resize(int w, int h);
    void bind()   const;
    void unbind() const;

    GLuint colorTexture() const { return m_color; }
    int width()  const { return m_w; }
    int height() const { return m_h; }

private:
    void create(int w, int h);
    void destroy();

    GLuint m_fbo   = 0;
    GLuint m_color = 0;
    GLuint m_depth = 0;
    int    m_w = 0, m_h = 0;
};
