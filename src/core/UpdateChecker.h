#pragma once
#include <string>

// Asks gutsandbolts.net (in the background) whether there's a newer version, and
// updates by downloading the ready-made app from there. Uses the `curl` program,
// which Windows 10+, macOS and Linux all have. No internet simply means "no update".
namespace UpdateChecker {

enum class State { Idle, Checking, UpToDate, Available, Failed };

struct Info {
    State       state = State::Idle;
    int         behindBy = 0;            // how many new changes
    bool        plainlyBehind = false;   // just older than main (not a build with its own changes)
    std::string latestMessage;           // first line of the newest change
    std::string latestDate;
    std::string compareUrl;              // page listing what's new
};

void start();                 // kick off a check (does nothing if one is running)
Info info();                  // thread-safe snapshot
const char* currentVersion(); // e.g. "0.3.0 (a1b2c3d)"
bool canUpdate();             // is there a download of the new version for this computer?
bool launchUpdater(const char* relaunchApp);   // downloads it, puts it over this one, restarts

} // namespace UpdateChecker
