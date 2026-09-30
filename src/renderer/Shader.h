#pragma once
#include "GL.h"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

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
    void setVec2 (const char* name, const glm::vec2& v) const;
    void setVec3 (const char* name, const glm::vec3& v) const;
    void setVec4 (const char* name, const glm::vec4& v) const;
    void setFloat(const char* name, float v)             const;
    void setInt  (const char* name, int v)               const;
    void setBool (const char* name, bool v)              const;
    void setVec4Array(const char* name, const glm::vec4* v, int count) const;    void setVec3Array(const char* n, const glm::vec3* v, int count) const;

private:
    GLint loc(const char* name) const;   // cached glGetUniformLocation

    GLuint m_id = 0;
    mutable std::unordered_map<std::string, GLint> m_locs;
    static GLuint compile(GLenum type, const char* src);
};
