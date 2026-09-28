#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <nlohmann/json.hpp>

class Scene;

// Getting uploaded things (games, audio, plugins) from the server onto this
// computer, into the downloads folder (see Paths::downloaded).
namespace Online {

// Download an asset (once). `done(ok, file, asset info or error text)` runs on the main thread.
using Downloaded = std::function<void(bool ok, const std::filesystem::path& file, const nlohmann::json& info)>;
void download(const std::string& assetId, Downloaded done = nullptr, bool redownload = false);

// Start downloading any server audio ("gb:<id>" sounds) a game uses.
void fetchSounds(Scene& scene);

} // namespace Online
