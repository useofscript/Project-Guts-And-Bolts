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
            fxaa = false; postFx = true; renderScale = 0.75f; maxLights = 8;
            break;
        case Medium:
            shadowRes = 2048; shadowQuality = 1; ssao = false; bloom = true;
            fxaa = true; postFx = true; renderScale = 1.0f; maxLights = 16;
            break;
        case High:
            shadowRes = 2048; shadowQuality = 2; ssao = true; bloom = true;
            fxaa = true; postFx = true; renderScale = 1.0f; maxLights = 32;
            break;
        case Ultra:
            shadowRes = 4096; shadowQuality = 2; ssao = true; bloom = true;
            fxaa = true; postFx = true; renderScale = 1.0f; maxLights = 32;
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
    };
    std::ofstream f(settingsFile());
    if (f) f << j.dump(2);
}
