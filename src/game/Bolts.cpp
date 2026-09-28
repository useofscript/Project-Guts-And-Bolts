#include "Bolts.h"
#include "../core/Account.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace Bolts {

namespace {
using json = nlohmann::json;

struct Ledger {
    bool               loaded = false;
    bool               reset = false;
    std::vector<Entry> entries;
    std::string        playDay;           // the day `playEarned` counts
    long long          playEarned = 0;
    float              playSeconds = 0;   // time played towards the next reward (not saved)
};

// Next to your account key, so your Bolts go with your account (and survive app updates).
std::filesystem::path ledgerFile() {
    std::error_code ec;
    std::filesystem::create_directories(Account::folder(), ec);
    return Account::folder() / "bolts.json";
}

Ledger& L() {
    static Ledger l;
    return l;
}

long long now() {
    return (long long)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string today() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

// Everything that gets signed (nlohmann sorts object keys, so it's stable).
json body(const Ledger& l) {
    json e = json::array();
    for (const Entry& x : l.entries) e.push_back({x.amount, x.reason, x.time, x.ref});
    return {{"account", Account::id()}, {"entries", e}, {"playDay", l.playDay}, {"playEarned", l.playEarned}};
}
std::string signedText(const Ledger& l) { return "gb-bolts-ledger:" + body(l).dump(); }

void save() {
    Ledger& l = L();
    json j = body(l);
    j["sig"] = Account::sign(signedText(l));
    std::ofstream f(ledgerFile());
    if (f) f << j.dump(2);
}

void add(long long amount, const std::string& reason, const std::string& ref) {
    L().entries.push_back({amount, reason, now(), ref});
    save();
}

void load() {
    Ledger& l = L();
    if (l.loaded) return;
    l.loaded = true;
    std::ifstream f(ledgerFile());
    if (!f) {   // first time here: a welcome gift
        add(kWelcome, "Welcome to Guts&Bolts!", "welcome");
        return;
    }
    json j = json::parse(f, nullptr, false);
    Ledger read;
    bool ok = j.is_object() && j.value("account", std::string()) == Account::id();
    if (ok && j.contains("entries") && j["entries"].is_array()) {
        for (const auto& e : j["entries"]) {
            if (!e.is_array() || e.size() != 4 || !e[0].is_number_integer() || !e[1].is_string() ||
                !e[2].is_number_integer() || !e[3].is_string()) { ok = false; break; }
            read.entries.push_back({e[0].get<long long>(), e[1].get<std::string>(), e[2].get<long long>(), e[3].get<std::string>()});
        }
        read.playDay = j.value("playDay", std::string());
        read.playEarned = j.value("playEarned", 0LL);
    }
    ok = ok && Account::verify(Account::id(), signedText(read), j.value("sig", std::string()));
    if (ok) {
        l.entries = std::move(read.entries);
        l.playDay = read.playDay;
        l.playEarned = read.playEarned;
    } else {
        // Someone edited the file (or it's from another computer): start over, no welcome gift this time.
        l.reset = true;
        l.entries.clear();
        add(0, "Your Bolts file had been changed by hand, so it was reset", "reset");
    }
}
} // namespace

long long balance() {
    load();
    long long b = 0;
    for (const Entry& e : L().entries) b += e.amount;
    return b;
}

const std::vector<Entry>& history() { load(); return L().entries; }
bool wasReset() { load(); return L().reset; }

bool has(const std::string& ref) {
    load();
    for (const Entry& e : L().entries) if (e.ref == ref) return true;
    return false;
}

// --- Earning ---------------------------------------------------------------

bool canClaimDaily() { return !has("daily:" + today()); }

bool claimDaily(std::string& message) {
    if (!canClaimDaily()) { message = "You already got today's Bolts. Come back in " + timeUntilDaily() + "!"; return false; }
    add(kDaily, "Daily reward", "daily:" + today());
    message = "You got " + std::to_string(kDaily) + " Bolts! Come back tomorrow for more.";
    return true;
}

std::string timeUntilDaily() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    int left = 24 * 3600 - (tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec);
    int h = left / 3600, m = (left % 3600) / 60;
    return h > 0 ? std::to_string(h) + "h " + std::to_string(m) + "m" : std::to_string(std::max(1, m)) + "m";
}

long long earnedFromPlayToday() {
    load();
    return L().playDay == today() ? L().playEarned : 0;
}

long long addPlayTime(float seconds) {
    load();
    Ledger& l = L();
    if (seconds <= 0 || seconds > 5) return 0;   // ignore pauses / hitches
    std::string d = today();
    if (l.playDay != d) { l.playDay = d; l.playEarned = 0; }
    l.playSeconds += seconds;
    if (l.playSeconds < kPlaySeconds) return 0;
    l.playSeconds -= kPlaySeconds;
    if (l.playEarned >= kPlayDailyCap) return 0;
    long long got = std::min(kPlayReward, kPlayDailyCap - l.playEarned);
    l.playEarned += got;
    add(got, "Playing games", "play:" + d);
    return got;
}

// --- Spending --------------------------------------------------------------

