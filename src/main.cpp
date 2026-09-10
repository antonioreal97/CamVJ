#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "app/App.h"
#include "core/Log.h"
#include "core/Version.h"
#include "gpu/Backend.h"
#include "platform/Display.h"
#include "video/VideoDevices.h"
#include "decklink/decklink_discovery.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

struct CommandLineOptions
{
    atemfx::AppOptions app;
    bool               listDeckLink = false;
    bool               listSources  = false;
    bool               listDisplays = false;
};

void printVideoSources()
{
    // Enumeration never opens a device, so this is safe to run anywhere and
    // never triggers a camera permission prompt. Only selecting a camera does.
    const std::vector<atemfx::VideoSourceDescriptor> sources = atemfx::enumerateVideoSources();

    // Pad by character count, not byte count: device names are UTF-8 and
    // "Câmera do MacBook Pro" is two bytes longer than it looks.
    const auto pad = [](const std::string& text, std::size_t width) {
        std::size_t characters = 0;
        for (const char byte : text)
        {
            if ((static_cast<unsigned char>(byte) & 0xC0) != 0x80)
            {
                ++characters;
            }
        }
        return text + std::string(characters < width ? width - characters : 1, ' ');
    };

    std::printf("%s%s%s\n", pad("CATEGORY", 12).c_str(), pad("NAME", 34).c_str(), "ID");
    for (const atemfx::VideoSourceDescriptor& source : sources)
    {
        std::printf("%s%s%s\n",
                    pad(source.category, 12).c_str(),
                    pad(source.displayName, 34).c_str(),
                    source.id.c_str());
    }
}

void printDisplays()
{
    // Enumeration opens no window and changes no display configuration.
    const std::vector<atemfx::DisplayInfo> displays = atemfx::enumerateDisplays();

    std::printf("%-10s %-30s %-12s %s\n", "ID", "NAME", "RESOLUTION", "REFRESH");
    for (const atemfx::DisplayInfo& display : displays)
    {
        char resolution[32];
        std::snprintf(resolution, sizeof(resolution), "%ux%u", display.width, display.height);

        char refresh[32];
        if (display.refreshHz > 0.0)
        {
            std::snprintf(refresh, sizeof(refresh), "%.2f Hz", display.refreshHz);
        }
        else
        {
            std::snprintf(refresh, sizeof(refresh), "-");
        }

        std::printf("%-10s %-30s %-12s %s%s\n",
                    display.id.c_str(),
                    display.name.c_str(),
                    resolution,
                    refresh,
                    display.primary ? "  (primary)" : "");
    }
}

// Which build is on this machine, in one line. At a show the answer has to be
// available without opening the app, so it is a command, not a UI corner.
void printVersion()
{
    // The backend is compiled in, so naming it here says as much about the
    // build as the number does.
    std::printf("CamVJ %s (%s)\n", atemfx::kVersion, atemfx::backendName());
}

void printUsage()
{
    std::printf(
        "CamVJ %s — live video FX engine\n"
        "\n"
        "  atem_fx [options]\n"
        "\n"
        "  --headless          run with no window or UI (self-test path)\n"
        "  --frames N          stop after N frames\n"
        "  --dump PATH         write the final frame as a binary PPM\n"
        "  --no-vsync          present without waiting for the display\n"
        "  --enable a,b,c      start with exactly these effects enabled\n"
        "  --source ID         start on this video input (see --list-sources)\n"
        "  --pattern NAME      test pattern: bars, plasma, grid or led-mapping\n"
        "  --output ID         send the processed frame to this display\n"
        "  --webcam            send PROGRAM as OBS Virtual Camera (macOS)\n"
        "  --program MODE      start PROGRAM in fx, clean, freeze or black\n"
        "  --list-sources      list the available video inputs and exit\n"
        "  --list-displays     list the available displays and exit\n"
        "  --check-shaders     compile every shader and exit (opens no device)\n"
        "  --list-decklink     list DeckLink devices and exit (Windows SDK build)\n"
        "  --version           print the version and exit\n"
        "  --help              show this message\n",
        atemfx::kVersion);
}

