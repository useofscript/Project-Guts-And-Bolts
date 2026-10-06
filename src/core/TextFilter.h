#pragma once
#include <string>

// The text filter: hate words, links and personal info (phone numbers, email
// addresses) get covered with #s. Swearing is fine (the site is 18+).
// worker/textfilter.js does exactly the same for the website's server.
namespace TextFilter {
std::string filter(const std::string& text);
bool nameHasHateWord(const std::string& name);   // usernames: anywhere in the name counts
inline bool changes(const std::string& text) { return filter(text) != text; }   // (names)
}
