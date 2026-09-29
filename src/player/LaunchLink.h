#pragma once
#include <string>

// gutsandbolts:// links: the website's Play button opens the app with one,
// like gutsandbolts://play/<game id>?guest=boy
namespace LaunchLink {

struct Link {
    std::string game;    // server asset id of the game to play
    std::string guest;   // "boy" / "girl" when a visitor picked a guest character, else ""
};
bool parse(const std::string& url, Link& out);

// Computers: make gutsandbolts:// links open this program (Windows and Linux;
// quietly does nothing where it can't).
void registerScheme();

// Android: a link that opened (or re-opened) the app since the last call, or "".
std::string poll();

} // namespace LaunchLink
