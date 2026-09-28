#pragma once
#include <filesystem>
#include <string>

// Your Guts&Bolts account on this computer.
//
// Every install gets its own key pair the first time it's needed. The public
// half is your account ID (safe to show anyone); the secret half stays in
// account.key in your user folder and is used to prove you are you when you
// join someone's game. Official things (the Guts staff account, badges,
// catalog items) are signed with the official key, so nobody can fake them.
namespace Account {

inline constexpr const char* kStaffName = "Guts";

const std::string& id();                  // 64 hex characters
std::string        shortId();             // first 8, for showing people
std::string        sign(const std::string& message);   // 128 hex characters
bool               verify(const std::string& idHex, const std::string& message, const std::string& sigHex);

// The official (staff) account's ID, or "" if none is set up yet.
std::string officialId();
bool        isOfficial(const std::string& idHex);
bool        iAmStaff();
// "Guts" (any capitalisation) belongs to the staff account only.
bool        nameIsReserved(const std::string& name);

std::filesystem::path folder();           // where account.key lives
std::string           randomHex(int bytes);

// `--create-staff-account`: make this computer's account the official one.
// Returns a message explaining what happened / what to do next.
std::string createStaffAccount();

} // namespace Account
