#include "Paths.h"
#include <algorithm>
#include <cstdlib>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#endif

namespace Paths {

std::filesystem::path appFolder() {
    static std::filesystem::path cached = [] {
        std::error_code ec;
#ifdef _WIN32
        wchar_t buf[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (n > 0) return std::filesystem::path(std::wstring(buf, n)).parent_path();
#elif defined(__APPLE__)
        char buf[PATH_MAX];
        uint32_t size = sizeof(buf);
        if (_NSGetExecutablePath(buf, &size) == 0)
            return std::filesystem::canonical(buf, ec).parent_path();
#else
        auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
        if (!ec) return exe.parent_path();
#endif
        return std::filesystem::current_path(ec);
    }();
    return cached;
}

std::filesystem::path file(const char* name) { return appFolder() / name; }

std::filesystem::path gamesFolder() {
    std::filesystem::path p = appFolder() / "games";
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p;
}

std::vector<std::filesystem::path> listGames() {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator(gamesFolder(), ec))
        if (e.is_regular_file() && e.path().extension() == kExtension) out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

std::filesystem::path sibling(const char* programName) {
#ifdef _WIN32
    return appFolder() / (std::string(programName) + ".exe");
#else
    return appFolder() / programName;
#endif
}

bool launch(const std::filesystem::path& program, const std::string& argument) {
    std::error_code ec;
    if (!std::filesystem::exists(program, ec)) return false;
#ifdef _WIN32
    std::wstring args = L"\"" + program.wstring() + L"\"";
    if (!argument.empty()) args += L" \"" + std::filesystem::path(argument).wstring() + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring dir = program.parent_path().wstring();
    if (!CreateProcessW(nullptr, args.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    std::string cmd = "\"" + program.string() + "\"";
    if (!argument.empty()) cmd += " \"" + argument + "\"";
    cmd += " >/dev/null 2>&1 &";
    return std::system(cmd.c_str()) == 0;
#endif
}

} // namespace Paths
