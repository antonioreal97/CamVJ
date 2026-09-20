#include "presets/scene_preset.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "effects/Effect.h"
#include "effects/EffectParameters.h"

namespace atemfx {
namespace {

void appendEscaped(std::string& out, std::string_view text)
{
    out.push_back('"');
    for (char c : text)
    {
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                }
                else
                {
                    out.push_back(c);
                }
                break;
        }
    }
    out.push_back('"');
}

void appendFloat(std::string& out, double value)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.9g", value);
    out += buf;
}

struct JsonCursor
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
        {
            ++i;
        }
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
                const char e = text[i++];
                switch (e)
                {
                    case '"':
                    case '\\':
                    case '/': out.push_back(e); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u':
                        if (i + 4 > text.size()) return fail("bad unicode escape");
                        i += 4; // ASCII-only presets; skip the code unit.
                        out.push_back('?');
                        break;
                    default: return fail("bad escape");
                }
            }
            else if (static_cast<unsigned char>(c) < 0x20)
            {
                return fail("control character in string");
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
        if (i < text.size() && (text[i] == 'e' || text[i] == 'E'))
        {
            ++i;
            if (i < text.size() && (text[i] == '+' || text[i] == '-')) ++i;
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

    bool parseNull()
    {
        skipWs();
        if (text.substr(i).starts_with("null"))
        {
            i += 4;
            return true;
        }
        return fail("expected null");
    }
};

bool parseParamObject(JsonCursor& c, SceneParamState& param)
{
    if (!c.expect('{')) return false;
    bool sawValue = false;
    while (!c.peek('}'))
    {
        std::string key;
        if (!c.parseString(key) || !c.expect(':')) return false;
        if (key == "value")
        {
            double number = 0.0;
            if (!c.parseNumber(number)) return false;
            param.value = static_cast<float>(number);
            sawValue = true;
        }
        else if (key == "automation")
        {
            if (!c.expect('{')) return false;
            while (!c.peek('}'))
            {
                std::string aKey;
                if (!c.parseString(aKey) || !c.expect(':')) return false;
                if (aKey == "enabled")
                {
                    if (!c.parseBool(param.automationEnabled)) return false;
                }
                else if (aKey == "paused")
                {
                    if (!c.parseBool(param.automationPaused)) return false;
                }
                else if (aKey == "waveform")
                {
                    std::string token;
                    if (!c.parseString(token)) return false;
                    if (!waveformFromToken(token, param.waveform))
                        return c.fail("unknown waveform");
                }
                else if (aKey == "periodSeconds")
                {
                    double number = 0.0;
                    if (!c.parseNumber(number)) return false;
                    param.periodSeconds = number;
                }
                else if (aKey == "minValue")
                {
                    double number = 0.0;
                    if (!c.parseNumber(number)) return false;
                    param.automationMin = static_cast<float>(number);
                }
                else if (aKey == "maxValue")
                {
                    double number = 0.0;
                    if (!c.parseNumber(number)) return false;
                    param.automationMax = static_cast<float>(number);
                }
                else if (aKey == "phaseOffset")
                {
                    double number = 0.0;
                    if (!c.parseNumber(number)) return false;
                    param.phaseOffset = static_cast<float>(number);
                }
                else
                {
                    return c.fail("unknown automation field");
                }
                if (c.peek(',')) c.expect(',');
            }
            if (!c.expect('}')) return false;
        }
        else
        {
            return c.fail("unknown param field");
        }
        if (c.peek(',')) c.expect(',');
    }
    if (!sawValue) return c.fail("param missing value");
    return c.expect('}');
}

bool parseEffectObject(JsonCursor& c, SceneEffectState& effect)
{
    if (!c.expect('{')) return false;
    while (!c.peek('}'))
    {
        std::string key;
        if (!c.parseString(key) || !c.expect(':')) return false;
        if (key == "type")
        {
            if (!c.parseString(effect.typeId)) return false;
        }
        else if (key == "enabled")
        {
            if (!c.parseBool(effect.enabled)) return false;
        }
        else if (key == "params")
        {
            if (!c.expect('{')) return false;
            while (!c.peek('}'))
            {
                SceneParamState param;
                if (!c.parseString(param.id) || !c.expect(':')) return false;
                if (!parseParamObject(c, param)) return false;
                effect.params.push_back(std::move(param));
                if (c.peek(',')) c.expect(',');
            }
            if (!c.expect('}')) return false;
        }
        else
        {
            return c.fail("unknown effect field");
        }
        if (c.peek(',')) c.expect(',');
    }
    if (effect.typeId.empty()) return c.fail("effect missing type");
    return c.expect('}');
}

bool parseOverlayObject(JsonCursor& c, SceneOverlayState& overlay)
{
    if (!c.expect('{')) return false;
    bool sawAssetId = false;
    while (!c.peek('}'))
    {
        std::string key;
        if (!c.parseString(key) || !c.expect(':')) return false;
        if (key == "assetId")
        {
            if (!c.parseString(overlay.assetId)) return false;
            sawAssetId = !overlay.assetId.empty();
        }
        else if (key == "enabled")
        {
            if (!c.parseBool(overlay.enabled)) return false;
        }
        else if (key == "opacity")
        {
            double number = 0.0;
            if (!c.parseNumber(number)) return false;
            overlay.opacity = static_cast<float>(number);
        }
        else if (key == "playback")
        {
            std::string token;
            if (!c.parseString(token)) return false;
            if (!overlayPlaybackFromToken(token, overlay.playback))
                return c.fail("unknown overlay playback");
        }
        else if (key == "fps")
        {
            double number = 0.0;
            if (!c.parseNumber(number)) return false;
            overlay.framesPerSecond = static_cast<float>(number);
        }
        else
        {
            return c.fail("unknown overlay field");
        }
        if (c.peek(',')) c.expect(',');
    }
    if (!c.expect('}')) return false;
    if (!sawAssetId) return c.fail("overlay missing assetId");
    if (!std::isfinite(overlay.opacity) || overlay.opacity < 0.0f || overlay.opacity > 1.0f)
        return c.fail("overlay opacity must be between 0 and 1");
    if (!std::isfinite(overlay.framesPerSecond) || overlay.framesPerSecond < 1.0f ||
        overlay.framesPerSecond > kOverlaySequenceFps)
        return c.fail("overlay fps must be between 1 and 30");
    return true;
}

} // namespace

