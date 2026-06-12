#include "Application.h"
#include "core/CrashHandler.h"
#include <stdexcept>
#include <iostream>

int main() {
    CrashHandler::install();
    try {
        Application app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
