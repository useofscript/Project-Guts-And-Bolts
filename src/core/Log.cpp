#include "Log.h"
#include <chrono>
#include <ctime>
#include <cstdio>

namespace Log {

namespace {
std::vector<Entry> g_entries;
constexpr size_t kMaxEntries = 2000;

std::string timestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

void push(Level level, const std::string& text) {
    if (g_entries.size() >= kMaxEntries)
        g_entries.erase(g_entries.begin(), g_entries.begin() + kMaxEntries / 4);
    g_entries.push_back({level, text, timestamp()});
    // Also echo to the terminal — handy when launching from a command line.
    static const char* tags[] = {"", "[warn] ", "[error] ", "[editor] "};
    std::printf("%s%s\n", tags[(int)level], text.c_str());
    std::fflush(stdout);
}
} // namespace

void info  (const std::string& text) { push(Level::Info,   text); }
void warn  (const std::string& text) { push(Level::Warn,   text); }
void error (const std::string& text) { push(Level::Error,  text); }
void system(const std::string& text) { push(Level::System, text); }

const std::vector<Entry>& entries() { return g_entries; }
void clear() { g_entries.clear(); }

} // namespace Log