const char* programModeToToken(ProgramMode mode)
{
    switch (mode)
    {
        case ProgramMode::Clean:  return "clean";
        case ProgramMode::Freeze: return "freeze";
        case ProgramMode::Black:  return "black";
        case ProgramMode::Effects:
        default:                  return "fx";
    }
}

bool programModeFromToken(std::string_view token, ProgramMode& out)
{
    if (token == "fx" || token == "effects") { out = ProgramMode::Effects; return true; }
    if (token == "clean") { out = ProgramMode::Clean; return true; }
    if (token == "freeze") { out = ProgramMode::Freeze; return true; }
    if (token == "black") { out = ProgramMode::Black; return true; }
    return false;
}

const char* waveformToToken(AutomationWaveform waveform)
{
    switch (waveform)
    {
        case AutomationWaveform::Triangle: return "triangle";
        case AutomationWaveform::RampUp:   return "ramp_up";
        case AutomationWaveform::RampDown: return "ramp_down";
        case AutomationWaveform::Square:   return "square";
        case AutomationWaveform::Sine:
        default:                           return "sine";
    }
}

bool waveformFromToken(std::string_view token, AutomationWaveform& out)
{
    if (token == "sine") { out = AutomationWaveform::Sine; return true; }
    if (token == "triangle") { out = AutomationWaveform::Triangle; return true; }
    if (token == "ramp_up") { out = AutomationWaveform::RampUp; return true; }
    if (token == "ramp_down") { out = AutomationWaveform::RampDown; return true; }
    if (token == "square") { out = AutomationWaveform::Square; return true; }
    return false;
}

const char* overlayPlaybackToToken(OverlayPlayback playback)
{
    return playback == OverlayPlayback::OneShot ? "one_shot" : "loop";
}

bool overlayPlaybackFromToken(std::string_view token, OverlayPlayback& out)
{
    if (token == "loop")
    {
        out = OverlayPlayback::Loop;
        return true;
    }
    if (token == "one_shot")
    {
        out = OverlayPlayback::OneShot;
        return true;
    }
    return false;
}

