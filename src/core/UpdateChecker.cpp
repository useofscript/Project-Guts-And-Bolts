#include "UpdateChecker.h"
#include "Paths.h"
#include "Version.h"

#include <nlohmann/json.hpp>
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <thread>

#ifdef _WIN32
#define popen  _popen
#define pclose _pclose
#endif

namespace UpdateChecker {

namespace {
std::mutex        g_mutex;
Info              g_info;
std::atomic<bool> g_running{false};

std::string runCapture(const std::string& cmd) {
    std::string out;
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
    pclose(p);
    return out;
}

void setState(State s) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_info.state = s;
}

void check() {
    std::string commit = GB_COMMIT;
    if (const char* fake = std::getenv("GB_PRETEND_COMMIT")) commit = fake;   // for testing the toast
    if (commit == "unknown" || commit.empty()) { setState(State::Failed); g_running = false; return; }

    // "How far behind main is this commit?"
    std::string url = std::string("https://api.github.com/repos/") + GB_REPO + "/compare/" + commit + "...main";
#ifdef _WIN32
    const char* quiet = " 2>NUL";
#else
    const char* quiet = " 2>/dev/null";
#endif
    std::string text = runCapture("curl -fsSL -m 15 -H \"Accept: application/vnd.github+json\" \"" + url + "\"" + quiet);
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);

    Info result;
    if (!j.is_object() || !j.contains("status")) {
        result.state = State::Failed;
    } else {
        std::string status = j.value("status", "");
        result.behindBy = j.value("ahead_by", 0);   // main is "ahead" of us
        result.compareUrl = std::string("https://github.com/") + GB_REPO + "/compare/" + commit.substr(0, 12) + "...main";
        if ((status == "ahead" || status == "diverged") && result.behindBy > 0) {
            result.state = State::Available;
            if (j.contains("commits") && j["commits"].is_array() && !j["commits"].empty()) {
                const auto& last = j["commits"].back();
                std::string msg = last["commit"].value("message", "");
                result.latestMessage = msg.substr(0, msg.find('\n'));
                result.latestDate = last["commit"]["committer"].value("date", "").substr(0, 10);
            }
        } else {
            result.state = State::UpToDate;
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_info = result;
    }
    g_running = false;
}
} // namespace

void start() {
    if (g_running.exchange(true)) return;
    setState(State::Checking);
    std::thread(check).detach();
}

Info info() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_info;
}

const char* currentVersion() {
    static std::string v = [] {
        std::string c = GB_COMMIT;
        return std::string(GB_VERSION) + (c == "unknown" ? "" : " (" + c.substr(0, 7) + ")");
    }();
    return v.c_str();
}

bool canUpdate() {
    std::error_code ec;
    return std::filesystem::exists(std::filesystem::path(GB_SOURCE_DIR) / "install.py", ec);
}

bool launchUpdater(const char* relaunchApp) {
    if (!canUpdate()) return false;
    std::filesystem::path src = GB_SOURCE_DIR;
    std::string app = Paths::sibling(relaunchApp).string();
#ifdef _WIN32
    // pythonw: no console window. `start` so we don't wait for it.
    std::string cmd = "cd /d \"" + src.string() + "\" && start \"\" py -3 install.py --update --relaunch \"" + app + "\"";
#else
    std::string cmd = "cd \"" + src.string() + "\" && (python3 install.py --update --relaunch \"" + app +
                      "\" >/dev/null 2>&1 &)";
#endif
    return std::system(cmd.c_str()) == 0;
}

} // namespace UpdateChecker
