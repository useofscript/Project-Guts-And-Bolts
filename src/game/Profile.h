#pragma once
#include <string>
#include "../scene/Player.h"

// Your account on this computer: display name and avatar. The Player app
// dresses your character with it in every game you join.
struct Profile {
    std::string name = "Player";
    BodyColors  colors = Player::colorPresets()[0].second;
    HatStyle    hat    = HatStyle::None;

    void applyTo(Player& player) const {
        player.setBodyColors(colors);
        player.setHat(hat);
    }

    static Profile& get();
    void load();
    void save() const;
};
