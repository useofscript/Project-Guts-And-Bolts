#pragma once
#include "Primitives.h"
#include "../scene/SceneNode.h"
#include <memory>

// One shared GPU mesh per primitive shape. Every Cube in the scene draws the
// same vertex buffers, which keeps loading, undo and cloning cheap.
namespace MeshLibrary {

inline std::shared_ptr<Mesh>& slot(PrimitiveType type) {
    static std::shared_ptr<Mesh> meshes[8];
    return meshes[(int)type];
}

inline std::shared_ptr<Mesh> get(PrimitiveType type) {
    if (type == PrimitiveType::None) return nullptr;
    auto& m = slot(type);
    if (!m) {
        switch (type) {
            case PrimitiveType::Cube:     m = Primitives::createCube();     break;
            case PrimitiveType::Sphere:   m = Primitives::createSphere();   break;
            case PrimitiveType::Plane:    m = Primitives::createPlane();    break;
            case PrimitiveType::Cylinder: m = Primitives::createCylinder(); break;
            default: break;
        }
    }
    return m;
}

// Release the GPU buffers — must run while the GL context is still alive.
inline void clear() {
    for (int i = 0; i < 8; ++i) slot((PrimitiveType)i).reset();
}

} // namespace MeshLibrary