bool parseArguments(int argc, char** argv, CommandLineOptions& command, bool& shouldExit)
{
    auto& options = command.app;
    bool hasRenderOptions = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i];

        if (argument == "--help" || argument == "-h")
        {
            printUsage();
            shouldExit = true;
            return true;
        }
        else if (argument == "--version")
        {
            // Like --help: answered before anything else is parsed, so it can
            // never be refused for keeping bad company on the command line.
            printVersion();
            shouldExit = true;
            return true;
        }
        else if (argument == "--list-decklink")
        {
            command.listDeckLink = true;
        }
        else if (argument == "--list-sources")
        {
            command.listSources = true;
        }
        else if (argument == "--list-displays")
        {
            command.listDisplays = true;
        }
        else if (argument == "--check-shaders")
        {
            hasRenderOptions = true;
            options.checkShaders = true;
            options.headless     = true;
        }
        else if (argument == "--source" && i + 1 < argc)
        {
            hasRenderOptions = true;
            options.sourceId = argv[++i];
        }
        else if (argument == "--output" && i + 1 < argc)
        {
            hasRenderOptions = true;
            options.outputDisplayId = argv[++i];
        }
        else if (argument == "--pattern" && i + 1 < argc)
        {
            hasRenderOptions = true;
            const std::string pattern = argv[++i];
            if (pattern == "bars") options.testPattern = 0;
            else if (pattern == "plasma") options.testPattern = 1;
            else if (pattern == "grid") options.testPattern = 2;
            else if (pattern == "led-mapping") options.testPattern = 3;
            else
            {
                std::fprintf(stderr, "Invalid pattern: %s (use bars, plasma, grid or led-mapping)\n",
                              pattern.c_str());
                return false;
            }
        }
        else if (argument == "--headless")
        {
            hasRenderOptions = true;
            options.headless = true;
        }
        else if (argument == "--webcam")
        {
            hasRenderOptions = true;
            options.webcam = true;
        }
        else if (argument == "--program" && i + 1 < argc)
        {
            hasRenderOptions = true;
            const std::string mode = argv[++i];
            if (mode == "fx") options.programMode = atemfx::ProgramMode::Effects;
            else if (mode == "clean") options.programMode = atemfx::ProgramMode::Clean;
            else if (mode == "freeze") options.programMode = atemfx::ProgramMode::Freeze;
            else if (mode == "black") options.programMode = atemfx::ProgramMode::Black;
            else
            {
                std::fprintf(stderr, "Invalid PROGRAM mode: %s (use fx, clean, freeze or black)\n", mode.c_str());
                return false;
            }
        }
        else if (argument == "--no-vsync")
        {
            hasRenderOptions = true;
            options.vsync = false;
        }
        else if (argument == "--frames" && i + 1 < argc)
        {
            hasRenderOptions = true;
            options.frames = std::atoi(argv[++i]);
        }
        else if (argument == "--dump" && i + 1 < argc)
        {
            hasRenderOptions = true;
            options.dumpPath = argv[++i];
        }
        else if (argument == "--enable" && i + 1 < argc)
        {
            hasRenderOptions = true;
            const std::string list = argv[++i];
            std::size_t       start = 0;
            while (start <= list.size())
            {
                const std::size_t comma = list.find(',', start);
                const std::string item  = list.substr(start, comma - start);
                if (!item.empty())
                {
                    options.enabledEffects.push_back(item);
                }
                if (comma == std::string::npos)
                {
                    break;
                }
                start = comma + 1;
            }
        }
        else
        {
            std::fprintf(stderr, "Unknown or incomplete option: %s\n\n", argument.c_str());
            printUsage();
            return false;
        }
    }

    const int listCommands = (command.listDeckLink ? 1 : 0) + (command.listSources ? 1 : 0) +
                             (command.listDisplays ? 1 : 0);
    if (listCommands > 1)
    {
        std::fprintf(stderr, "The --list- options are separate commands.\n");
        return false;
    }
    if (listCommands > 0 && hasRenderOptions)
    {
        std::fprintf(stderr, "A --list- option cannot be combined with rendering options.\n");
        return false;
    }
    if (options.checkShaders && options.webcam)
    {
        std::fprintf(stderr, "--webcam cannot be combined with --check-shaders.\n");
        return false;
    }
    if (options.testPattern >= 0 && !options.sourceId.empty() &&
        options.sourceId != atemfx::kTestPatternSourceId)
    {
        std::fprintf(stderr, "--pattern requires the Test Pattern input.\n");
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
#if defined(_WIN32)
    // Per-monitor DPI awareness: without it Windows scales the window and the
    // preview is resampled twice.
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif

    CommandLineOptions options;
    bool               shouldExit = false;
    if (!parseArguments(argc, argv, options, shouldExit))
    {
        return 2;
    }
    if (shouldExit)
    {
        return 0;
    }

    if (options.listSources)
    {
        printVideoSources();
        return 0;
    }

    if (options.listDisplays)
    {
        printDisplays();
        return 0;
    }

    if (options.listDeckLink)
    {
        return atemfx::runDeckLinkDiscovery();
    }

    atemfx::App app;

    if (!app.initialize(options.app))
    {
        ATEMFX_LOG_ERROR("Initialisation failed. See the log above.");
        app.shutdown();
        return 1;
    }

    const int result = app.run();

    app.shutdown();
    return result;
}
