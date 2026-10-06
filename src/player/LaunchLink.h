#pragma once
#include <string>

// gutsandbolts:// links: the website's Play button opens the app with one,
// like gutsandbolts://play/<game id>?guest=boy, and its "Edit in Studio" button with
// gutsandbolts://edit/<game id> (the app downloads it and opens Studio on it).
namespace LaunchLink {

struct Link {
    std::string game;    // server asset id of the game to play
    std::string guest;   // "boy" / "girl" when a visitor picked a guest character, else ""
    std::string server;  // a running server to join (the game page's Join button), else ""
    bool        edit = false;   // open it in Studio instead of playing it
};
bool parse(const std::string& url, Link& out);

// Computers: make gutsandbolts:// links open this program (Windows and Linux;
// quietly does nothing where it can't).
void registerScheme();

// Android: a link that opened (or re-opened) the app since the last call, or "".
std::string poll();

} // namespace LaunchLink
