#include "ui/Panels.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

#include "imgui.h"
#include "ui/Fonts.h"
#include "ui/Inspector.h"
#include "ui/Theme.h"

namespace atemfx {
namespace {

constexpr std::size_t kMaxLayers = 4;

uint64_t g_seenImportSerial = 0;
char     g_libraryFilter[96] = {};

struct PendingVariant
{
    bool                active = false;
    std::string         assetId;
    OverlayMediaType    mediaType = OverlayMediaType::StillPng;
    OverlayCanvasFormat format = OverlayCanvasFormat::Landscape16x9;
};

PendingVariant g_pendingLiveReplace;
std::string    g_pendingRemoveAsset;

const char* mediaLabel(OverlayMediaType type)
{
    return type == OverlayMediaType::PngSequence ? "SEQ" : "PNG";
}

const char* mediaName(OverlayMediaType type)
{
    return type == OverlayMediaType::PngSequence ? "PNG sequence" : "Static PNG";
}

const char* formatLabel(OverlayCanvasFormat format)
{
    return format == OverlayCanvasFormat::Portrait9x16 ? "9:16" : "16:9";
}

bool importBusy(const OverlayPanelSnapshot& snapshot)
{
    return snapshot.import.phase != OverlayImportPhase::Idle &&
           snapshot.import.phase != OverlayImportPhase::Failed;
}

const char* importPhaseLabel(OverlayImportPhase phase)
{
    switch (phase)
    {
    case OverlayImportPhase::Picking:    return "PICKING";
    case OverlayImportPhase::Validating: return "VALIDATING";
    case OverlayImportPhase::Copying:    return "COPYING";
    case OverlayImportPhase::Preparing:  return "PREPARING";
    case OverlayImportPhase::Failed:     return "IMPORT FAILED";
    case OverlayImportPhase::Idle:       return "";
    }
    return "";
}

OverlayUiCommand* command(UiFrameState& state, OverlayUiCommandType type)
{
    if (!state.overlayCommand) return nullptr;
    *state.overlayCommand = {};
    state.overlayCommand->type = type;
    return state.overlayCommand;
}

const OverlayLayerUiState* findLayer(const OverlayPanelSnapshot& snapshot, uint64_t id)
{
    if (!snapshot.layers) return nullptr;
    for (const OverlayLayerUiState& layer : *snapshot.layers)
    {
        if (layer.id == id) return &layer;
    }
    return nullptr;
}

const OverlayAssetUiState* findAsset(const OverlayPanelSnapshot& snapshot,
                                     std::string_view assetId)
{
    if (!snapshot.assets) return nullptr;
    for (const OverlayAssetUiState& asset : *snapshot.assets)
    {
        if (asset.id == assetId) return &asset;
    }
    return nullptr;
}

const OverlayVariantUiState& variantFor(const OverlayAssetUiState& asset,
                                        OverlayCanvasFormat format)
{
    return format == OverlayCanvasFormat::Portrait9x16 ? asset.portrait : asset.landscape;
}

bool containsCaseInsensitive(std::string_view text, std::string_view needle)
{
    if (needle.empty()) return true;
    if (needle.size() > text.size()) return false;

    for (std::size_t start = 0; start + needle.size() <= text.size(); ++start)
    {
        bool match = true;
        for (std::size_t i = 0; i < needle.size(); ++i)
        {
            const unsigned char a = static_cast<unsigned char>(text[start + i]);
            const unsigned char b = static_cast<unsigned char>(needle[i]);
            if (std::tolower(a) != std::tolower(b))
            {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

bool stackContains(const OverlayPanelSnapshot& snapshot, std::string_view assetId)
{
    if (!snapshot.layers) return false;
    for (const OverlayLayerUiState& layer : *snapshot.layers)
    {
        if (layer.assetId == assetId) return true;
    }
    return false;
}

bool assetIsLive(const UiFrameState& state, const OverlayPanelSnapshot& snapshot,
                 const OverlayAssetUiState& asset, OverlayCanvasFormat format)
{
    if (format != snapshot.format || !state.programMode ||
        *state.programMode != ProgramMode::Effects)
    {
        return false;
    }

    const bool displayLive = state.outputActive;
    const bool webcamLive = state.webcamStats.state == VirtualCameraState::Sending;
    if (!displayLive && !webcamLive) return false;

    if (!snapshot.layers) return false;
    for (const OverlayLayerUiState& layer : *snapshot.layers)
    {
        if (layer.assetId == asset.id && layer.enabled && layer.visible) return true;
    }
    return false;
}

void setTooltipWhenDisabled(const char* reason)
{
    if (reason && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", reason);
    }
}

void drawImportPopup(UiFrameState& state, const char* popupId,
                     const OverlayPanelSnapshot& snapshot, bool operationLocked)
{
    if (!ImGui::BeginPopup(popupId)) return;

    const bool disabled = operationLocked || importBusy(snapshot);
    ImGui::BeginDisabled(disabled);
    if (ImGui::MenuItem("Static PNG...") )
    {
        if (OverlayUiCommand* request = command(state, OverlayUiCommandType::BeginImport))
        {
            request->mediaType = OverlayMediaType::StillPng;
        }
    }
    if (ImGui::MenuItem("PNG sequence..."))
    {
        if (OverlayUiCommand* request = command(state, OverlayUiCommandType::BeginImport))
        {
            request->mediaType = OverlayMediaType::PngSequence;
        }
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
}

void drawImportProgress(UiFrameState& state, const OverlayPanelSnapshot& snapshot)
{
    const OverlayImportUiState& job = snapshot.import;
    if (job.phase == OverlayImportPhase::Idle) return;

    if (job.phase == OverlayImportPhase::Failed)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::splitMagenta);
        ImGui::TextWrapped("%s", job.status.empty() ? "Import failed" : job.status.c_str());
        ImGui::PopStyleColor();
        if (ImGui::SmallButton("Dismiss##overlay_import_error"))
        {
            command(state, OverlayUiCommandType::DismissStatus);
        }
        return;
    }

    const float cancelWidth = job.cancellable
        ? ImGui::CalcTextSize("Cancel").x + ImGui::GetStyle().FramePadding.x * 2.0f
        : 0.0f;
    const float progressWidth = std::max(theme::scaled(48.0f),
        ImGui::GetContentRegionAvail().x -
            (job.cancellable ? cancelWidth + ImGui::GetStyle().ItemSpacing.x : 0.0f));

    char label[128];
    if (!job.status.empty())
    {
        std::snprintf(label, sizeof(label), "%s", job.status.c_str());
    }
    else
    {
        std::snprintf(label, sizeof(label), "%s", importPhaseLabel(job.phase));
    }
    ImGui::ProgressBar(std::clamp(job.progress, 0.0f, 1.0f),
                       ImVec2(progressWidth, ImGui::GetFrameHeight()), label);
    if (job.cancellable)
    {
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(cancelWidth, 0.0f)))
        {
            command(state, OverlayUiCommandType::CancelImport);
        }
    }
}

void drawStackStatus(UiFrameState& state, const OverlayPanelSnapshot& snapshot)
{
    if (snapshot.import.phase != OverlayImportPhase::Idle)
    {
        drawImportProgress(state, snapshot);
        return;
    }

    std::size_t missing = 0;
    if (snapshot.layers)
    {
        for (const OverlayLayerUiState& layer : *snapshot.layers)
        {
            if (layer.enabled && !layer.variantAvailable) ++missing;
        }
    }

    if (!snapshot.status.empty())
    {
        ImGui::TextWrapped("%s", snapshot.status.c_str());
        if (ImGui::SmallButton("Dismiss##overlay_status"))
        {
            command(state, OverlayUiCommandType::DismissStatus);
        }
    }
    else if (missing > 0)
    {
        ImGui::TextColored(theme::splitMagenta, "%zu hidden - missing %s", missing,
                           formatLabel(snapshot.format));
    }
    else if (state.programMode && *state.programMode == ProgramMode::Clean)
    {
        ImGui::TextDisabled("Hidden by Clean");
    }
    else if (state.programMode &&
             (*state.programMode == ProgramMode::Freeze || *state.programMode == ProgramMode::Black))
    {
        ImGui::TextDisabled("PROGRAM held - stack continues on FX");
    }
    else
    {
        ImGui::TextDisabled("Top row is front");
    }
}

void drawLayerRow(UiFrameState& state, const OverlayPanelSnapshot& snapshot,
                  const OverlayLayerUiState& layer, std::size_t index, bool operationLocked)
{
    ImGui::PushID(reinterpret_cast<const void*>(static_cast<uintptr_t>(layer.id)));

    bool enabled = layer.enabled;
    if (ImGui::Checkbox("##enabled", &enabled))
    {
        if (OverlayUiCommand* request = command(state, OverlayUiCommandType::SetLayerEnabled))
        {
            request->layerId = layer.id;
            request->enabled = enabled;
        }
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s this layer with a 0.35 s fade.", enabled ? "Hide" : "Show");
    }

    ImGui::SameLine();
    const bool selected = ui::inspectorKind() == ui::InspectorKind::OverlayLayer &&
                          ui::inspectedOverlayLayerId() == layer.id;
    const float glyph = ImGui::GetFrameHeight();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float deleteGap = theme::scaled(14.0f);
    const float controls = glyph * 3.0f + style.ItemSpacing.x * 3.0f + deleteGap;
    const float rowWidth = std::max(theme::scaled(40.0f),
                                    ImGui::GetContentRegionAvail().x - controls);

    char label[320];
    std::snprintf(label, sizeof(label), "%s   %s%s%s###layer", layer.name.c_str(),
                  mediaLabel(layer.mediaType), layer.variantAvailable ? "" : "   MISSING",
                  layer.status.empty() ? "" : "   ERROR");
    if (!layer.variantAvailable || !layer.status.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::splitMagenta);
    }
    else if (!layer.enabled)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::rackGrey);
    }
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
    if (ImGui::Selectable(label, selected, 0, ImVec2(rowWidth, glyph)))
    {
        if (selected) ui::closeInspector();
        else ui::inspectOverlayLayer(layer.id);
    }
    ImGui::PopStyleVar();
    if (!layer.variantAvailable || !layer.status.empty() || !layer.enabled)
        ImGui::PopStyleColor();

