#pragma once

// Opt-in runtime diagnostics.
//
// The overlay is a GUI-subsystem app with no console, and its whole value is
// invisible when something breaks — "nothing appeared" has many possible causes
// spread across the dwell coordinator, the STA worker, the message marshalling
// and the layered-window composition. This writes a timestamped trace of every
// pipeline stage so a failure can be localized instead of guessed at.
//
// Disabled unless the environment variable CONTEXT_OVERLAY_DIAG=1 is set, so
// shipped behaviour is unchanged. Log path:
//   %LOCALAPPDATA%\context-overlay\diag.log   (falls back to %TEMP%)
//
// Every entry is also sent to OutputDebugStringW for DebugView.

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cwchar>

namespace diag {

inline bool enabled() {
    static int cached = -1;
    if (cached < 0) {
        wchar_t buf[8]{};
        const DWORD n =
            GetEnvironmentVariableW(L"CONTEXT_OVERLAY_DIAG", buf, 8);
        cached = (n > 0 && buf[0] == L'1') ? 1 : 0;
    }
    return cached == 1;
}

inline const wchar_t* logPath() {
    static wchar_t path[MAX_PATH]{};
    if (path[0]) return path;

    wchar_t base[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        n = GetTempPathW(MAX_PATH, base);
        if (n == 0 || n >= MAX_PATH) return nullptr;
        wsprintfW(path, L"%scontext-overlay-diag.log", base);
        return path;
    }
    wchar_t dir[MAX_PATH]{};
    wsprintfW(dir, L"%s\\context-overlay", base);
    CreateDirectoryW(dir, nullptr);  // ok if it already exists
    wsprintfW(path, L"%s\\diag.log", dir);
    return path;
}

// Append one line. Uses a mutex because STA workers log concurrently with the
// UI thread, and interleaved writes would make the trace useless.
inline void writeLine(const wchar_t* text) {
    if (!enabled()) return;

    OutputDebugStringW(text);
    OutputDebugStringW(L"\n");

    static HANDLE mutex = CreateMutexW(nullptr, FALSE, nullptr);
    if (mutex) WaitForSingleObject(mutex, 1000);

    const wchar_t* path = logPath();
    if (path) {
        HANDLE f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            SYSTEMTIME st{};
            GetLocalTime(&st);
            wchar_t line[1024];
            // _snwprintf (CRT) rather than wsprintfW: the Win32 formatter does
            // not support 64-bit specifiers such as %llu / %016llX.
            const int len = _snwprintf(line, 1024,
                                       L"%02u:%02u:%02u.%03u  t%-5lu  %s\r\n",
                                       st.wHour, st.wMinute, st.wSecond,
                                       st.wMilliseconds, GetCurrentThreadId(),
                                       text);
            if (len <= 0) { CloseHandle(f); if (mutex) ReleaseMutex(mutex); return; }
            // Write UTF-8 so the log opens cleanly in any editor.
            char utf8[2048];
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, line, len, utf8,
                                                  sizeof(utf8), nullptr, nullptr);
            if (bytes > 0) {
                DWORD written = 0;
                WriteFile(f, utf8, static_cast<DWORD>(bytes), &written, nullptr);
            }
            CloseHandle(f);
        }
    }

    if (mutex) ReleaseMutex(mutex);
}

inline void logf(const wchar_t* fmt, ...) {
    if (!enabled()) return;
    wchar_t buf[900];
    va_list args;
    va_start(args, fmt);
    // _vsnwprintf, not wvsprintfW: the Win32 formatter silently mangles 64-bit
    // specifiers (%llu / %016llX), which the identity hash and counters need.
    _vsnwprintf(buf, 900, fmt, args);
    buf[899] = L'\0';
    va_end(args);
    writeLine(buf);
}

}  // namespace diag
