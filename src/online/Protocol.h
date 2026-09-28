#pragma once
#include <nlohmann/json.hpp>
#include <string>

// What the Guts&Bolts apps and the Guts&Bolts server say to each other.
//
// Every request is a JSON message signed with the sender's account key:
//   {"op": "upload", "account": "<id>", "time": 1790000000, "nonce": "1a2b3c4d",
//    "args": {...}, "sig": "<signature of requestText()>"}
// so the server always knows exactly who is asking, and nobody can replay an
// old request or change one on the way. Replies are {"ok": true, ...} or
// {"ok": false, "error": "what went wrong, in plain words"}.
namespace Online {

inline constexpr int  kDefaultPort = 7780;
inline constexpr int  kProtocol    = 1;
inline constexpr long long kMaxClockSkew = 600;   // seconds a request's time may be off by

// Uploading. Verified creators pay nothing and can sell their creations;
// everyone else pays a small fee and has a daily limit.
inline constexpr long long kFeeClothing = 10;     // hats, shirts, pants
inline constexpr long long kFeeAudio    = 20;
inline constexpr long long kFeePlugin   = 20;
inline constexpr long long kFeeGame     = 0;
inline constexpr int       kDailyUploadsUnverified = 5;
inline constexpr int       kCreatorSharePercent = 70;   // of every sale goes to the creator

// Biggest uploads, in bytes.
inline constexpr size_t kMaxAudio  = 6u * 1024u * 1024u;
inline constexpr size_t kMaxGame   = 24u * 1024u * 1024u;
inline constexpr size_t kMaxPlugin = 512u * 1024u;

// Kinds of things people upload.
bool        validKind(const std::string& kind);        // hat, shirt, pants, audio, plugin, game
bool        isClothing(const std::string& kind);
long long   uploadFee(const std::string& kind);
size_t      maxSize(const std::string& kind);
const char* kindTitle(const std::string& kind);        // "Hat", "Audio", ...

// The exact text a request's signature covers.
std::string requestText(const std::string& op, const std::string& account, long long time,
                        const std::string& nonce, const nlohmann::json& args);

// Is this badge grant real? `sig` is signed by `official`, or for badges Staff
// may give ("verified"), "s:<staff id>:<their staff grant>:<signature>".
bool grantValid(const std::string& official, const std::string& key, const std::string& accountId,
                const std::string& sig);
std::string grantMessage(const std::string& key, const std::string& accountId);

// Binary data inside JSON.
std::string base64Encode(const std::string& bytes);
bool        base64Decode(const std::string& text, std::string& bytes);

// Names and descriptions: printable text only, trimmed, at most `maxLen` characters.
std::string cleanText(const std::string& s, size_t maxLen, bool allowNewlines = false);

// Usernames: 3-20 letters, numbers or one underscore (not at the ends), not
// all numbers, and not one of the reserved names. "" if it's fine.
std::string usernameProblem(const std::string& name, bool official = false);
inline constexpr size_t kMinPassword = 8;

long long   unixNow();
std::string utcDay(long long t);    // "2026-09-28"

} // namespace Online
