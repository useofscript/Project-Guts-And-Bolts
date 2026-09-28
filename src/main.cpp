#include "Application.h"
#include "core/CrashHandler.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    CrashHandler::install();

    LaunchOptions opts;
    for (int i = 1; i < argc; ++i) {
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if      (!std::strcmp(argv[i], "--open"))       opts.openFile   = next();
        else if (!std::strcmp(argv[i], "--play"))       opts.play       = true;
        else if (!std::strcmp(argv[i], "--screenshot")) opts.screenshot = next();
        else if (!std::strcmp(argv[i], "--frames"))     opts.frames     = std::atoi(next());
        else if (!std::strcmp(argv[i], "--hold"))       opts.holdKey    = next();
        else if (!std::strcmp(argv[i], "--team-host"))  opts.teamHost   = true;
        else if (!std::strcmp(argv[i], "--team-join"))  opts.teamJoin   = next();
        else if (!std::strcmp(argv[i], "--test-add-part")) opts.testAddPart = next();
        else if (!std::strcmp(argv[i], "--test-premades")) opts.testPremades = next();
        else if (!std::strcmp(argv[i], "--test-keys"))  opts.testKeys   = next();
        else if (!std::strcmp(argv[i], "--test-select")) opts.testSelect = next();
        else if (!std::strcmp(argv[i], "--export-roblox")) opts.exportRoblox = next();
        else if (argv[i][0] != '-')                     opts.openFile   = argv[i];  // double-clicked file
    }

    try {
        Application app(opts);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
