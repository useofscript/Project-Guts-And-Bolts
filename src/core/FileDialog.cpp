#include "FileDialog.h"

#if !defined(GB_MOBILE)
#include <portable-file-dialogs.h>
#endif

namespace FileDialog {

#if defined(GB_MOBILE)
bool available() { return false; }
std::string openAny(const std::string&, const std::string&, const std::string&) { return {}; }
#else
bool available() { return pfd::settings::available(); }

std::string openAny(const std::string& title, const std::string& what, const std::string& patterns) {
    auto picked = pfd::open_file(title, "", {what, patterns, "All files", "*"}).result();
    return picked.empty() ? std::string() : picked[0];
}
#endif

std::string openImage(const std::string& title) {
    return openAny(title, "Pictures", "*.png *.jpg *.jpeg *.bmp *.tga *.gif");
}

std::string openAudio(const std::string& title) {
    return openAny(title, "Sounds", "*.wav *.mp3 *.flac *.ogg");
}

} // namespace FileDialog