    if (selected)
    {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(
            min, ImVec2(min.x + theme::scaled(2.0f), max.y), theme::kSplitCyanU32);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s\n%s - opacity %.0f%%%s\n%s", layer.name.c_str(),
                          mediaName(layer.mediaType), static_cast<double>(layer.opacity) * 100.0,
                          layer.variantAvailable ? "" : " - current variant missing",
                          selected ? "Click to close its controls"
                                   : "Click to edit below the preview");
    }

    const std::size_t count = snapshot.layers ? snapshot.layers->size() : 0;
    ImGui::SameLine();
    ImGui::BeginDisabled(operationLocked || index == 0);
    if (theme::glyphButton("##up", theme::Glyph::Up, glyph, "Move toward front"))
    {
        if (OverlayUiCommand* request = command(state, OverlayUiCommandType::MoveLayer))
        {
            request->layerId = layer.id;
            request->moveDelta = -1;
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(operationLocked || index + 1 >= count);
    if (theme::glyphButton("##down", theme::Glyph::Down, glyph, "Move toward back"))
    {
        if (OverlayUiCommand* request = command(state, OverlayUiCommandType::MoveLayer))
        {
            request->layerId = layer.id;
            request->moveDelta = 1;
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine(0.0f, style.ItemSpacing.x + deleteGap);
    ImGui::BeginDisabled(operationLocked);
    if (theme::glyphButton("##remove", theme::Glyph::Close, glyph,
                           "Remove with a 0.35 s fade", theme::ButtonAccent::Magenta))
    {
        if (OverlayUiCommand* request = command(state, OverlayUiCommandType::RemoveLayer))
        {
            request->layerId = layer.id;
        }
    }
    ImGui::EndDisabled();
    setTooltipWhenDisabled(operationLocked ? "Unlock Operation to change overlay structure." : nullptr);

    ImGui::PopID();
}

void drawEmptyLayerRow(std::size_t index)
{
    ImGui::PushID(static_cast<int>(index));
    ImGui::BeginDisabled();
    bool off = false;
    ImGui::Checkbox("##empty_enabled", &off);
    ImGui::SameLine();
    ImGui::Selectable("Empty", false, 0,
                      ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()));
    ImGui::EndDisabled();
    ImGui::PopID();
}

void drawSidebarImportActions(UiFrameState& state, const OverlayPanelSnapshot& snapshot,
                              bool operationLocked)
{
    const float width = theme::rowButtonWidth(2);
    if (theme::actionButton("Library", theme::ButtonAccent::Cyan, ImVec2(width, 0.0f),
                            ui::inspectorKind() == ui::InspectorKind::OverlayLibrary))
    {
        ui::inspectOverlayLibrary();
    }
    ImGui::SameLine();

    const bool busy = importBusy(snapshot);
    ImGui::BeginDisabled(operationLocked || busy);
    if (theme::actionButton("Import", theme::ButtonAccent::Cyan, ImVec2(width, 0.0f)))
    {
        ImGui::OpenPopup("overlay_sidebar_import");
    }
    ImGui::EndDisabled();
    setTooltipWhenDisabled(operationLocked
        ? "Unlock Operation to change the overlay library."
        : (busy ? "Finish or cancel the current import first." : nullptr));
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Import into the managed library. It will not be put on air.");
    }
    drawImportPopup(state, "overlay_sidebar_import", snapshot, operationLocked);
}

void drawInspectorTitle(const char* scope, const char* name, bool showEnabled,
                        bool enabled, UiFrameState& state, uint64_t layerId)
{
    const float glyph = ImGui::GetFrameHeight();
    const float startX = ImGui::GetCursorPosX();
    const float width = ImGui::GetContentRegionAvail().x;

    ui::pushMono();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::rackGrey, "%s", scope);
    if (name && name[0] != '\0')
    {
        ImGui::SameLine(0.0f, theme::scaled(10.0f));
        ImGui::TextColored(theme::splitCyan, "%s", name);
    }
    ui::popMono();

    if (showEnabled)
    {
        ImGui::SameLine(0.0f, theme::scaled(18.0f));
        bool value = enabled;
        if (ImGui::Checkbox("Enabled", &value))
        {
            if (OverlayUiCommand* request = command(state, OverlayUiCommandType::SetLayerEnabled))
            {
                request->layerId = layerId;
                request->enabled = value;
            }
        }
    }

    ImGui::SameLine();
    ImGui::SetCursorPosX(startX + width - glyph);
    if (theme::glyphButton("##close_overlay_inspector", theme::Glyph::Close, glyph,
                           "Close and show the stats strip"))
    {
        ui::closeInspector();
    }
    ImGui::Separator();
}

