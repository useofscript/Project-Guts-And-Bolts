#include "PlayerApp.h"
#include "../core/CrashHandler.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>

// Guts&BoltsPlayer — the platform app.
//   GutsAndBoltsPlayer                 open the home screen
//   GutsAndBoltsPlayer MyGame.gbscene  jump straight into a game
int main(int argc, char** argv) {
    CrashHandler::install();

    PlayerOptions opts;
    for (int i = 1; i < argc; ++i) {
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if      (!std::strcmp(argv[i], "--page"))       opts.page       = next();
        else if (!std::strcmp(argv[i], "--screenshot")) opts.screenshot = next();
        else if (!std::strcmp(argv[i], "--frames"))     opts.frames     = std::atoi(next());
        else if (!std::strcmp(argv[i], "--hold"))       opts.holdKey    = next();
        else if (argv[i][0] != '-')                     opts.game       = argv[i];
    }

    try {
        PlayerApp app(opts);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
