#pragma once
#include <string>
#include <vector>

// Engine-wide message log, shown in the Output panel. Scripts write to it with
// print() / warn(), script errors land here in red, and the editor reports
// things like "Saved scene".
namespace Log {

enum class Level { Info, Warn, Error, System };

struct Entry {
    Level       level;
    std::string text;
    std::string time;   // "HH:MM:SS"
};

void info  (const std::string& text);
void warn  (const std::string& text);
void error (const std::string& text);
void system(const std::string& text);   // editor messages (dimmed)

const std::vector<Entry>& entries();
void clear();

} // namespace Log
