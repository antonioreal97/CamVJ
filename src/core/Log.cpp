#include "core/Log.h"

#include <cstdarg>
#include <cstdio>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace atemfx {

namespace {

const char* levelPrefix(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Warn:  return "[warn ] ";
    case LogLevel::Error: return "[error] ";
    case LogLevel::Info:
    default:              return "[info ] ";
    }
}

} // namespace

void logMessage(LogLevel level, const char* format, ...)
{
    char message[1024];

    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    if (written < 0)
    {
        return;
    }

    char line[1152];
    std::snprintf(line, sizeof(line), "%s%s\n", levelPrefix(level), message);

#if defined(_WIN32)
    // Device names arrive as UTF-8. Write Unicode directly to a console so
    // diagnostics do not depend on the operator's OEM code page. Redirected
    // logs retain UTF-8, and the shared console configuration stays intact.
    wchar_t wideLine[1153];
    const int wideLength = ::MultiByteToWideChar(CP_UTF8, 0, line, -1, wideLine, 1152);
    if (wideLength > 1)
    {
        ::OutputDebugStringW(wideLine);
        const HANDLE output = ::GetStdHandle(level == LogLevel::Error
                                              ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (::GetConsoleMode(output, &mode))
        {
            // Console writes bypass the CRT's LF-to-CRLF translation.
            wideLine[wideLength - 2] = L'\r';
            wideLine[wideLength - 1] = L'\n';
            wideLine[wideLength] = L'\0';
            DWORD consoleWritten = 0;
            if (::WriteConsoleW(output, wideLine, static_cast<DWORD>(wideLength),
                                &consoleWritten, nullptr))
                return;
        }
    }
    else
        ::OutputDebugStringA(line);
#endif

    std::fputs(line, level == LogLevel::Error ? stderr : stdout);
    std::fflush(level == LogLevel::Error ? stderr : stdout);
}

} // namespace atemfx
