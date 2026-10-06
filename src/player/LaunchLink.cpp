#include "LaunchLink.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#ifdef __ANDROID__
#include <SDL.h>
#include <jni.h>
#endif
#if defined(__linux__) && !defined(__ANDROID__)
#include <unistd.h>
#endif

namespace LaunchLink {

namespace {
bool safeChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ':' || c == '.';
}
std::string unescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            out += (char)std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else out += s[i];
    }
    return out;
}
} // namespace

bool parse(const std::string& url, Link& out) {
    const std::string play = "gutsandbolts://play/", edit = "gutsandbolts://edit/";
    const bool editing = url.compare(0, edit.size(), edit) == 0;
    if (!editing && url.compare(0, play.size(), play) != 0) return false;
    std::string rest = url.substr(editing ? edit.size() : play.size());
    std::string query;
    if (size_t q = rest.find('?'); q != std::string::npos) { query = rest.substr(q + 1); rest = rest.substr(0, q); }
    while (!rest.empty() && rest.back() == '/') rest.pop_back();
    rest = unescape(rest);
    if (rest.empty() || rest.size() > 100) return false;
    for (char c : rest) if (!safeChar(c)) return false;   // ids only: nothing that could reach a shell or a path
    out.game = rest;
    out.edit = editing;
    out.guest.clear();
    out.server.clear();
    std::stringstream qs(query);
    std::string pair;
    while (std::getline(qs, pair, '&')) {
        if (pair == "guest=boy") out.guest = "boy";
        else if (pair == "guest=girl") out.guest = "girl";
        else if (pair.rfind("server=", 0) == 0 && pair.size() <= 7 + 40) {
            std::string sv = pair.substr(7);
            bool ok = !sv.empty();
            for (char c : sv) if (!safeChar(c)) ok = false;
            if (ok) out.server = sv;
        }
    }
    return true;
}

void registerScheme() {
#ifdef _WIN32
    wchar_t exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    std::wstring command = L"\"" + std::wstring(exe) + L"\" \"%1\"";
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\gutsandbolts", 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    const wchar_t* name = L"URL:Guts&Bolts";
    RegSetValueExW(key, nullptr, 0, REG_SZ, (const BYTE*)name, (DWORD)((wcslen(name) + 1) * sizeof(wchar_t)));
    RegSetValueExW(key, L"URL Protocol", 0, REG_SZ, (const BYTE*)L"", sizeof(wchar_t));
    RegCloseKey(key);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\gutsandbolts\\shell\\open\\command", 0, nullptr, 0, KEY_WRITE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    RegSetValueExW(key, nullptr, 0, REG_SZ, (const BYTE*)command.c_str(), (DWORD)((command.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
#elif defined(__linux__) && !defined(__ANDROID__)
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    const char* home = std::getenv("HOME");
    if (n <= 0 || !home) return;
    buf[n] = 0;
    std::string exe = buf;
    if (exe.find('"') != std::string::npos) return;
    namespace fs = std::filesystem;
    fs::path dir = fs::path(home) / ".local/share/applications";
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::path file = dir / "gutsandbolts-link.desktop";
    std::string text = "[Desktop Entry]\nType=Application\nName=Guts&Bolts Player\nExec=\"" + exe + "\" %u\n"
                       "NoDisplay=true\nMimeType=x-scheme-handler/gutsandbolts;\n";
    std::string old;
    { std::ifstream in(file); std::stringstream ss; ss << in.rdbuf(); old = ss.str(); }
    if (old == text) return;   // already set up
    { std::ofstream out(file); out << text; }
    std::system("xdg-mime default gutsandbolts-link.desktop x-scheme-handler/gutsandbolts >/dev/null 2>&1 &");
#endif
}

std::string poll() {
#ifdef __ANDROID__
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (!env || !activity) return "";
    std::string link;
    jclass cls = env->GetObjectClass(activity);
    jmethodID take = env->GetStaticMethodID(cls, "takeLaunchLink", "()Ljava/lang/String;");
    if (take) {
        jstring s = (jstring)env->CallStaticObjectMethod(cls, take);
        if (s && !env->ExceptionCheck()) {
            const char* c = env->GetStringUTFChars(s, nullptr);
            if (c) { link = c; env->ReleaseStringUTFChars(s, c); }
            env->DeleteLocalRef(s);
        }
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return link;
#else
    return "";
#endif
}

} // namespace LaunchLink
