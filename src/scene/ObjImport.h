#pragma once
#include <map>
#include <memory>
#include <string>
#include <glm/glm.hpp>
#include "EditMesh.h"

// Reading .obj models (what Blender and most 3D tools export).
namespace ObjImport {

struct Object {
    std::shared_ptr<EditMesh> mesh;   // points squeezed into the -0.5..0.5 box, like every part
    glm::vec3 center{0.0f};           // where the object sat in the file...
    glm::vec3 size{1.0f};             // ...and how big it was
};

// Every object ("o Name") in the file, by name. Texture coordinates, normals
// and materials are skipped; shapes are smooth-shaded (like Blender's "Shade Smooth").
std::map<std::string, Object> parse(const std::string& text);

} // namespace ObjImport