bool spend(long long amount, const std::string& reason, const std::string& ref, std::string& message) {
    if (amount <= 0) { message = "Nothing to pay."; return true; }
    long long have = balance();
    if (have < amount) {
        message = "You need " + format(amount - have) + " more Bolts for that.";
        return false;
    }
    add(-amount, reason, ref);
    message = reason + " for " + format(amount) + " Bolts.";
    return true;
}

// --- Codes -----------------------------------------------------------------

namespace {
std::string codeMessage(long long amount, const std::string& account, const std::string& nonce) {
    return "gb-bolts:" + std::to_string(amount) + ":" + account + ":" + nonce;
}
std::string compact(const std::string& in) {
    std::string out;
    for (char c : in) if (!std::isspace((unsigned char)c)) out += c;
    return out;
}
} // namespace

std::string makeCode(long long amount, const std::string& accountId, std::string& error) {
    if (!Account::iAmStaff()) { error = "Only the staff account can give out Bolts."; return ""; }
    if (amount <= 0 || amount > 1000000) { error = "Pick an amount from 1 to 1,000,000."; return ""; }
    std::string who;
    for (char c : compact(accountId)) who += (char)std::tolower((unsigned char)c);
    if (who.size() != 64) { error = "An account ID is 64 letters and numbers long (from their Avatar page)."; return ""; }
    std::string nonce = Account::randomHex(4);
    return "BOLTS-" + std::to_string(amount) + "-" + nonce + "-" + Account::sign(codeMessage(amount, who, nonce));
}

bool redeem(const std::string& codeIn, std::string& message) {
    std::string code = compact(codeIn);
    // BOLTS-<amount>-<nonce>-<signature>
    size_t a = code.find('-'), b = a == std::string::npos ? a : code.find('-', a + 1),
           c = b == std::string::npos ? b : code.find('-', b + 1);
    if (code.rfind("BOLTS-", 0) != 0 || c == std::string::npos) { message = "That doesn't look like a Bolts code."; return false; }
    long long amount = 0;
    try { amount = std::stoll(code.substr(a + 1, b - a - 1)); } catch (...) { amount = 0; }
    std::string nonce = code.substr(b + 1, c - b - 1), sig = code.substr(c + 1);
    std::string official = Account::officialId();
    if (amount <= 0 || official.empty() || !Account::verify(official, codeMessage(amount, Account::id(), nonce), sig)) {
        message = "That code isn't valid for your account (codes only work for the account they were made for).";
        return false;
    }
    if (has("code:" + nonce)) { message = "You already used that code."; return false; }
    add(amount, "Bolts from Guts&Bolts staff", "code:" + nonce);
    message = "You got " + format(amount) + " Bolts!";
    return true;
}

// --- Looks -----------------------------------------------------------------

std::string format(long long n) {
    std::string s = std::to_string(n < 0 ? -n : n), out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i > 0 && (s.size() - i) % 3 == 0) out += ',';
        out += s[i];
    }
    return n < 0 ? "-" + out : out;
}

void drawIcon(ImDrawList* dl, ImVec2 c, float s) {
    // A golden hex bolt head seen from above, with a shine and a slot.
    const float kPi = 3.14159265f;
    auto hex = [&](float r, ImU32 col) {
        ImVec2 p[6];
        for (int i = 0; i < 6; ++i) {
            float a = kPi / 3.0f * i + kPi / 6.0f;
            p[i] = ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r);
        }
        dl->AddConvexPolyFilled(p, 6, col);
    };
    hex(s * 0.5f, IM_COL32(150, 95, 10, 255));      // dark rim
    hex(s * 0.42f, IM_COL32(250, 190, 40, 255));    // gold
    dl->AddCircleFilled(c, s * 0.24f, IM_COL32(255, 222, 110, 255), 20);
    dl->AddCircle(c, s * 0.24f, IM_COL32(190, 125, 15, 255), 20, std::max(1.0f, s * 0.05f));
    float w = s * 0.2f, h = std::max(1.5f, s * 0.07f);
    dl->AddRectFilled(ImVec2(c.x - w, c.y - h * 0.5f), ImVec2(c.x + w, c.y + h * 0.5f), IM_COL32(170, 110, 12, 255));
    dl->AddCircleFilled(ImVec2(c.x - s * 0.17f, c.y - s * 0.2f), s * 0.06f, IM_COL32(255, 245, 200, 220), 8);
}

void amount(long long n, float iconSize, ImVec4 color) {
    if (iconSize <= 0) iconSize = ImGui::GetTextLineHeight() * 1.15f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    float lh = ImGui::GetTextLineHeight();
    ImGui::Dummy(ImVec2(iconSize, std::max(iconSize, lh)));
    drawIcon(ImGui::GetWindowDrawList(), ImVec2(p.x + iconSize * 0.5f, p.y + std::max(iconSize, lh) * 0.5f), iconSize);
    ImGui::SameLine(0, 4);
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, p.y + (std::max(iconSize, lh) - lh) * 0.5f));
    ImGui::TextColored(color, "%s", format(n).c_str());
}

} // namespace Bolts
