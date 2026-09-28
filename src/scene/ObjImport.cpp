#include "ObjImport.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace ObjImport {

std::map<std::string, Object> parse(const std::string& text) {
    std::vector<glm::vec3> points;                                   // every "v" in the file (indices are global)
    std::vector<std::pair<std::string, std::vector<std::vector<long>>>> objects;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;
        if (tag == "v") {
            glm::vec3 p(0.0f);
            ls >> p.x >> p.y >> p.z;
            points.push_back(p);
        } else if (tag == "o" || tag == "g") {
            std::string name;
            std::getline(ls >> std::ws, name);
            if (tag == "g" && !objects.empty() && objects.back().second.empty()) objects.back().first = name;
            else objects.push_back({name, {}});
        } else if (tag == "f") {
            if (objects.empty()) objects.push_back({"Object", {}});
            std::vector<long> face;
            std::string corner;
            while (ls >> corner) {
                long i = std::strtol(corner.c_str(), nullptr, 10);   // "12/5/7" -> 12
                if (i < 0) i = (long)points.size() + i + 1;           // negative = counted from the end
                face.push_back(i - 1);
            }
            if (face.size() >= 3) objects.back().second.push_back(std::move(face));
        }
    }

    std::map<std::string, Object> out;
    for (auto& [name, faces] : objects) {
        if (faces.empty()) continue;
        // This object's own points, renumbered from 0.
        std::map<long, uint32_t> remap;
        auto mesh = std::make_shared<EditMesh>();
        mesh->smooth = true;
        for (const auto& f : faces) {
            std::vector<uint32_t> nf;
            for (long i : f) {
                if (i < 0 || i >= (long)points.size()) continue;
                auto [it, fresh] = remap.try_emplace(i, (uint32_t)mesh->verts.size());
                if (fresh) mesh->verts.push_back(points[(size_t)i]);
                nf.push_back(it->second);
            }
            if (nf.size() >= 3) mesh->faces.push_back(std::move(nf));
        }
        if (mesh->faces.empty()) continue;
        glm::vec3 lo(1e30f), hi(-1e30f);
        for (const auto& p : mesh->verts) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
        Object o;
        o.center = (lo + hi) * 0.5f;
        o.size = hi - lo;
        glm::vec3 div = glm::max(o.size, glm::vec3(1e-6f));   // a flat object stays flat
        for (auto& p : mesh->verts) p = (p - o.center) / div;
        o.mesh = std::move(mesh);
        out[name] = std::move(o);
    }
    return out;
}

} // namespace ObjImport
