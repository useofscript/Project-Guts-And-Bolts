#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "../scene/Player.h"

// The avatar shop. Items are small signed files in the "catalog" folder; only
// ones signed by the official staff account show up, so only staff can add them.
namespace Catalog {

enum class Type { Hat, Shirt, Pants, Count };
const char* typeName(Type t);

struct Item {
    std::string id;            // e.g. "red-cap-1a2b3c4d"
    std::string name;
    std::string description;
    Type        type  = Type::Hat;
    HatStyle    hat   = HatStyle::Cap;     // hats only
    glm::vec3   color = glm::vec3(1.0f);
    long long   created = 0;               // unix time
    std::string signature;
};

// Every properly signed item, newest first.
std::vector<Item> load();

// Staff only: sign and save a new item (into the app's catalog folder and,
// when building from source, the repo's catalog folder so it can be shared).
bool create(Item& item, std::string& message);
// Staff only: remove an item file.
bool remove(const Item& item, std::string& message);

// Put an item on the profile (and remember you own it).
void wear(const Item& item);
bool isWearing(const Item& item);

} // namespace Catalog
