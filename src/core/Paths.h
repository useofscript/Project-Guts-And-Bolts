#pragma once
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

// Where games live. The editor saves into ./games and the Guts&BoltsPlayer
// app lists everything it finds there.
namespace Paths {

inline std::filesystem::path gamesFolder() {
    std::filesystem::path p = std::filesystem::current_path() / "games";
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p;
}

inline constexpr const char* kExtension = ".gbscene";

// All game files in the games folder, sorted by name.
inline std::vector<std::filesystem::path> listGames() {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator(gamesFolder(), ec))
        if (e.is_regular_file() && e.path().extension() == kExtension) out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace Paths
