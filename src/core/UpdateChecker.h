#pragma once
#include <string>

// Asks GitHub (in the background) whether the repository has changes newer
// than this build. Uses the `curl` program, which Windows 10+, macOS and Linux
// all have. Private repositories or no internet simply mean "no update".
namespace UpdateChecker {

enum class State { Idle, Checking, UpToDate, Available, Failed };

struct Info {
    State       state = State::Idle;
    int         behindBy = 0;            // how many new changes
    std::string latestMessage;           // first line of the newest change
    std::string latestDate;
    std::string compareUrl;              // page listing what's new
};

void start();                 // kick off a check (does nothing if one is running)
Info info();                  // thread-safe snapshot
const char* currentVersion(); // e.g. "0.3.0 (a1b2c3d)"
bool canUpdate();             // is the source folder + installer here?
bool launchUpdater(const char* relaunchApp);   // runs install.py --update

} // namespace UpdateChecker
