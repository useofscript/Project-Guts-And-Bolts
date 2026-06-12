#pragma once

// Installs a last-chance handler that prints a symbolized stack trace (to
// stderr and crash_log.txt) when the process faults. No-op on non-Windows.
namespace CrashHandler {
void install();
}
