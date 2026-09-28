#include "PlayerApp.h"
#ifdef GB_MOBILE
#include <SDL.h>   // on Android, SDL starts the app through this main()
#endif
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
        else if (!std::strcmp(argv[i], "--join"))       opts.join       = next();
        else if (!std::strcmp(argv[i], "--host"))       opts.host       = true;
        else if (!std::strcmp(argv[i], "--say"))        opts.say        = next();
        else if (!std::strcmp(argv[i], "--create-staff-account")) opts.createStaff = true;
        else if (!std::strcmp(argv[i], "--touch-test")) opts.touchTest  = next();
        else if (!std::strcmp(argv[i], "--test-make-items")) opts.testItems = true;
        else if (!std::strcmp(argv[i], "--test-grant"))  opts.testGrantFor = next();
        else if (!std::strcmp(argv[i], "--test-redeem")) opts.testRedeem = next();
        else if (!std::strcmp(argv[i], "--test-bolts-code")) opts.testBoltsFor = next();
        else if (!std::strcmp(argv[i], "--test-redeem-bolts")) opts.testRedeemBolts = next();
        else if (!std::strcmp(argv[i], "--test-buy")) opts.testBuy = next();
        else if (!std::strcmp(argv[i], "--online-test")) opts.onlineTest = next();
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
