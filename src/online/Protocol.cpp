#include "Protocol.h"
#include "../core/Account.h"

#include <cctype>
#include <chrono>
#include <cstring>
#include <ctime>

namespace Online {

bool validKind(const std::string& k) {
    return k == "hat" || k == "shirt" || k == "pants" || k == "audio" || k == "plugin" || k == "game" || k == "decal";
}
bool isClothing(const std::string& k) { return k == "hat" || k == "shirt" || k == "pants"; }
bool alwaysFree(const std::string& k) { return k == "decal" || k == "audio"; }

const char* banReasonTitle(const std::string& key) {
    for (const BanReason& r : kBanReasons) if (key == r.key) return r.title;
    return nullptr;
}

std::string banMessage(const std::string& reason, const std::string& note) {
    std::string m = "This account has been banned";
    if (const char* t = banReasonTitle(reason)) m += std::string(" for: ") + t;
    m += ".";
    if (!note.empty()) m += " Note from staff: " + note;
    return m;
}

long long uploadFee(const std::string& k) {
    if (isClothing(k)) return kFeeClothing;
    if (k == "audio") return kFeeAudio;
    if (k == "plugin") return kFeePlugin;
    if (k == "decal") return kFeeDecal;
    return kFeeGame;
}

size_t maxSize(const std::string& k) {
    if (k == "audio") return kMaxAudio;
    if (k == "game") return kMaxGame;
    if (k == "plugin") return kMaxPlugin;
    if (k == "decal") return kMaxDecal;
    return 64u * 1024u;   // clothing is just a little description of the look
}

const char* kindTitle(const std::string& k) {
    if (k == "hat") return "Hat";
    if (k == "shirt") return "Shirt";
    if (k == "pants") return "Pants";
    if (k == "audio") return "Audio";
    if (k == "plugin") return "Plugin";
    if (k == "game") return "Game";
    if (k == "decal") return "Decal";
    return "?";
}

std::string requestText(const std::string& op, const std::string& account, long long time,
                        const std::string& nonce, const nlohmann::json& args) {
    return "gb-req:" + op + "\n" + account + "\n" + std::to_string(time) + "\n" + nonce + "\n" + args.dump();
}

std::string grantMessage(const std::string& key, const std::string& accountId) {
    return "gb-badge:" + key + ":" + accountId;
}

bool grantValid(const std::string& official, const std::string& key, const std::string& accountId, const std::string& sig) {
    if (official.empty()) return false;
    if (sig.rfind("s:", 0) != 0) return Account::verify(official, grantMessage(key, accountId), sig);
    if (key != "verified") return false;   // the only badge Staff members can give
    size_t a = sig.find(':', 2), b = a == std::string::npos ? a : sig.find(':', a + 1);
    if (b == std::string::npos) return false;
    std::string staffId = sig.substr(2, a - 2), staffSig = sig.substr(a + 1, b - a - 1), grantSig = sig.substr(b + 1);
    return Account::verify(official, grantMessage("staff", staffId), staffSig) &&
           Account::verify(staffId, grantMessage(key, accountId), grantSig);
}

// --- Base64 ------------------------------------------------------------------

namespace {
const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

std::string base64Encode(const std::string& in) {
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        uint32_t v = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8) | (uint8_t)in[i + 2];
        out += kB64[(v >> 18) & 63]; out += kB64[(v >> 12) & 63]; out += kB64[(v >> 6) & 63]; out += kB64[v & 63];
    }
    if (i + 1 == in.size()) {
        uint32_t v = (uint8_t)in[i] << 16;
        out += kB64[(v >> 18) & 63]; out += kB64[(v >> 12) & 63]; out += "==";
    } else if (i + 2 == in.size()) {
        uint32_t v = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8);
        out += kB64[(v >> 18) & 63]; out += kB64[(v >> 12) & 63]; out += kB64[(v >> 6) & 63]; out += '=';
    }
    return out;
}

bool base64Decode(const std::string& in, std::string& out) {
    int map[256];
    for (int& m : map) m = -1;
    for (int k = 0; k < 64; ++k) map[(unsigned char)kB64[k]] = k;
    out.clear();
    out.reserve(in.size() / 4 * 3);
    uint32_t v = 0;
    int bits = 0;
    for (char c : in) {
        if (c == '=' ) break;
        if (c == '\n' || c == '\r' || c == ' ') continue;
        int d = map[(unsigned char)c];
        if (d < 0) return false;
        v = (v << 6) | (uint32_t)d;
        bits += 6;
        if (bits >= 8) { bits -= 8; out += (char)((v >> bits) & 0xFF); }
    }
    return true;
}

// --- Text ----------------------------------------------------------------------

std::string cleanText(const std::string& s, size_t maxLen, bool allowNewlines) {
    std::string out;
    for (unsigned char c : s) {
        if (c == '\n' && allowNewlines) out += '\n';
        else if (c >= 32 && c != 127) out += (char)c;   // keeps UTF-8 letters too
        if (out.size() >= maxLen) break;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '\n')) out.pop_back();
    size_t start = 0;
    while (start < out.size() && (out[start] == ' ' || out[start] == '\n')) ++start;
    return out.substr(start);
}

std::string usernameProblem(const std::string& name, bool official) {
    if (name.size() < 3) return "Usernames need at least 3 characters.";
    if (name.size() > 20) return "Usernames can be at most 20 characters.";
    int underscores = 0, digits = 0;
    for (char c : name) {
        if (c == '_') ++underscores;
        else if (c >= '0' && c <= '9') ++digits;
        else if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
            return "Usernames can only have letters, numbers and _.";
    }
    if (underscores > 1) return "Usernames can have only one _.";
    if (name.front() == '_' || name.back() == '_') return "Usernames can't start or end with _.";
    if (digits == (int)name.size()) return "Usernames can't be only numbers.";
    std::string l;
    for (char c : name) if (c != '_') l += (char)std::tolower((unsigned char)c);
    if (l == "guts") return official ? "" : "That username belongs to Guts&Bolts staff.";
    static const char* reserved[] = {"admin", "administrator", "staff", "moderator", "mod", "gutsandbolts",
                                     "gutsbolts", "official", "system", "server", "roblox", "support", "help"};
    for (const char* r : reserved) if (l == r) return "That username is reserved.";
    return "";
}

long long unixNow() {
    return (long long)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string utcDay(long long t) {
    std::time_t tt = (std::time_t)t;
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

} // namespace Online
