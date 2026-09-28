#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include "../scene/Player.h"

// Your account on this computer: display name and avatar. The Player app
// dresses your character with it in every game you join.
struct Profile {
    std::string name = "Player";
    BodyColors  colors = Player::colorPresets()[0].second;
    HatStyle    hat    = HatStyle::None;
    std::vector<std::string> recent;   // file names of recently played games, newest first

    void played(const std::string& game) {
        recent.erase(std::remove(recent.begin(), recent.end(), game), recent.end());
        recent.insert(recent.begin(), game);
        if (recent.size() > 12) recent.resize(12);
        save();
    }

    void applyTo(Player& player) const {
        player.setBodyColors(colors);
        player.setHat(hat);
    }

    static Profile& get();
    void load();
    void save() const;
};
