#include "Shader.h"
#include <stdexcept>
#include <string>
#include <glm/gtc/type_ptr.hpp>

Shader::Shader(const char* vertSrc, const char* fragSrc) {
    GLuint vert = compile(GL_VERTEX_SHADER,   vertSrc);
    GLuint frag = compile(GL_FRAGMENT_SHADER, fragSrc);

    m_id = glCreateProgram();
    glAttachShader(m_id, vert);
    glAttachShader(m_id, frag);
    glLinkProgram(m_id);

    GLint ok;
    glGetProgramiv(m_id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(m_id, 1024, nullptr, log);
        glDeleteShader(vert);
        glDeleteShader(frag);
        throw std::runtime_error(std::string("Shader link error: ") + log);
    }
    glDeleteShader(vert);
    glDeleteShader(frag);
}

Shader::~Shader() { if (m_id) glDeleteProgram(m_id); }

Shader::Shader(Shader&& o) noexcept : m_id(o.m_id) { o.m_id = 0; }
Shader& Shader::operator=(Shader&& o) noexcept {
    if (this != &o) { if (m_id) glDeleteProgram(m_id); m_id = o.m_id; o.m_id = 0; }
    return *this;
}

void Shader::bind()   const { glUseProgram(m_id); }
void Shader::unbind() const { glUseProgram(0); }

void Shader::setMat4 (const char* n, const glm::mat4& v) const { glUniformMatrix4fv(glGetUniformLocation(m_id,n),1,GL_FALSE,glm::value_ptr(v)); }
void Shader::setMat3 (const char* n, const glm::mat3& v) const { glUniformMatrix3fv(glGetUniformLocation(m_id,n),1,GL_FALSE,glm::value_ptr(v)); }
void Shader::setVec3 (const char* n, const glm::vec3& v) const { glUniform3fv(glGetUniformLocation(m_id,n),1,glm::value_ptr(v)); }
void Shader::setVec4 (const char* n, const glm::vec4& v) const { glUniform4fv(glGetUniformLocation(m_id,n),1,glm::value_ptr(v)); }
void Shader::setFloat(const char* n, float v)             const { glUniform1f (glGetUniformLocation(m_id,n),v); }
void Shader::setInt  (const char* n, int v)               const { glUniform1i (glGetUniformLocation(m_id,n),v); }
void Shader::setBool (const char* n, bool v)              const { glUniform1i (glGetUniformLocation(m_id,n),(int)v); }

GLuint Shader::compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, 1024, nullptr, log);
        glDeleteShader(s);
        throw std::runtime_error(std::string("Shader compile error: ") + log);
    }
    return s;
}
