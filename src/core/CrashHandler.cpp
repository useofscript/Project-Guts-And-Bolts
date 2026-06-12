#include "CrashHandler.h"

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstdarg>

namespace {

FILE* g_logFile = nullptr;

void emit(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vfprintf(stderr, fmt, a);
    va_end(a);
    if (g_logFile) {
        va_start(a, fmt);
        vfprintf(g_logFile, fmt, a);
        va_end(a);
    }
}

LONG WINAPI handler(EXCEPTION_POINTERS* info) {
    g_logFile = std::fopen("crash_log.txt", "w");

    HANDLE proc   = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, nullptr, TRUE);

    const auto* rec = info->ExceptionRecord;
    emit("\n=== CRASH ===\n");
    emit("Exception 0x%08lX at %p\n",
         rec->ExceptionCode, rec->ExceptionAddress);
    if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        rec->NumberParameters >= 2) {
        emit("Access violation %s address %p\n",
             rec->ExceptionInformation[0] ? "writing" : "reading",
             (void*)rec->ExceptionInformation[1]);
    }

    CONTEXT* ctx = info->ContextRecord;
    STACKFRAME64 frame = {};
    frame.AddrPC.Offset    = ctx->Rip; frame.AddrPC.Mode    = AddrModeFlat;
    frame.AddrFrame.Offset = ctx->Rbp; frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx->Rsp; frame.AddrStack.Mode = AddrModeFlat;

    char symBuf[sizeof(SYMBOL_INFO) + 256] = {};
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen   = 255;

    emit("Stack trace:\n");
    for (int i = 0; i < 48; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, thread, &frame, ctx,
                         nullptr, SymFunctionTableAccess64, SymGetModuleBase64,
                         nullptr))
            break;
        DWORD64 pc = frame.AddrPC.Offset;
        if (pc == 0) break;

        DWORD64 disp = 0;
        const char* name = "<unknown>";
        if (SymFromAddr(proc, pc, &disp, sym)) name = sym->Name;

        IMAGEHLP_LINE64 line = {};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisp = 0;
        if (SymGetLineFromAddr64(proc, pc, &lineDisp, &line))
            emit("  [%2d] %s  (%s:%lu)\n", i, name, line.FileName, line.LineNumber);
        else
            emit("  [%2d] %s +0x%llX\n", i, name, (unsigned long long)disp);
    }
    emit("=== end ===\n");

    if (g_logFile) { std::fclose(g_logFile); g_logFile = nullptr; }
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

namespace CrashHandler {
void install() { SetUnhandledExceptionFilter(handler); }
}

#else
namespace CrashHandler {
void install() {}
}
#endif