void drawScalarTrack(const char* label, const char* id, float shown, float minimum,
                     float maximum, float defaultValue, const char* format,
                     float speed, float& changed)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const float valueWidth = ImGui::CalcTextSize("0000.00").x + style.FramePadding.x * 2.0f;
    bool hovered = false;

    ImGui::PushID(id);
    if (ImGui::BeginTable("##header", 2, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, valueWidth);
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableNextColumn();
        ui::pushMono();
        ImGui::SetNextItemWidth(valueWidth);
        float value = shown;
        if (ImGui::DragFloat("##number", &value, speed, minimum, maximum, format,
                             ImGuiSliderFlags_AlwaysClamp))
        {
            changed = value;
        }
        hovered = ImGui::IsItemHovered();
        ui::popMono();
        ImGui::EndTable();
    }

    ImGui::SetNextItemWidth(-FLT_MIN);
    float track = shown;
    if (ImGui::SliderFloat("##track", &track, minimum, maximum, "",
                           ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput))
    {
        changed = track;
    }
    hovered = hovered || ImGui::IsItemHovered();

    const float span = maximum - minimum;
    if (std::abs(span) > 1.0e-6f)
    {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const float grab = ImGui::GetStyle().GrabMinSize * 0.5f + theme::scaled(2.0f);
        const float x0 = min.x + grab;
        const float x1 = max.x - grab;
        const float x = x0 + (x1 - x0) * std::clamp((defaultValue - minimum) / span, 0.0f, 1.0f);
        ImGui::GetWindowDrawList()->AddLine(ImVec2(x, min.y + theme::scaled(3.0f)),
                                            ImVec2(x, max.y - theme::scaled(3.0f)),
                                            IM_COL32(107, 114, 128, 130), theme::scaled(1.0f));
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) changed = defaultValue;
    if (hovered)
    {
        ImGui::SetTooltip("%s\nDrag the number for fine steps; Ctrl-click to type.\n"
                          "Right-click to reset.", label);
    }
    ImGui::PopID();
}

