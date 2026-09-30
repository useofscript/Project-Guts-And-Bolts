#include "Settings.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <string>
#include "Paths.h"

namespace {
std::string settingsFile() { return Paths::file("settings.json").string(); }
}

GraphicsSettings& GraphicsSettings::get() {
    static GraphicsSettings s;
    return s;
}

void GraphicsSettings::applyPreset(int q) {
    quality = q;
    switch (q) {
        case Low:
            shadowRes = 1024; shadowQuality = 0; ssao = false; bloom = false;
            fxaa = false; postFx = true; renderScale = 0.75f; maxLights = 8; waterQuality = 0;
            break;
        case Medium:
            shadowRes = 2048; shadowQuality = 1; ssao = false; bloom = true;
            fxaa = true; postFx = true; renderScale = 1.0f; maxLights = 16; waterQuality = 1;
            break;
        case High:
            shadowRes = 2048; shadowQuality = 2; ssao = true; bloom = true;
            fxaa = true; postFx = true; renderScale = 1.0f; maxLights = 32; waterQuality = 2;
            break;
        case Ultra:
            shadowRes = 4096; shadowQuality = 2; ssao = true; bloom = true;
            fxaa = true; postFx = true; renderScale = 1.0f; maxLights = 32; waterQuality = 3;
            break;
        default: break;
    }
}

void GraphicsSettings::load() {
    std::ifstream f(settingsFile());
#ifdef GB_MOBILE
    if (!f) {
        // First run on a phone: lighter settings so it stays smooth and cool.
        applyPreset(Medium);
        renderScale = 0.8f;
        showFps = false;
        checkUpdates = false;
        return;
    }
#endif
    if (!f) return;
    nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    if (!j.is_object()) return;
    auto rd = [&](const char* k, auto& v) {
        if (j.contains(k)) try { v = j[k].get<std::decay_t<decltype(v)>>(); } catch (...) {}
    };
    rd("vsync", vsync); rd("fpsCap", fpsCap); rd("showFps", showFps);
    rd("quality", quality); rd("shadowRes", shadowRes); rd("shadowQuality", shadowQuality);
    rd("ssao", ssao); rd("bloom", bloom); rd("fxaa", fxaa); rd("postFx", postFx);
    rd("renderScale", renderScale); rd("maxLights", maxLights); rd("allowGore", allowGore); rd("checkUpdates", checkUpdates);
    rd("touchControls", touchControls); rd("touchSize", touchSize);
    rd("shiftLockSwitch", shiftLockSwitch); rd("mouseSensitivity", mouseSensitivity); rd("invertCamera", invertCamera);
    rd("volume", volume); rd("fullscreen", fullscreen);
    rd("waterQuality", waterQuality); rd("graphicsApi", graphicsApi);
}

const char* const* GraphicsSettings::apiNames() {
#ifdef GB_GLES
    static const char* const names[] = {"Auto (best for this device)", "OpenGL ES 3.2", "OpenGL ES 3.1",
                                        "OpenGL ES 3.0 (safe mode)"};
#else
    static const char* const names[] = {"Auto (best for this computer)", "OpenGL 4.6 (newest)", "OpenGL 4.3",
                                        "OpenGL 4.1 (safe mode)"};
#endif
    return names;
}

std::string& GraphicsSettings::activeApi() {
    static std::string s;
    return s;
}

bool GraphicsSettings::touchEnabled() const {
    if (touchControls == TouchOn) return true;
    if (touchControls == TouchOff) return false;
#if defined(__ANDROID__) || defined(GB_MOBILE)
    return true;
#else
    return false;
#endif
}

void GraphicsSettings::save() const {
    nlohmann::json j = {
        {"vsync", vsync}, {"fpsCap", fpsCap}, {"showFps", showFps},
        {"quality", quality}, {"shadowRes", shadowRes}, {"shadowQuality", shadowQuality},
        {"ssao", ssao}, {"bloom", bloom}, {"fxaa", fxaa}, {"postFx", postFx},
        {"renderScale", renderScale}, {"maxLights", maxLights}, {"allowGore", allowGore}, {"checkUpdates", checkUpdates},
        {"touchControls", touchControls}, {"touchSize", touchSize},
        {"shiftLockSwitch", shiftLockSwitch}, {"mouseSensitivity", mouseSensitivity}, {"invertCamera", invertCamera},
        {"volume", volume}, {"fullscreen", fullscreen},
        {"waterQuality", waterQuality}, {"graphicsApi", graphicsApi},
    };
    std::ofstream f(settingsFile());
    if (f) f << j.dump(2);
}
