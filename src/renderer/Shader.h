#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>

class Shader {
public:
    Shader() = default;
    Shader(const char* vertSrc, const char* fragSrc);
    ~Shader();

    Shader(const Shader&)            = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& o) noexcept;
    Shader& operator=(Shader&& o) noexcept;

    void bind()   const;
    void unbind() const;

    void setMat4 (const char* name, const glm::mat4& v) const;
    void setMat3 (const char* name, const glm::mat3& v) const;
    void setVec3 (const char* name, const glm::vec3& v) const;
    void setVec4 (const char* name, const glm::vec4& v) const;
    void setFloat(const char* name, float v)             const;
    void setInt  (const char* name, int v)               const;
    void setBool (const char* name, bool v)              const;

private:
    GLuint m_id = 0;
    static GLuint compile(GLenum type, const char* src);
};
