#pragma once
#include <string>
#include <utility>
#include <vector>

// One row of the in-game player list.
struct PlayerEntry {
    std::string name;
    bool        admin = false;   // the official staff account (checked with its signature)
    bool        verified = false;   // has the (signed) Verified badge
    std::vector<std::pair<std::string, std::string>> stats;   // leaderstats: ("Coins", "12")...
    bool        creator = false;   // made this game (a little hammer shows next to their name)
};
