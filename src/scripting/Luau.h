#pragma once
#include <string>
#include <vector>

// Roblox scripts are written in Luau, a Lua dialect with a few extras. This
// rewrites the common ones into plain Lua 5.4 so imported scripts run:
//   x += 1            ->  x = x + (1)        (also -= *= /= //= %= ^= ..=)
//   continue          ->  goto to the end of the loop
//   local n: number   ->  local n            (type annotations and casts)
//   type Foo = ...    ->  (removed)
//   `Hi {name}!`      ->  ("Hi " .. tostring(name) .. "!")
// `notes` (optional) collects what was changed or couldn't be converted.
namespace Luau {
std::string toLua(const std::string& source, std::vector<std::string>* notes = nullptr);
}
