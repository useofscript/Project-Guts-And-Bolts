#pragma once

// The public key of the official "Guts" staff account. Whoever holds the
// matching secret key (made by `GutsAndBoltsPlayer --create-staff-account`)
// is the only one who can use the name Guts, wears the Administrator badge,
// makes catalog items and hands out official badges.
//
// This is a *public* key: it's safe to share and to commit. The secret half
// never leaves the owner's computer. Empty = no staff account set up yet.
inline constexpr const char* kOfficialKey = "";
