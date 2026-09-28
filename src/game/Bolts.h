#pragma once
#include <imgui.h>
#include <string>
#include <vector>

// Bolts: the Guts&Bolts currency (like Robux on the old Roblox site).
//
// You get some when you first start, a daily reward, and a few for time spent
// playing games. Staff can hand out Bolts with a code (signed with the official
// key, made for one account, and only usable once), and you spend them on
// catalog items.
//
// There's no central server, so your Bolts live in bolts.json on this computer
// (next to your account key).
// That file is signed with your own account key: editing it by hand breaks the
// signature and the balance is reset, so it can't just be typed in.
namespace Bolts {

inline constexpr long long kWelcome     = 100;   // first time you open the site
inline constexpr long long kDaily       = 25;    // once a day
inline constexpr long long kPlayReward  = 5;     // for every few minutes of playing...
inline constexpr float     kPlaySeconds = 300;   // ...this many seconds
inline constexpr long long kPlayDailyCap = 50;   // most you can earn from playing in one day

struct Entry {
    long long   amount = 0;   // + earned, - spent
    std::string reason;       // "Daily reward", "Bought Red Cap", ...
    long long   time = 0;     // unix seconds
    std::string ref;          // "daily:2026-09-28", "code:1a2b3c4d", "item:red-cap-..."
};

long long                 balance();
const std::vector<Entry>& history();          // oldest first
bool                      wasReset();         // bolts.json had been edited, so it started over
bool                      has(const std::string& ref);   // e.g. has("item:red-cap-1a2b")

// Earning
bool        canClaimDaily();
bool        claimDaily(std::string& message);
std::string timeUntilDaily();                 // "5h 12m"
long long   earnedFromPlayToday();
// Call every frame while playing a game. Returns the Bolts just earned (usually 0).
long long   addPlayTime(float seconds);

// Spending: fails (with a friendly message) if you don't have enough.
bool spend(long long amount, const std::string& reason, const std::string& ref, std::string& message);

// Staff: a code that gives `amount` Bolts to one account, once.
std::string makeCode(long long amount, const std::string& accountId, std::string& error);
bool        redeem(const std::string& code, std::string& message);

// "1,250"
std::string format(long long n);
// The Bolts coin: a golden hex bolt head, centred on `c`.
void drawIcon(ImDrawList* dl, ImVec2 c, float size);
// Coin + amount as one ImGui item, e.g. [bolt] 1,250
void amount(long long n, float iconSize = 0.0f, ImVec4 color = ImVec4(0.72f, 0.5f, 0.02f, 1.0f));

} // namespace Bolts
