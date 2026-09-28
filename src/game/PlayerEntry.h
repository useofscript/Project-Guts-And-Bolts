#pragma once
#include <string>

// One row of the in-game player list.
struct PlayerEntry {
    std::string name;
    bool        admin = false;   // the official staff account (checked with its signature)
};