void drawThumbnail(const OverlayVariantUiState& variant, OverlayCanvasFormat format)
{
    if (!variant.thumbnail || !variant.thumbnail->valid() || !variant.thumbnail->nativeTexture())
    {
        ImGui::TextDisabled("First frame preview unavailable");
        return;
    }

    const float height = theme::scaled(86.0f);
    const float aspect = format == OverlayCanvasFormat::Portrait9x16 ? 9.0f / 16.0f : 16.0f / 9.0f;
    ImGui::Image(reinterpret_cast<ImTextureID>(variant.thumbnail->nativeTexture()),
                 ImVec2(height * aspect, height));
}

void drawSequenceControls(UiFrameState& state, const OverlayLayerUiState& layer)
{
    theme::drawGroupLabel("PLAYBACK");

    int playback = layer.playback == OverlayPanelPlayback::OneShot ? 1 : 0;
    ImGui::SetNextItemWidth(theme::scaled(180.0f));
    if (ImGui::Combo("Mode", &playback, "Loop\0One-shot\0"))
    {
        if (OverlayUiCommand* request = command(state, OverlayUiCommandType::SetLayerPlayback))
        {
            request->layerId = layer.id;
            request->playback = playback == 1 ? OverlayPanelPlayback::OneShot
                                              : OverlayPanelPlayback::Loop;
        }
    }

    float changed = layer.fps;
    drawScalarTrack("FPS", "sequence_fps", layer.fps, 1.0f, 30.0f, 30.0f, "%.1f", 0.1f,
                    changed);
    if (std::abs(changed - layer.fps) > 1.0e-4f)
    {
        if (OverlayUiCommand* request = command(state, OverlayUiCommandType::SetLayerFps))
        {
            request->layerId = layer.id;
            request->fps = changed;
        }
    }

    const float buttonWidth = theme::rowButtonWidth(2);
    if (theme::actionButton(layer.paused ? "Play" : "Pause", theme::ButtonAccent::Cyan,
                            ImVec2(buttonWidth, 0.0f), layer.paused))
    {
        if (OverlayUiCommand* request = command(state, OverlayUiCommandType::SetLayerPaused))
        {
            request->layerId = layer.id;
            request->paused = !layer.paused;
        }
    }
    ImGui::SameLine();
    if (theme::actionButton("Restart", theme::ButtonAccent::Neutral,
                            ImVec2(buttonWidth, 0.0f)))
    {
        if (OverlayUiCommand* request = command(state, OverlayUiCommandType::RestartLayer))
        {
            request->layerId = layer.id;
        }
    }

    ImGui::TextDisabled("Frame %u / %u", layer.frameCount > 0 ? layer.displayedFrame + 1 : 0,
                        layer.frameCount);
    if (layer.underflows > 0)
    {
        ImGui::SameLine(0.0f, theme::scaled(14.0f));
        ImGui::TextColored(theme::splitMagenta, "underflows %llu",
                           static_cast<unsigned long long>(layer.underflows));
    }
}

