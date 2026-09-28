#pragma once
#include <filesystem>
#include <string>
#include <vector>

// Where things live. Everything is kept next to the program itself, so it
// works the same no matter how the app was started (double-click, shortcut,
// terminal):
//   <app folder>/games/*.gbscene   games (the editor saves here, the Player lists them)
//   <app folder>/settings.json     graphics / frame-rate settings
//   <app folder>/profile.json      your avatar and display name
namespace Paths {

std::filesystem::path appFolder();
std::filesystem::path gamesFolder();
std::filesystem::path file(const char* name);   // appFolder() / name

inline constexpr const char* kExtension = ".gbscene";

// All game files in the games folder, sorted by name.
std::vector<std::filesystem::path> listGames();

// Path of the other app (editor <-> player) next to this one.
std::filesystem::path sibling(const char* programName);

// Start another program (without waiting for it to finish).
bool launch(const std::filesystem::path& program, const std::string& argument = {});

} // namespace Paths
