#pragma once
#include <algorithm>
#include <string>
#include <utility>
#include <vector>
#include "../scene/Player.h"

// Your account on this computer: display name and avatar. The Player app
// dresses your character with it in every game you join.
struct Profile {
    std::string name = "Player";
    BodyColors  colors = Player::colorPresets()[0].second;
    HatStyle    hat    = HatStyle::None;
    glm::vec3   hatColor = glm::vec3(-1.0f);   // negative = the hat's normal colours
    std::vector<std::string> recent;   // file names of recently played games, newest first
    std::vector<std::pair<std::string, std::string>> grants;   // official badges: {badge, signature}
    std::vector<std::string> inventory;   // catalog item ids you own
    std::vector<std::string> wearing;     // catalog item ids you have on
    std::string server;                   // Guts&Bolts server to use ("" = offline)

    // Change your display name. "Guts" is kept for the staff account.
    bool rename(const std::string& newName, std::string& error);

    void played(const std::string& game) {
        recent.erase(std::remove(recent.begin(), recent.end(), game), recent.end());
        recent.insert(recent.begin(), game);
        if (recent.size() > 12) recent.resize(12);
        save();
    }

    void applyTo(Player& player) const {
        player.setBodyColors(colors);
        player.setHat(hat, hatColor);
    }

    static Profile& get();
    void load();
    void save() const;
};
