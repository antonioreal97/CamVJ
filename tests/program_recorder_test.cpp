#include "video/program_recorder.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <system_error>

namespace {

int failures = 0;
int checks   = 0;

void expect(bool condition, const char* description)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", description);
    }
}

std::tm sampleTime()
{
    std::tm t{};
    t.tm_year = 2026 - 1900;
    t.tm_mon  = 8;   // September
    t.tm_mday = 18;
    t.tm_hour = 21;
    t.tm_min  = 4;
    t.tm_sec  = 3;
    return t;
}

void checkFileName()
{
    const std::string name = atemfx::recordingFileName(sampleTime());
    expect(name == "CamVJ 2026-09-18 21-04-03.mov", "the name is the zero-padded local timestamp");
    expect(name.find(':') == std::string::npos, "the name carries no ':' (exFAT, SMB, Windows)");
}

void touch(const std::filesystem::path& path)
{
    std::ofstream(path) << "x";
}

void checkNeverOverwrites()
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() /
        ("camvj-recorder-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;
    fs::create_directories(dir, ec);
    expect(!ec, "the scratch directory can be created");

    const fs::path first = atemfx::uniqueRecordingPath(dir, sampleTime());
    expect(first.filename() == "CamVJ 2026-09-18 21-04-03.mov", "a free name is used as is");

    touch(first);
    const fs::path second = atemfx::uniqueRecordingPath(dir, sampleTime());
    expect(second.filename() == "CamVJ 2026-09-18 21-04-03 2.mov",
           "a second take in the same second gets \" 2\"");

    touch(second);
    const fs::path third = atemfx::uniqueRecordingPath(dir, sampleTime());
    expect(third.filename() == "CamVJ 2026-09-18 21-04-03 3.mov", "and a third gets \" 3\"");

    fs::remove_all(dir, ec);
}

void checkTimeFormat()
{
    expect(atemfx::formatRecordingTime(0.0) == "00:00", "zero");
    expect(atemfx::formatRecordingTime(59.9) == "00:59", "seconds truncate, never round up");
    expect(atemfx::formatRecordingTime(123.0) == "02:03", "minutes and seconds");
    expect(atemfx::formatRecordingTime(3723.0) == "1:02:03", "hours appear only when reached");
    expect(atemfx::formatRecordingTime(-5.0) == "00:00", "negative time reads as zero");
    expect(atemfx::formatRecordingTime(std::nan("")) == "00:00", "NaN reads as zero");
}

void checkMinutesLeft()
{
    expect(atemfx::recordingMinutesLeft(1000, 0, 60.0) < 0.0, "no bytes yet: unknown");
    expect(atemfx::recordingMinutesLeft(1000, 1000, 1.0) < 0.0, "too early to trust a rate");

    // 55 MB/s is ProRes 422 HQ at 1080p59.94; 198 GB free is one hour of it.
    const double rate    = 55.0e6;
    const double seconds = 60.0;
    const double minutes = atemfx::recordingMinutesLeft(
        static_cast<uint64_t>(rate * 3600.0), static_cast<uint64_t>(rate * seconds), seconds);
    expect(std::abs(minutes - 60.0) < 1.0e-6, "free space divides by the observed rate");
}

} // namespace

int main()
{
    checkFileName();
    checkNeverOverwrites();
    checkTimeFormat();
    checkMinutesLeft();

    std::printf("program_recorder: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