ScenePreset captureScene(const EffectChain& chain, ProgramMode program,
                         const OverlayStackConfig& overlays,
                         std::string id, std::string name)
{
    ScenePreset preset;
    preset.id   = std::move(id);
    preset.name = std::move(name);
    // Looks are FX or Clean. Safety modes are not saved into a recall slot.
    preset.program = (program == ProgramMode::Clean) ? ProgramMode::Clean
                                                     : ProgramMode::Effects;

    for (std::size_t i = 0; i < chain.size(); ++i)
    {
        const Effect& effect = chain.at(i);
        SceneEffectState node;
        node.typeId  = effect.descriptor().typeId;
        node.enabled = effect.enabled();
        for (const Parameter& parameter : effect.parameters().all())
        {
            SceneParamState param;
            param.id    = parameter.id;
            param.value = parameter.value;
            if (parameter.automation.enabled)
            {
                param.automationEnabled = true;
                param.automationPaused  = parameter.automation.paused;
                param.waveform          = parameter.automation.waveform;
                param.periodSeconds     = parameter.automation.periodSeconds;
                param.automationMin     = parameter.automation.minValue;
                param.automationMax     = parameter.automation.maxValue;
                param.phaseOffset       = parameter.automation.phaseOffset;
            }
            node.params.push_back(std::move(param));
        }
        preset.effects.push_back(std::move(node));
    }

    const std::size_t overlayCount = std::min(overlays.layerCount, kMaxOverlayLayers);
    for (std::size_t i = 0; i < overlayCount; ++i)
    {
        const OverlayLayerConfig& layer = overlays.layers[i];
        if (layer.assetId.empty()) continue;
        SceneOverlayState state;
        state.assetId          = layer.assetId;
        state.enabled          = layer.enabled;
        state.opacity          = std::clamp(layer.opacity, 0.0f, 1.0f);
        state.playback         = layer.playback;
        state.framesPerSecond  = std::clamp(layer.framesPerSecond, 1.0f,
                                            kOverlaySequenceFps);
        preset.overlays.push_back(std::move(state));
    }
    return preset;
}

ScenePreset captureScene(const EffectChain& chain, ProgramMode program,
                         std::string id, std::string name)
{
    return captureScene(chain, program, OverlayStackConfig{}, std::move(id), std::move(name));
}

bool applyScene(const ScenePreset& preset, EffectChain& chain, EffectContext& context,
                ProgramMode& program, ProgramTransition& transition, std::string& error)
{
    if (preset.effects.empty())
    {
        error = "Preset has no effects";
        return false;
    }

    EffectChain rebuilt;
    for (const SceneEffectState& node : preset.effects)
    {
        if (!rebuilt.addByType(node.typeId, context, error))
        {
            error = "Unknown or failed effect '" + node.typeId + "': " + error;
            return false;
        }
        Effect& effect = rebuilt.at(rebuilt.size() - 1);
        effect.setEnabled(node.enabled);
        for (const SceneParamState& param : node.params)
        {
            Parameter* live = effect.parameters().find(param.id);
            if (!live) continue;
            live->value = param.value;
            live->automation = {};
            live->automation.minValue = live->minValue;
            live->automation.maxValue = live->maxValue;
            if (live->type == ParameterType::Bool)
            {
                live->automation.waveform = AutomationWaveform::Square;
            }
            if (param.automationEnabled)
            {
                live->automation.enabled     = true;
                live->automation.paused      = param.automationPaused;
                live->automation.waveform    = param.waveform;
                live->automation.periodSeconds = param.periodSeconds;
                live->automation.minValue    = param.automationMin;
                live->automation.maxValue    = param.automationMax;
                live->automation.phaseOffset = param.phaseOffset;
                live->automation.restart();
            }
        }
    }

    chain.shutdown();
    chain = std::move(rebuilt);
    chain.prepare(context);

    ProgramMode target = preset.program;
    if (target != ProgramMode::Effects && target != ProgramMode::Clean)
    {
        target = ProgramMode::Effects;
    }
    program = target;
    transition.snapTo(target);
    return true;
}

