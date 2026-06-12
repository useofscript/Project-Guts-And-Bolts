#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

class Mesh {
public:
    Mesh() = default;
    Mesh(const std::vector<Vertex>& verts, const std::vector<uint32_t>& indices);
    ~Mesh();

    Mesh(const Mesh&)            = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& o) noexcept;
    Mesh& operator=(Mesh&& o) noexcept;

    void draw() const;
    int  indexCount() const { return m_indexCount; }

private:
    void upload(const std::vector<Vertex>& verts, const std::vector<uint32_t>& indices);
    void release();

    GLuint m_vao = 0, m_vbo = 0, m_ebo = 0;
    int    m_indexCount = 0;
};
