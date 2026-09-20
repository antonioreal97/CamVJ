#include "video/program_recorder.h"

#include <cmath>
#include <cstdio>
#include <system_error>

namespace atemfx {

std::string recordingFileName(const std::tm& t)
{
    char name[64];
    std::snprintf(name, sizeof(name), "CamVJ %04d-%02d-%02d %02d-%02d-%02d.mov",
                  t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    return name;
}

std::filesystem::path uniqueRecordingPath(const std::filesystem::path& directory,
                                          const std::tm&               localTime)
{
    const std::string     name = recordingFileName(localTime);
    std::filesystem::path path = directory / name;

    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
    {
        return path;
    }

    const std::string stem = name.substr(0, name.size() - 4); // without ".mov"
    for (int take = 2; take < 1000; ++take)
    {
        path = directory / (stem + " " + std::to_string(take) + ".mov");
        if (!std::filesystem::exists(path, ec))
        {
            return path;
        }
    }
    // A thousand takes in one second is not a show; let the writer refuse it.
    return path;
}

std::string formatRecordingTime(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0)
    {
        seconds = 0.0;
    }
    const long long total   = static_cast<long long>(seconds);
    const long long hours   = total / 3600;
    const long long minutes = (total / 60) % 60;
    const long long secs    = total % 60;

    char text[32];
    if (hours > 0)
    {
        std::snprintf(text, sizeof(text), "%lld:%02lld:%02lld", hours, minutes, secs);
    }
    else
    {
        std::snprintf(text, sizeof(text), "%02lld:%02lld", minutes, secs);
    }
    return text;
}

double recordingMinutesLeft(uint64_t freeBytes, uint64_t fileBytes, double seconds)
{
    // Under a few seconds the file is mostly header and the first fragment
    // has not been flushed; a rate from that would be fiction.
    if (!std::isfinite(seconds) || seconds < 5.0 || fileBytes == 0)
    {
        return -1.0;
    }
    const double bytesPerSecond = static_cast<double>(fileBytes) / seconds;
    return static_cast<double>(freeBytes) / bytesPerSecond / 60.0;
}

} // namespace atemfx
