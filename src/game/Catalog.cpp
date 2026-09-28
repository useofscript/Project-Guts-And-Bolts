#include "Catalog.h"
#include "Profile.h"
#include "../core/Account.h"
#include "../core/Paths.h"
#include "Version.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>

namespace Catalog {

namespace {
namespace fs = std::filesystem;
using json = nlohmann::json;

int byte(float v) { return (int)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f); }

// The exact text that gets signed (keys are sorted, colours are whole numbers,
// so it comes out the same on every computer).
json body(const Item& it) {
    return {{"id", it.id}, {"name", it.name}, {"description", it.description}, {"type", typeName(it.type)},
            {"hat", (int)it.hat}, {"color", {byte(it.color.r), byte(it.color.g), byte(it.color.b)}},
            {"created", it.created}};
}
std::string message(const Item& it) { return "gb-item:" + body(it).dump(); }

fs::path appFolder() { return Paths::appFolder() / "catalog"; }
fs::path repoFolder() { return fs::path(GB_SOURCE_DIR) / "catalog"; }

bool parse(const json& j, Item& it) {
    if (!j.is_object()) return false;
    it.id = j.value("id", std::string());
    it.name = j.value("name", std::string());
    it.description = j.value("description", std::string());
    std::string t = j.value("type", std::string());
    it.type = Type::Count;
    for (int i = 0; i < (int)Type::Count; ++i) if (t == typeName((Type)i)) it.type = (Type)i;
    it.hat = (HatStyle)std::clamp(j.value("hat", 0), 0, 3);
    if (j.contains("color") && j["color"].is_array() && j["color"].size() == 3)
        it.color = {j["color"][0].get<int>() / 255.0f, j["color"][1].get<int>() / 255.0f, j["color"][2].get<int>() / 255.0f};
    it.created = j.value("created", 0LL);
    it.signature = j.value("sig", std::string());
    return !it.id.empty() && it.type != Type::Count;
}

std::string slug(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (std::isalnum((unsigned char)c)) out += (char)std::tolower((unsigned char)c);
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    if (out.size() > 24) out.resize(24);
    return out.empty() ? "item" : out;
}
} // namespace

const char* typeName(Type t) {
    switch (t) {
        case Type::Hat:   return "Hat";
        case Type::Shirt: return "Shirt";
        case Type::Pants: return "Pants";
        default:          return "?";
    }
}

std::vector<Item> load() {
    std::vector<Item> out;
    std::string official = Account::officialId();
    std::error_code ec;
    if (official.empty() || !fs::is_directory(appFolder(), ec)) return out;
    for (const auto& e : fs::directory_iterator(appFolder(), ec)) {
        if (e.path().extension() != ".gbitem") continue;
        std::ifstream f(e.path());
        json j = json::parse(f, nullptr, false);
        Item it;
        if (!parse(j, it)) continue;
        if (!Account::verify(official, message(it), it.signature)) continue;   // not made by staff
        out.push_back(it);
    }
    std::sort(out.begin(), out.end(), [](const Item& a, const Item& b) { return a.created > b.created; });
    return out;
}

bool create(Item& it, std::string& msg) {
    if (!Account::iAmStaff()) { msg = "Only the staff account can make catalog items."; return false; }
    if (it.name.empty()) { msg = "Give the item a name."; return false; }
    it.created = (long long)std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::system_clock::now().time_since_epoch()).count();
    it.id = slug(it.name) + "-" + Account::randomHex(4);
    it.signature = Account::sign(message(it));
    json j = body(it);
    j["sig"] = it.signature;

    std::error_code ec;
    fs::create_directories(appFolder(), ec);
    std::ofstream f(appFolder() / (it.id + ".gbitem"));
    if (!f) { msg = "Couldn't save the item."; return false; }
    f << j.dump(2);
    f.close();
    msg = "Made \"" + it.name + "\"!";
    if (fs::is_directory(fs::path(GB_SOURCE_DIR), ec)) {
        fs::create_directories(repoFolder(), ec);
        std::ofstream r(repoFolder() / (it.id + ".gbitem"));
        if (r) {
            r << j.dump(2);
            msg += " It's also in " + repoFolder().string() + " - commit that file to give it to everyone.";
        }
    }
    return true;
}

bool remove(const Item& it, std::string& msg) {
    if (!Account::iAmStaff()) { msg = "Only the staff account can remove catalog items."; return false; }
    std::error_code ec;
    fs::remove(appFolder() / (it.id + ".gbitem"), ec);
    fs::remove(repoFolder() / (it.id + ".gbitem"), ec);
    msg = "Removed \"" + it.name + "\".";
    return true;
}

void wear(const Item& it) {
    Profile& me = Profile::get();
    if (std::find(me.inventory.begin(), me.inventory.end(), it.id) == me.inventory.end()) me.inventory.push_back(it.id);
    switch (it.type) {
        case Type::Hat:
            me.hat = it.hat;
            me.hatColor = it.color;
            break;
        case Type::Shirt:
            me.colors.torso = me.colors.leftArm = me.colors.rightArm = it.color;
            break;
        case Type::Pants:
            me.colors.leftLeg = me.colors.rightLeg = it.color;
            break;
        default: break;
    }
    // Only one item of each type at a time.
    std::vector<std::string> keep;
    for (const std::string& w : me.wearing) {
        bool sameType = false;
        for (const Item& other : load()) if (other.id == w && other.type == it.type) sameType = true;
        if (!sameType && w != it.id) keep.push_back(w);
    }
    keep.push_back(it.id);
    me.wearing = keep;
    me.save();
}

bool isWearing(const Item& it) {
    const auto& w = Profile::get().wearing;
    return std::find(w.begin(), w.end(), it.id) != w.end();
}

} // namespace Catalog
