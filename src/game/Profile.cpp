#include "Profile.h"
#include "../core/Paths.h"

#include <nlohmann/json.hpp>
#include <fstream>

namespace {
nlohmann::json vec(const glm::vec3& v) { return {v.x, v.y, v.z}; }
glm::vec3 vec(const nlohmann::json& j, const char* k, glm::vec3 fb) {
    if (!j.contains(k) || !j[k].is_array() || j[k].size() != 3) return fb;
    return {j[k][0].get<float>(), j[k][1].get<float>(), j[k][2].get<float>()};
}
} // namespace

Profile& Profile::get() {
    static Profile p = [] { Profile q; q.load(); return q; }();
    return p;
}

void Profile::load() {
    std::ifstream f(Paths::file("profile.json"));
    if (!f) return;
    nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    if (!j.is_object()) return;
    if (j.contains("name") && j["name"].is_string()) name = j["name"].get<std::string>();
    if (j.contains("hat") && j["hat"].is_number_integer()) hat = (HatStyle)j["hat"].get<int>();
    if (j.contains("recent") && j["recent"].is_array())
        for (auto& r : j["recent"]) if (r.is_string()) recent.push_back(r.get<std::string>());
    colors.head     = vec(j, "head", colors.head);
    colors.torso    = vec(j, "torso", colors.torso);
    colors.leftArm  = vec(j, "leftArm", colors.leftArm);
    colors.rightArm = vec(j, "rightArm", colors.rightArm);
    colors.leftLeg  = vec(j, "leftLeg", colors.leftLeg);
    colors.rightLeg = vec(j, "rightLeg", colors.rightLeg);
}

void Profile::save() const {
    nlohmann::json j = {
        {"name", name}, {"hat", (int)hat},
        {"head", vec(colors.head)}, {"torso", vec(colors.torso)},
        {"leftArm", vec(colors.leftArm)}, {"rightArm", vec(colors.rightArm)},
        {"leftLeg", vec(colors.leftLeg)}, {"rightLeg", vec(colors.rightLeg)},
        {"recent", recent},
    };
    std::ofstream f(Paths::file("profile.json"));
    if (f) f << j.dump(2);
}