std::string serializeScene(const ScenePreset& preset)
{
    std::string out;
    out += "{\n  \"version\": ";
    appendFloat(out, ScenePreset::kVersion);
    out += ",\n  \"id\": ";
    appendEscaped(out, preset.id);
    out += ",\n  \"name\": ";
    appendEscaped(out, preset.name);
    out += ",\n  \"factory\": ";
    out += preset.factory ? "true" : "false";
    out += ",\n  \"program\": ";
    appendEscaped(out, programModeToToken(preset.program));
    out += ",\n  \"effects\": [\n";
    for (std::size_t e = 0; e < preset.effects.size(); ++e)
    {
        const SceneEffectState& effect = preset.effects[e];
        out += "    {\n      \"type\": ";
        appendEscaped(out, effect.typeId);
        out += ",\n      \"enabled\": ";
        out += effect.enabled ? "true" : "false";
        out += ",\n      \"params\": {\n";
        for (std::size_t p = 0; p < effect.params.size(); ++p)
        {
            const SceneParamState& param = effect.params[p];
            out += "        ";
            appendEscaped(out, param.id);
            out += ": {\n          \"value\": ";
            appendFloat(out, param.value);
            if (param.automationEnabled)
            {
                out += ",\n          \"automation\": {\n";
                out += "            \"enabled\": true,\n";
                out += "            \"paused\": ";
                out += param.automationPaused ? "true" : "false";
                out += ",\n            \"waveform\": ";
                appendEscaped(out, waveformToToken(param.waveform));
                out += ",\n            \"periodSeconds\": ";
                appendFloat(out, param.periodSeconds);
                out += ",\n            \"minValue\": ";
                appendFloat(out, param.automationMin);
                out += ",\n            \"maxValue\": ";
                appendFloat(out, param.automationMax);
                out += ",\n            \"phaseOffset\": ";
                appendFloat(out, param.phaseOffset);
                out += "\n          }";
            }
            out += "\n        }";
            if (p + 1 < effect.params.size()) out += ',';
            out += '\n';
        }
        out += "      }\n    }";
        if (e + 1 < preset.effects.size()) out += ',';
        out += '\n';
    }
    out += "  ],\n  \"overlays\": [\n";
    for (std::size_t i = 0; i < preset.overlays.size(); ++i)
    {
        const SceneOverlayState& overlay = preset.overlays[i];
        out += "    {\"assetId\": ";
        appendEscaped(out, overlay.assetId);
        out += ", \"enabled\": ";
        out += overlay.enabled ? "true" : "false";
        out += ", \"opacity\": ";
        appendFloat(out, overlay.opacity);
        out += ", \"playback\": ";
        appendEscaped(out, overlayPlaybackToToken(overlay.playback));
        out += ", \"fps\": ";
        appendFloat(out, overlay.framesPerSecond);
        out += '}';
        if (i + 1 < preset.overlays.size()) out += ',';
        out += '\n';
    }
    out += "  ]\n}\n";
    return out;
}

bool parseScene(std::string_view json, ScenePreset& out, std::string& error)
{
    ScenePreset preset;
    JsonCursor  c{json, 0, &error};
    int         parsedVersion = 0;
    bool        sawVersion    = false;
    bool        sawOverlays   = false;
    if (!c.expect('{')) return false;

    while (!c.peek('}'))
    {
        std::string key;
        if (!c.parseString(key) || !c.expect(':')) return false;
        if (key == "version")
        {
            double version = 0.0;
            if (!c.parseNumber(version)) return false;
            parsedVersion = static_cast<int>(version);
            sawVersion    = true;
            if (version != static_cast<double>(parsedVersion) ||
                (parsedVersion != 1 && parsedVersion != ScenePreset::kVersion))
            {
                error = "Unsupported preset version";
                return false;
            }
        }
        else if (key == "id")
        {
            if (!c.parseString(preset.id)) return false;
        }
        else if (key == "name")
        {
            if (!c.parseString(preset.name)) return false;
        }
        else if (key == "factory")
        {
            if (!c.parseBool(preset.factory)) return false;
        }
        else if (key == "program")
        {
            std::string token;
            if (!c.parseString(token)) return false;
            if (!programModeFromToken(token, preset.program))
            {
                error = "Unknown program mode";
                return false;
            }
        }
        else if (key == "effects")
        {
            if (!c.expect('[')) return false;
            while (!c.peek(']'))
            {
                SceneEffectState effect;
                if (!parseEffectObject(c, effect)) return false;
                preset.effects.push_back(std::move(effect));
                if (c.peek(',')) c.expect(',');
            }
            if (!c.expect(']')) return false;
        }
        else if (key == "overlays")
        {
            sawOverlays = true;
            if (!c.expect('[')) return false;
            while (!c.peek(']'))
            {
                if (preset.overlays.size() >= kMaxOverlayLayers)
                    return c.fail("preset has more than four overlays");
                SceneOverlayState overlay;
                if (!parseOverlayObject(c, overlay)) return false;
                preset.overlays.push_back(std::move(overlay));
                if (c.peek(',')) c.expect(',');
            }
            if (!c.expect(']')) return false;
        }
        else
        {
            error = "Unknown preset field: " + key;
            return false;
        }
        if (c.peek(',')) c.expect(',');
    }
    if (!c.expect('}')) return false;
    c.skipWs();
    if (c.i != c.text.size())
    {
        error = "Trailing data after preset JSON";
        return false;
    }
    if (!sawVersion)
    {
        error = "Preset missing version";
        return false;
    }
    if (parsedVersion == 1 && sawOverlays)
    {
        error = "Preset version 1 cannot contain overlays";
        return false;
    }
    std::unordered_set<std::string> overlayIds;
    for (const SceneOverlayState& overlay : preset.overlays)
    {
        if (!overlayIds.insert(overlay.assetId).second)
        {
            error = "Preset contains the same overlay more than once";
            return false;
        }
    }
    if (preset.id.empty() || preset.name.empty() || preset.effects.empty())
    {
        error = "Preset needs id, name and effects";
        return false;
    }
    out = std::move(preset);
    return true;
}

} // namespace atemfx
