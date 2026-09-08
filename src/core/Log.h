#pragma once

// Minimal diagnostic logging.
//
// AGENTS.md names spdlog as the project logger. It is deliberately not pulled
// in for M0: nothing here needs sinks, formatting or async logging, and M0
// should not carry a dependency it does not use. The call shape below matches
// spdlog's, so the switch in M1 (when hardware logging actually matters) is
// mechanical.
//
// Messages are UTF-8; Windows consoles/debuggers receive Unicode, while
// redirected output keeps UTF-8. Not for use inside the per-frame hot path.

namespace atemfx {

enum class LogLevel
{
    Info,
    Warn,
    Error,
};

void logMessage(LogLevel level, const char* format, ...);

#define ATEMFX_LOG_INFO(...)  ::atemfx::logMessage(::atemfx::LogLevel::Info, __VA_ARGS__)
#define ATEMFX_LOG_WARN(...)  ::atemfx::logMessage(::atemfx::LogLevel::Warn, __VA_ARGS__)
#define ATEMFX_LOG_ERROR(...) ::atemfx::logMessage(::atemfx::LogLevel::Error, __VA_ARGS__)

} // namespace atemfx
