#include "presets/boot_state.h"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string_view>

#include "core/Log.h"
#include "core/Paths.h"

namespace atemfx {
namespace {

void appendEscaped(std::string& out, std::string_view text)
{
    out.push_back('"');
    for (char c : text)
    {
        if (c == '"' || c == '\\')
        {
            out.push_back('\\');
            out.push_back(c);
        }
        else
        {
            out.push_back(c);
        }
    }
    out.push_back('"');
}

struct Cursor
{
    std::string_view text;
    std::size_t      i = 0;
    std::string*     error = nullptr;

    bool fail(const char* message)
    {
        if (error) *error = message;
        return false;
    }

    void skipWs()
    {
        while (i < text.size() &&
               (text[i] == ' ' || text[i] == '\n' || text[i] == '\r' || text[i] == '\t'))
            ++i;
    }

    bool expect(char c)
    {
        skipWs();
        if (i >= text.size() || text[i] != c) return fail("unexpected token");
        ++i;
        return true;
    }

    bool peek(char c)
    {
        skipWs();
        return i < text.size() && text[i] == c;
    }

    bool parseString(std::string& out)
    {
        skipWs();
        if (i >= text.size() || text[i] != '"') return fail("expected string");
        ++i;
        out.clear();
        while (i < text.size())
        {
            const char c = text[i++];
            if (c == '"') return true;
            if (c == '\\')
            {
                if (i >= text.size()) return fail("unterminated escape");
                out.push_back(text[i++]);
            }
            else
            {
                out.push_back(c);
            }
        }
        return fail("unterminated string");
    }

    bool parseNumber(double& out)
    {
        skipWs();
        const std::size_t start = i;
        if (i < text.size() && (text[i] == '-' || text[i] == '+')) ++i;
        if (i >= text.size() || !std::isdigit(static_cast<unsigned char>(text[i])))
            return fail("expected number");
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
        if (i < text.size() && text[i] == '.')
        {
            ++i;
            while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
        }
        try
        {
            out = std::stod(std::string(text.substr(start, i - start)));
        }
        catch (...)
        {
            return fail("bad number");
        }
        return true;
    }

    bool parseBool(bool& out)
    {
        skipWs();
        if (text.substr(i).starts_with("true"))
        {
            i += 4;
            out = true;
            return true;
        }
        if (text.substr(i).starts_with("false"))
        {
            i += 5;
            out = false;
            return true;
        }
        return fail("expected boolean");
    }
};

std::filesystem::path bootPath()
{
    return userDataDirectory() / "boot.json";
}

} // namespace

std::string serializeBoot(const BootState& state)
{
    std::string out = "{\n  \"version\": 1,\n  \"sourceId\": ";
    appendEscaped(out, state.sourceId);
    out += ",\n  \"outputDisplayId\": ";
    appendEscaped(out, state.outputDisplayId);
    out += ",\n  \"portrait\": ";
    out += state.portrait ? "true" : "false";
    out += "\n}\n";
    return out;
}

bool parseBoot(std::string_view json, BootState& out, std::string& error)
{
    BootState state;
    Cursor    c{json, 0, &error};
    if (!c.expect('{')) return false;
    while (!c.peek('}'))
    {
        std::string key;
        if (!c.parseString(key) || !c.expect(':')) return false;
        if (key == "version")
        {
            double version = 0.0;
            if (!c.parseNumber(version)) return false;
            if (static_cast<int>(version) != BootState::kVersion)
            {
                error = "Unsupported boot version";
                return false;
            }
        }
        else if (key == "sourceId")
        {
            if (!c.parseString(state.sourceId)) return false;
        }
        else if (key == "outputDisplayId")
        {
            if (!c.parseString(state.outputDisplayId)) return false;
        }
        else if (key == "portrait")
        {
            if (!c.parseBool(state.portrait)) return false;
        }
        else
        {
            error = "Unknown boot field";
            return false;
        }
        if (c.peek(',')) c.expect(',');
    }
    if (!c.expect('}')) return false;
    out = std::move(state);
    return true;
}

bool loadBootState(BootState& out, std::string& error)
{
    const std::filesystem::path path = bootPath();
    std::ifstream               file(path);
    if (!file)
    {
        error = "No boot file";
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (!parseBoot(text, out, error))
    {
        ATEMFX_LOG_WARN("Ignoring boot.json: %s", error.c_str());
        return false;
    }
    return true;
}

bool saveBootState(const BootState& state, std::string& error)
{
    if (!ensureUserDataDirectory(error)) return false;
    const std::filesystem::path path = bootPath();
    const std::filesystem::path tmp  = path.string() + ".tmp";
    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            error = "Could not write " + tmp.string();
            return false;
        }
        file << serializeBoot(state);
        if (!file)
        {
            error = "Failed writing boot.json";
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec)
    {
        error = "Could not replace boot.json: " + ec.message();
        return false;
    }
    return true;
}

} // namespace atemfx
