#include "Textures.h"
#include "GL.h"
#include "../core/Log.h"
#include "../core/Paths.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>   // (Studio saves game pictures as PNG with it)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include <stb_image.h>

#include <chrono>
#include <filesystem>
#include <map>

namespace Textures {

namespace {

struct Entry {
    unsigned tex = 0;
    int      w = 0, h = 0;
    bool     broken = false;      // tried and it isn't a picture
    double   retryAt = 0.0;       // not downloaded yet: look again later
};

std::map<std::string, Entry>& cache() {
    static std::map<std::string, Entry> c;
    return c;
}

double now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::filesystem::path locate(const std::string& id) {
    if (id.rfind("gb:", 0) == 0) return Paths::downloaded(id.substr(3));
    std::error_code ec;
    std::filesystem::path p = Paths::gamesFolder() / id;
    if (std::filesystem::exists(p, ec)) return p;
    p = id;
    if (std::filesystem::exists(p, ec)) return p;
    return {};
}

void load(const std::string& id, Entry& e) {
    std::filesystem::path p = locate(id);
    if (p.empty()) { e.retryAt = now() + 1.0; return; }   // maybe it's still downloading
    stbi_set_flip_vertically_on_load(1);                   // GL wants the bottom row first
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load(p.string().c_str(), &w, &h, &n, 4);
    if (!px) {
        e.broken = true;
        Log::warn("\"" + id + "\" isn't a picture Guts and Bolts can read (use PNG or JPG).");
        return;
    }
    glGenTextures(1, &e.tex);
    glBindTexture(GL_TEXTURE_2D, e.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(px);
    e.w = w;
    e.h = h;
}

} // namespace

unsigned get(const std::string& id) {
    if (id.empty()) return 0;
    Entry& e = cache()[id];
    if (!e.tex && !e.broken && now() >= e.retryAt) load(id, e);
    return e.tex;
}

bool size(const std::string& id, int& w, int& h) {
    if (!get(id)) return false;
    const Entry& e = cache()[id];
    w = e.w;
    h = e.h;
    return true;
}

void forget(const std::string& id) {
    auto it = cache().find(id);
    if (it == cache().end()) return;
    if (it->second.tex) glDeleteTextures(1, &it->second.tex);
    cache().erase(it);
}

void clear() {
    for (auto& [id, e] : cache())
        if (e.tex) glDeleteTextures(1, &e.tex);
    cache().clear();
}

} // namespace Textures
