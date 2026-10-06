#pragma once
#include <string>

// The text filter: if something has a hate word, a link or personal info (phone
// numbers, email addresses) in it, the whole thing becomes "[ Content Deleted ]",
// like classic Roblox. Swearing is fine (the site is 18+).
// worker/textfilter.js does exactly the same for the website's server.
namespace TextFilter {
inline const char* const kContentDeleted = "[ Content Deleted ]";
std::string filter(const std::string& text);
bool nameHasHateWord(const std::string& name);   // usernames: anywhere in the name counts
inline bool changes(const std::string& text) { return filter(text) != text; }   // (names)
}
