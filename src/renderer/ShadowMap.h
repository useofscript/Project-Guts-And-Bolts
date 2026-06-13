#pragma once
#include <GL/glew.h>

// A depth-only framebuffer used as a shadow map. Render the scene into it from
// the light's point of view, then sample it in the lit shader.
class ShadowMap {
public:
    ShadowMap() = default;
    ~ShadowMap();

    ShadowMap(const ShadowMap&)            = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    void init(int size);
    void bindForWrite() const;       // bind FBO + viewport, clears depth
    void bindForRead(int unit) const;// bind the depth texture to a texture unit

    GLuint depthTexture() const { return m_depth; }
    int    size()         const { return m_size; }

private:
    GLuint m_fbo   = 0;
    GLuint m_depth = 0;
    int    m_size  = 0;
};
