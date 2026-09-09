#include "ui/Panels.h"

#include <algorithm>
#include <cstdio>
#include <numeric>
#include <string>
#include <vector>

#include "effects/Effect.h"
#include "imgui.h"
#include "tracking/source_mapping.h"
#include "ui/Theme.h"
#include "video/VideoSource.h"

namespace atemfx {

namespace {

void drawPickControls(UiFrameState& state)
{
    if (!state.trackingAvailable || !state.pickSubjectMode)
    {
        return;
    }

    const bool locked =
        state.effectContext && state.effectContext->tracking.locked;
    const bool picking = *state.pickSubjectMode;
    const bool choosing = picking || !locked;

    if (locked)
    {
        if (picking)
        {
            if (theme::actionButton("Cancel", theme::ButtonAccent::Neutral))
            {
                *state.pickSubjectMode = false;
            }
            ImGui::SameLine();
            // Centre the hint on the taller action row rather than on a text
            // frame it no longer matches.
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                                 (theme::actionHeight() - ImGui::GetTextLineHeight()) * 0.5f);
            ImGui::TextDisabled("Click SOURCE to switch target");
        }
        // Choosing who PROGRAM follows is the panel's real action, so it gets
        // the width and the source accent rather than a default grey button
        // the same size as a rescan.
        else if (theme::actionButton("Pick subject", theme::ButtonAccent::Cyan,
                                     ImVec2(-1.0f, 0.0f)))
        {
            *state.pickSubjectMode = true;
        }
    }
    else
    {
        ImGui::TextDisabled("Click a box on SOURCE, or a name below");
    }

    if (!choosing || !state.trackingCandidates)
    {
        return;
    }

    const std::vector<TrackingCandidate>& candidates = *state.trackingCandidates;
    if (candidates.empty())
    {
        // The atlas is built with ImGui's default Latin-1 range, so an em dash
        // here reaches the operator as a literal "?".
        ImGui::TextDisabled("No people in shot. Click or drag on SOURCE");
        return;
    }

    std::vector<int> order(candidates.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return candidates[static_cast<std::size_t>(a)].centerX <
               candidates[static_cast<std::size_t>(b)].centerX;
    });

    for (int i = 0; i < static_cast<int>(order.size()); ++i)
    {
        const std::size_t            index     = static_cast<std::size_t>(order[static_cast<std::size_t>(i)]);
        const TrackingCandidate&     candidate = candidates[index];
        char                         label[32];
        std::snprintf(label, sizeof(label), "Person %d", i + 1);

        ImGui::PushID(static_cast<int>(index));
        if (ImGui::Selectable(label) && state.requestedLock)
        {
            fillLockRequest(*state.requestedLock, candidate.centerX, candidate.centerY,
                            candidate.width, candidate.height);
            *state.pickSubjectMode = false;
        }
        ImGui::PopID();
    }
}

} // namespace

void drawSourcePanel(UiFrameState& state)
{
    char meta[32];
    std::snprintf(meta, sizeof(meta), "%ux%u", state.processingWidth, state.processingHeight);
    if (!theme::drawPanelHeader("SOURCE", theme::kSplitCyanU32, theme::PanelSection::Source, meta))
    {
        return;
    }

    const bool recovering = state.sourceDisconnected ||
                            state.sourceHealth.signal == SourceSignal::Waiting ||
                            state.sourceHealth.signal == SourceSignal::Stale;
    const bool sourceLocked = state.operationLocked && *state.operationLocked && !recovering;

    const ImGuiStyle& style       = ImGui::GetStyle();
    const float       rescanWidth = ImGui::CalcTextSize("Rescan").x + style.FramePadding.x * 2.0f;
    const float       rowStartX   = ImGui::GetCursorPosX();
    const float       rowWidth    = ImGui::GetContentRegionAvail().x;

    ImGui::BeginDisabled(sourceLocked);
    if (state.availableSources && !state.availableSources->empty())
    {
        const std::vector<VideoSourceDescriptor>& sources = *state.availableSources;

        const int  selected = state.selectedSource;
        const bool valid    = selected >= 0 && selected < static_cast<int>(sources.size());
        const char* preview = valid ? sources[selected].displayName.c_str() : "Select input";

        ImGui::SetNextItemWidth(rowWidth - rescanWidth - style.ItemSpacing.x);
        if (ImGui::BeginCombo("##input", preview))
        {
            for (int i = 0; i < static_cast<int>(sources.size()); ++i)
            {
                const VideoSourceDescriptor& source = sources[i];

                ImGui::PushID(i);
                if (ImGui::Selectable(source.displayName.c_str(), i == selected) &&
                    state.requestedSource)
                {
                    *state.requestedSource = i;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%s", source.category.c_str());
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    }
    else
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("No inputs");
    }

    // The rescan sits on the row it repairs. An empty input list is when the
    // operator reaches for it, and that is the one case where it used to
    // disappear along with the combo.
    ImGui::SameLine();
    ImGui::SetCursorPosX(rowStartX + rowWidth - rescanWidth);
    if (ImGui::Button("Rescan", ImVec2(rescanWidth, 0.0f)) && state.requestDeviceRescan)
    {
        *state.requestDeviceRescan = true;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("Look for capture devices again.");
    }

    if (state.source)
    {
        const std::string status = state.source->status();
        if (!status.empty())
        {
            ImGui::TextWrapped("%s", status.c_str());
        }
    }

    if (state.trackingStatus && !state.trackingStatus->empty())
    {
        // Tungsten is the locked subject in the mark. Use it when the tracker
        // has a body; otherwise this line is just a diagnostic.
        const bool locked = state.effectContext && state.effectContext->tracking.valid;
        ImGui::PushStyleColor(ImGuiCol_Text, locked ? theme::tungsten : theme::rackGrey);
        ImGui::TextWrapped("Tracking  %s", state.trackingStatus->c_str());
        ImGui::PopStyleColor();
    }

    drawPickControls(state);

    ImGui::Spacing();

    if (state.source)
    {
        drawParameters(state.source->parameters(), "source");
    }
}

} // namespace atemfx