void drawLayerInspector(UiFrameState& state, const OverlayPanelSnapshot& snapshot,
                        const OverlayLayerUiState& layer)
{
    drawInspectorTitle("OVERLAY", layer.name.c_str(), true, layer.enabled, state, layer.id);

    const bool operationLocked = state.operationLocked && *state.operationLocked;
    const OverlayAssetUiState* asset = findAsset(snapshot, layer.assetId);
    const OverlayVariantUiState* variant = asset ? &variantFor(*asset, snapshot.format) : nullptr;

    if (!layer.variantAvailable)
    {
        ImGui::TextColored(theme::splitMagenta, "Missing %s variant - this layer is transparent",
                           formatLabel(snapshot.format));
    }
    if (!layer.status.empty())
    {
        ImGui::TextColored(theme::splitMagenta, "%s", layer.status.c_str());
    }
    if (layer.variantAvailable && layer.status.empty() && state.programMode &&
        *state.programMode == ProgramMode::Clean)
    {
        ImGui::TextDisabled("Hidden by Clean");
    }
    else if (layer.variantAvailable && layer.status.empty() && state.programMode &&
             (*state.programMode == ProgramMode::Freeze || *state.programMode == ProgramMode::Black))
    {
        ImGui::TextDisabled("PROGRAM held - controls continue on the FX preview");
    }

    const float available = ImGui::GetContentRegionAvail().x;
    const bool columns = available >= theme::scaled(760.0f);
    if (columns && ImGui::BeginTable("##overlay_layer_columns", 3,
                                     ImGuiTableFlags_SizingStretchSame |
                                         ImGuiTableFlags_BordersInnerV))
    {
        ImGui::TableNextColumn();
    }

    theme::drawGroupLabel("COMPOSITE");
    float opacity = layer.opacity * 100.0f;
    float changed = opacity;
    drawScalarTrack("Opacity", "opacity", opacity, 0.0f, 100.0f, 100.0f, "%.0f%%", 0.25f,
                    changed);
    if (std::abs(changed - opacity) > 1.0e-4f)
    {
        if (OverlayUiCommand* request = command(state, OverlayUiCommandType::SetLayerOpacity))
        {
            request->layerId = layer.id;
            request->opacity = changed / 100.0f;
        }
    }

    std::size_t position = 0;
    if (snapshot.layers)
    {
        for (std::size_t i = 0; i < snapshot.layers->size(); ++i)
        {
            if ((*snapshot.layers)[i].id == layer.id) { position = i; break; }
        }
    }
    ImGui::TextDisabled("%zu of %zu%s", position + 1,
                        snapshot.layers ? snapshot.layers->size() : 0,
                        position == 0 ? " - Front" : "");
    const float moveWidth = theme::rowButtonWidth(2);
    ImGui::BeginDisabled(operationLocked || position == 0);
    if (theme::actionButton("Move forward", theme::ButtonAccent::Neutral,
                            ImVec2(moveWidth, 0.0f)))
    {
        if (OverlayUiCommand* request = command(state, OverlayUiCommandType::MoveLayer))
        {
            request->layerId = layer.id;
            request->moveDelta = -1;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    const std::size_t layerCount = snapshot.layers ? snapshot.layers->size() : 0;
    ImGui::BeginDisabled(operationLocked || position + 1 >= layerCount);
    if (theme::actionButton("Move back", theme::ButtonAccent::Neutral,
                            ImVec2(moveWidth, 0.0f)))
    {
        if (OverlayUiCommand* request = command(state, OverlayUiCommandType::MoveLayer))
        {
            request->layerId = layer.id;
            request->moveDelta = 1;
        }
    }
    ImGui::EndDisabled();

    if (columns) ImGui::TableNextColumn();
    else ImGui::Separator();

    theme::drawGroupLabel("CURRENT FORMAT", formatLabel(snapshot.format),
                          layer.variantAvailable ? theme::kSplitCyanU32 : theme::kSplitMagentaU32);
    if (variant && variant->present)
    {
        drawThumbnail(*variant, snapshot.format);
        ImGui::TextDisabled("%ux%u", variant->width, variant->height);
    }
    else
    {
        ImGui::TextColored(theme::splitMagenta, "Missing - no fit, crop or stretch");
    }

    if (columns) ImGui::TableNextColumn();
    else ImGui::Separator();

    theme::drawGroupLabel("ASSET", mediaLabel(layer.mediaType));
    ImGui::TextWrapped("%s", mediaName(layer.mediaType));
    if (asset)
    {
        ImGui::TextDisabled("16:9 %s   9:16 %s", asset->landscape.present ? "ready" : "missing",
                            asset->portrait.present ? "ready" : "missing");
    }
    if (theme::actionButton("Show in Library", theme::ButtonAccent::Cyan,
                            ImVec2(-1.0f, 0.0f)))
    {
        ui::inspectOverlayLibrary(layer.assetId);
    }

    if (columns) ImGui::EndTable();

    if (layer.mediaType == OverlayMediaType::PngSequence)
    {
        ImGui::Separator();
        drawSequenceControls(state, layer);
    }
}

void issueVariantCommand(UiFrameState& state, OverlayUiCommandType type,
                         const OverlayAssetUiState& asset, OverlayCanvasFormat format)
{
    if (OverlayUiCommand* request = command(state, type))
    {
        request->assetId = asset.id;
        request->mediaType = asset.mediaType;
        request->format = format;
    }
}

void drawLiveReplaceConfirmation(UiFrameState& state)
{
    if (!g_pendingLiveReplace.active) return;
    if (ImGui::BeginPopupModal("Replace live variant?###overlay_replace_live", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped("This variant is on PROGRAM. The current image stays live until the "
                           "replacement is ready, then fades to it over 0.35 s.");
        if (theme::actionButton("Choose replacement", theme::ButtonAccent::Magenta))
        {
            if (OverlayUiCommand* request = command(state, OverlayUiCommandType::ReplaceVariant))
            {
                request->assetId = g_pendingLiveReplace.assetId;
                request->mediaType = g_pendingLiveReplace.mediaType;
                request->format = g_pendingLiveReplace.format;
            }
            g_pendingLiveReplace = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            g_pendingLiveReplace = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void requestVariantChange(UiFrameState& state, const OverlayPanelSnapshot& snapshot,
                          const OverlayAssetUiState& asset, OverlayCanvasFormat format,
                          bool replacing)
{
    if (replacing && assetIsLive(state, snapshot, asset, format))
    {
        g_pendingLiveReplace.active = true;
        g_pendingLiveReplace.assetId = asset.id;
        g_pendingLiveReplace.mediaType = asset.mediaType;
        g_pendingLiveReplace.format = format;
        ImGui::OpenPopup("Replace live variant?###overlay_replace_live");
        return;
    }
    issueVariantCommand(state, replacing ? OverlayUiCommandType::ReplaceVariant
                                         : OverlayUiCommandType::AddVariant,
                        asset, format);
}

void drawVariantCard(UiFrameState& state, const OverlayPanelSnapshot& snapshot,
                     const OverlayAssetUiState& asset, OverlayCanvasFormat format,
                     bool operationLocked)
{
    const OverlayVariantUiState& variant = variantFor(asset, format);
    ImGui::PushID(format == OverlayCanvasFormat::Portrait9x16 ? "portrait" : "landscape");
    theme::drawGroupLabel(formatLabel(format), variant.present ? "READY" : "MISSING",
                          variant.present ? theme::kSplitCyanU32 : theme::kSplitMagentaU32);
    if (variant.present)
    {
        drawThumbnail(variant, format);
        if (asset.mediaType == OverlayMediaType::PngSequence)
        {
            ImGui::TextDisabled("%ux%u   %u frames   %.0f fps", variant.width, variant.height,
                                variant.frameCount, variant.fps);
        }
        else
        {
            ImGui::TextDisabled("%ux%u", variant.width, variant.height);
        }
        if (!variant.status.empty()) ImGui::TextWrapped("%s", variant.status.c_str());
    }
    else
    {
        ImGui::Dummy(ImVec2(0.0f, theme::scaled(28.0f)));
        ImGui::TextDisabled("Exact %s asset required", formatLabel(format));
    }

    const bool busy = importBusy(snapshot);
    ImGui::BeginDisabled(operationLocked || busy);
    const char* label = variant.present ? "Replace..." : "Add variant...";
    if (theme::actionButton(label, theme::ButtonAccent::Cyan, ImVec2(-1.0f, 0.0f)))
    {
        requestVariantChange(state, snapshot, asset, format, variant.present);
    }
    ImGui::EndDisabled();
    setTooltipWhenDisabled(operationLocked
        ? "Unlock Operation to change the overlay library."
        : (busy ? "Finish or cancel the current import first." : nullptr));
    ImGui::PopID();
}

void drawRemoveConfirmation(UiFrameState& state)
{
    if (g_pendingRemoveAsset.empty()) return;
    if (ImGui::BeginPopupModal("Remove overlay asset?###overlay_remove_asset", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped("Remove this managed asset and both of its variants?\n"
                           "This cannot be undone from CamVJ.");
        if (theme::actionButton("Remove", theme::ButtonAccent::Magenta))
        {
            if (OverlayUiCommand* request = command(state, OverlayUiCommandType::RemoveAsset))
            {
                request->assetId = g_pendingRemoveAsset;
            }
            g_pendingRemoveAsset.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            g_pendingRemoveAsset.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void drawLibraryList(const OverlayPanelSnapshot& snapshot, float height)
{
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##overlay_filter", "Filter library", g_libraryFilter,
                             sizeof(g_libraryFilter));

    if (!ImGui::BeginChild("##overlay_asset_list", ImVec2(0.0f, height),
                           ImGuiChildFlags_None))
    {
        ImGui::EndChild();
        return;
    }

    const std::string_view selected = ui::inspectedOverlayAssetId();
    if (!snapshot.assets || snapshot.assets->empty())
    {
        ImGui::TextDisabled("Library is empty");
    }
    else
    {
        for (const OverlayAssetUiState& asset : *snapshot.assets)
        {
            if (!containsCaseInsensitive(asset.name, g_libraryFilter)) continue;

            ImGui::PushID(asset.id.c_str());
            char label[384];
            std::snprintf(label, sizeof(label), "%s   %s   %s%s%s###asset", asset.name.c_str(),
                          mediaLabel(asset.mediaType), asset.landscape.present ? "16:9" : "",
                          asset.landscape.present && asset.portrait.present ? "+" : "",
                          asset.portrait.present ? "9:16" : "");
            const bool isSelected = selected == asset.id;
            if (ImGui::Selectable(label, isSelected))
            {
                ui::selectOverlayLibraryAsset(asset.id);
            }
            if (asset.inStack)
            {
                ImGui::SameLine();
                ImGui::TextColored(theme::splitCyan, "IN STACK");
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", asset.name.c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

void drawLibraryDetail(UiFrameState& state, const OverlayPanelSnapshot& snapshot,
                       const OverlayAssetUiState* asset, bool operationLocked)
{
    if (!asset)
    {
        theme::drawGroupLabel("LIBRARY");
        ImGui::TextWrapped("Import a static PNG or PNG-sequence folder, then select it here. "
                           "Importing never adds an overlay to the active stack.");
        return;
    }

    theme::drawGroupLabel("ASSET", mediaLabel(asset->mediaType));
    ImGui::TextWrapped("%s", asset->name.c_str());
    if (!asset->status.empty())
    {
        ImGui::TextColored(theme::splitMagenta, "%s", asset->status.c_str());
    }

    const std::size_t layerCount = snapshot.layers ? snapshot.layers->size() : 0;
    const bool duplicate = stackContains(snapshot, asset->id);
    const char* addBlocked = nullptr;
    if (operationLocked) addBlocked = "Unlock Operation to change overlay structure.";
    else if (layerCount >= kMaxLayers) addBlocked = "The active stack already has four layers.";
    else if (duplicate) addBlocked = "This asset is already in the active stack.";

    ImGui::BeginDisabled(addBlocked != nullptr);
    if (theme::actionButton("Add layer", theme::ButtonAccent::Cyan, ImVec2(-1.0f, 0.0f)))
    {
        if (OverlayUiCommand* request = command(state, OverlayUiCommandType::AddLayer))
        {
            request->assetId = asset->id;
        }
    }
    ImGui::EndDisabled();
    setTooltipWhenDisabled(addBlocked);
    if (!addBlocked && ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Add at the front, enabled at 100%% opacity, with a 0.35 s fade.");
    }

    const float cardWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::BeginChild("##landscape_variant", ImVec2(cardWidth, theme::scaled(190.0f)),
                          ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysUseWindowPadding))
    {
        drawVariantCard(state, snapshot, *asset, OverlayCanvasFormat::Landscape16x9,
                        operationLocked);
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("##portrait_variant", ImVec2(cardWidth, theme::scaled(190.0f)),
                          ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysUseWindowPadding))
    {
        drawVariantCard(state, snapshot, *asset, OverlayCanvasFormat::Portrait9x16,
                        operationLocked);
    }
    ImGui::EndChild();

    const bool referenced = asset->inStack || asset->presetReferences > 0;
    ImGui::BeginDisabled(operationLocked || referenced || importBusy(snapshot));
    if (theme::actionButton("Remove from library", theme::ButtonAccent::Magenta,
                            ImVec2(-1.0f, 0.0f)))
    {
        g_pendingRemoveAsset = asset->id;
        ImGui::OpenPopup("Remove overlay asset?###overlay_remove_asset");
    }
    ImGui::EndDisabled();
    if (referenced)
    {
        setTooltipWhenDisabled(asset->inStack
            ? "Remove this asset from the active stack first."
            : "This asset is referenced by saved presets.");
        ImGui::TextDisabled("References: %s%s%u preset%s",
                            asset->inStack ? "active stack, " : "",
                            asset->presetReferences == 0 ? "" : "",
                            asset->presetReferences,
                            asset->presetReferences == 1 ? "" : "s");
    }

    drawLiveReplaceConfirmation(state);
    drawRemoveConfirmation(state);
}

void drawLibraryInspector(UiFrameState& state, const OverlayPanelSnapshot& snapshot)
{
    drawInspectorTitle("OVERLAY LIBRARY", nullptr, false, false, state, 0);

    const bool operationLocked = state.operationLocked && *state.operationLocked;
    const bool busy = importBusy(snapshot);
    ImGui::BeginDisabled(operationLocked || busy);
    if (theme::actionButton("Import", theme::ButtonAccent::Cyan,
                            ImVec2(theme::scaled(150.0f), 0.0f)))
    {
        ImGui::OpenPopup("overlay_library_import");
    }
    ImGui::EndDisabled();
    setTooltipWhenDisabled(operationLocked
        ? "Unlock Operation to change the overlay library."
        : (busy ? "Finish or cancel the current import first." : nullptr));
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Import into the library only; it will not be put on air.");
    }
    drawImportPopup(state, "overlay_library_import", snapshot, operationLocked);

    if (snapshot.import.phase != OverlayImportPhase::Idle)
    {
        ImGui::SameLine();
        drawImportProgress(state, snapshot);
    }

    const OverlayAssetUiState* selected =
        findAsset(snapshot, ui::inspectedOverlayAssetId());
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float availableHeight = ImGui::GetContentRegionAvail().y;
    if (availableWidth >= theme::scaled(720.0f))
    {
        if (ImGui::BeginTable("##overlay_library_columns", 2,
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV))
        {
            ImGui::TableSetupColumn("Assets", ImGuiTableColumnFlags_WidthFixed,
                                    theme::scaled(300.0f));
            ImGui::TableSetupColumn("Detail", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextColumn();
            drawLibraryList(snapshot, std::max(0.0f, availableHeight - ImGui::GetFrameHeightWithSpacing()));
            ImGui::TableNextColumn();
            if (ImGui::BeginChild("##overlay_asset_detail", ImVec2(0.0f, availableHeight),
                                  ImGuiChildFlags_None))
            {
                drawLibraryDetail(state, snapshot, selected, operationLocked);
            }
            ImGui::EndChild();
            ImGui::EndTable();
        }
    }
    else
    {
        drawLibraryList(snapshot, theme::scaled(96.0f));
        ImGui::Separator();
        drawLibraryDetail(state, snapshot, selected, operationLocked);
    }
}

} // namespace

float overlaysPanelHeight()
{
    if (!theme::panelOpen(theme::PanelSection::Overlays))
    {
        return theme::foldedPanelHeight();
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    return theme::foldedPanelHeight() + ImGui::GetTextLineHeightWithSpacing() +
           ImGui::GetFrameHeightWithSpacing() * static_cast<float>(kMaxLayers) +
           theme::actionHeight() + style.ItemSpacing.y + ImGui::GetFrameHeightWithSpacing() +
           theme::scaled(10.0f);
}

void drawOverlaysPanel(UiFrameState& state)
{
    const OverlayPanelSnapshot* snapshot = state.overlays;
    const std::size_t layerCount = snapshot && snapshot->layers ? snapshot->layers->size() : 0;

    char meta[32];
    if (snapshot && importBusy(*snapshot))
    {
        std::snprintf(meta, sizeof(meta), "IMPORT %.0f%%",
                      static_cast<double>(std::clamp(snapshot->import.progress, 0.0f, 1.0f)) * 100.0);
    }
    else
    {
        std::snprintf(meta, sizeof(meta), "%zu/%zu  %s", layerCount, kMaxLayers,
                      snapshot ? formatLabel(snapshot->format) : "--");
    }

    const bool open = theme::drawPanelHeader("OVERLAYS", theme::kSplitCyanU32,
                                              theme::PanelSection::Overlays, meta);
    if (!open) return;

    if (!snapshot)
    {
        ImGui::TextDisabled("Overlay system unavailable");
        return;
    }

    if (snapshot->importSerial != 0 && snapshot->importSerial != g_seenImportSerial)
    {
        g_seenImportSerial = snapshot->importSerial;
        ui::inspectOverlayLibrary(snapshot->lastImportedAssetId);
    }

    const bool operationLocked = state.operationLocked && *state.operationLocked;
    theme::drawGroupLabel("ACTIVE", formatLabel(snapshot->format));
    for (std::size_t i = 0; i < kMaxLayers; ++i)
    {
        if (snapshot->layers && i < snapshot->layers->size())
        {
            drawLayerRow(state, *snapshot, (*snapshot->layers)[i], i, operationLocked);
        }
        else
        {
            drawEmptyLayerRow(i);
        }
    }

    ImGui::Spacing();
    drawSidebarImportActions(state, *snapshot, operationLocked);
    drawStackStatus(state, *snapshot);
}

void drawOverlayInspectorPanel(UiFrameState& state)
{
    if (!state.overlays)
    {
        ui::closeInspector();
        drawStatsPanel(state);
        return;
    }

    if (ui::inspectorKind() == ui::InspectorKind::OverlayLibrary)
    {
        drawLibraryInspector(state, *state.overlays);
        return;
    }

    const OverlayLayerUiState* layer =
        findLayer(*state.overlays, ui::inspectedOverlayLayerId());
    if (!layer)
    {
        ui::closeInspector();
        drawStatsPanel(state);
        return;
    }
    drawLayerInspector(state, *state.overlays, *layer);
}

} // namespace atemfx
