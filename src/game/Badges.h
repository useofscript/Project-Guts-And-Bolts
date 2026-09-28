#pragma once
#include <imgui.h>
#include <string>
#include <utility>
#include <vector>

// Official Guts&Bolts badges. Only the staff account can hand them out: each
// one is a signature from the official key saying "account X has badge Y",
// so they can't be copied onto someone else's account or made up.
namespace Badges {

enum class Id { Administrator, Tester, BugHunter, FeaturedCreator, Count };

struct Info {
    const char* key;          // stored in files / sent over the network
    const char* name;
    const char* description;
    bool        grantable;    // false: only the staff account has it
};

const Info& info(Id id);
bool        fromKey(const std::string& key, Id& out);

// A signed "grant": {badge key, signature}.
using Grant = std::pair<std::string, std::string>;

// Which official badges an account really has (bad grants are ignored).
std::vector<Id> verified(const std::string& accountId, const std::vector<Grant>& grants);

// Staff only: a code the other player pastes into "Redeem a badge code".
std::string makeCode(Id id, const std::string& accountId, std::string& error);
// Checks a code is for this account and adds it to the profile.
bool        redeem(const std::string& code, std::string& message);

// The badge's picture, centred on `c`, `size` pixels across.
void drawIcon(ImDrawList* dl, ImVec2 c, float size, Id id);
// A badge picture as an ImGui item (with a tooltip).
void icon(Id id, float size);

} // namespace Badges
