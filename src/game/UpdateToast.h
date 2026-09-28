#pragma once

// The little "Update available" card that slides in at the bottom-right.
// `appName` is which program to reopen after updating.
namespace UpdateToast {
// Returns true if the user chose "Update now" (the app should close).
bool draw(const char* appName);
}
