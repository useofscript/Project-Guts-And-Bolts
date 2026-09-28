#include "PlayerModel.h"
#include "EditMesh.h"
#include "ObjImport.h"
#include "SceneNode.h"
#include "PlayerObj.h"   // generated: the bytes of assets/models/player.obj

#include <map>

namespace PlayerModel {

namespace {

constexpr float kScale = 0.5f;   // the model is Roblox-sized; our characters are half that

struct Part {
    std::shared_ptr<EditMesh> mesh;
    std::shared_ptr<Mesh>     gpu;   // made the first time it's drawn
    glm::vec3 position{0.0f}, size{1.0f};
};

std::map<std::string, Part>& parts() {
    static std::map<std::string, Part> p = [] {
        std::map<std::string, Part> out;
        std::string text(reinterpret_cast<const char*>(kPlayerObj), kPlayerObjSize);
        for (auto& [name, o] : ObjImport::parse(text)) {
            std::string rig = name;   // "Left_Arm" in the file -> "Left Arm" on the rig
            for (char& c : rig) if (c == '_') c = ' ';
            out[rig] = {o.mesh, nullptr, o.center * kScale, o.size * kScale};
        }
        return out;
    }();
    return p;
}

} // namespace

std::shared_ptr<EditMesh> mesh(const std::string& rigPart) {
    auto it = parts().find(rigPart);
    return it == parts().end() ? nullptr : it->second.mesh;
}

bool placement(const std::string& rigPart, glm::vec3& position, glm::vec3& size) {
    auto it = parts().find(rigPart);
    if (it == parts().end()) return false;
    position = it->second.position;
    size = it->second.size;
    return true;
}

const char* nameOf(const EditMesh* m) {
    if (!m) return nullptr;
    for (auto& [name, p] : parts()) if (p.mesh.get() == m) return name.c_str();
    return nullptr;
}

bool apply(SceneNode& node) {
    auto it = parts().find(node.name);
    if (it == parts().end()) return false;
    Part& p = it->second;
    node.primitiveType = PrimitiveType::Mesh;
    node.editMesh = p.mesh;
    if (!p.gpu) {
        MeshEdit::refresh(node);
        p.gpu = node.mesh;
    }
    node.mesh = p.gpu;
    return true;
}

} // namespace PlayerModel
