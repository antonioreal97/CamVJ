#include "ui/Panels.h"

#include <algorithm>
#include <cmath>

#include "effects/Effect.h"
#include "imgui.h"
#include "tracking/framing.h"
#include "tracking/source_mapping.h"
#include "ui/Fonts.h"
#include "ui/Inspector.h"
#include "ui/Theme.h"

namespace atemfx {

namespace {

ImTextureID textureId(const GpuTexture& texture)
{
    return reinterpret_cast<ImTextureID>(texture.nativeTexture());
}

ImVec2 letterboxSize(ImVec2 available, float aspect)
{
    ImVec2 size = available;
    if (available.x / available.y > aspect)
    {
        size.x = available.y * aspect;
    }
    else
    {
        size.y = available.x / aspect;
    }
    return size;
}

void drawCropMarks(ImVec2 min, ImVec2 max, ImU32 colour)
{
    const float arm   = theme::scaled(10.0f);
    const float thick = theme::scaled(1.5f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddLine(ImVec2(min.x, min.y + arm), min, colour, thick);
    drawList->AddLine(min, ImVec2(min.x + arm, min.y), colour, thick);
    drawList->AddLine(ImVec2(max.x - arm, min.y), ImVec2(max.x, min.y), colour, thick);
    drawList->AddLine(ImVec2(max.x, min.y), ImVec2(max.x, min.y + arm), colour, thick);
    drawList->AddLine(ImVec2(max.x, max.y - arm), max, colour, thick);
    drawList->AddLine(max, ImVec2(max.x - arm, max.y), colour, thick);
    drawList->AddLine(ImVec2(min.x + arm, max.y), ImVec2(min.x, max.y), colour, thick);
    drawList->AddLine(ImVec2(min.x, max.y), ImVec2(min.x, max.y - arm), colour, thick);
}

bool drawFittedImage(const GpuTexture* texture, float aspect, ImVec2 uv0, ImVec2 uv1, ImU32 marks)
{
    if (!texture)
    {
        ImGui::TextDisabled("No signal");
        return false;
    }

    const ImVec2 available = ImGui::GetContentRegionAvail();
    if (available.x <= 1.0f || available.y <= 1.0f)
    {
        return false;
    }

    const ImVec2 size   = letterboxSize(available, aspect);
    const ImVec2 cursor = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(cursor.x + (available.x - size.x) * 0.5f,
                               cursor.y + (available.y - size.y) * 0.5f));
    ImGui::Image(textureId(*texture), size, uv0, uv1);
    drawCropMarks(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), marks);
    return true;
}

void overlayRect(ImDrawList* drawList,
                 ImVec2      imageMin,
                 ImVec2      imageSize,
                 float       centerX,
                 float       centerY,
                 float       halfWidth,
                 float       halfHeight,
                 ImU32       colour,
                 float       thickness)
{
    const float x0 = centerX - halfWidth;
    const float y0 = centerY - halfHeight;
    const float x1 = centerX + halfWidth;
    const float y1 = centerY + halfHeight;

    drawList->AddRect(ImVec2(imageMin.x + x0 * imageSize.x, imageMin.y + y0 * imageSize.y),
                      ImVec2(imageMin.x + x1 * imageSize.x, imageMin.y + y1 * imageSize.y),
                      colour, 0.0f, 0, thickness);
}

void drawSourceOverlays(const EffectContext& context)
{
    const ImVec2 imageMin  = ImGui::GetItemRectMin();
    const ImVec2 imageSize = ImGui::GetItemRectSize();
    if (imageSize.x <= 1.0f || imageSize.y <= 1.0f)
    {
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    if (context.tracking.valid)
    {
        overlayRect(drawList, imageMin, imageSize,
                    context.tracking.centerX, context.tracking.centerY,
                    context.tracking.width * 0.5f, context.tracking.height * 0.5f,
                    theme::kTungstenU32, theme::scaled(2.0f));
    }

    if (context.framingActive)
    {
        overlayRect(drawList, imageMin, imageSize,
                    context.framing.centerX, context.framing.centerY,
                    context.framing.halfWidth, context.framing.halfHeight,
                    theme::kSplitCyanU32, theme::scaled(2.0f));
    }
}

// Thirds and centre over the picture being composed.
//
// Alignment only, and preview only. It is drawn onto the interface's own draw
// list, on top of an ImGui::Image; the texture that reaches the output surface
// and the virtual webcam is the one the chain produced, which this never
// touches. A grid that could go to air would be a grid nobody dares turn on
// during a show.
void drawAlignmentGrid(ImVec2 min, ImVec2 max)
{
    const ImVec2 size(max.x - min.x, max.y - min.y);
    if (size.x <= 4.0f || size.y <= 4.0f)
    {
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 thirds   = IM_COL32(107, 114, 128, 150);
    const ImU32 centre   = IM_COL32(242, 244, 247, 110);
    const float thick    = theme::scaled(1.0f);

    for (int i = 1; i < 3; ++i)
    {
        const float t = static_cast<float>(i) / 3.0f;
        const float x = min.x + size.x * t;
        const float y = min.y + size.y * t;
        drawList->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), thirds, thick);
        drawList->AddLine(ImVec2(min.x, y), ImVec2(max.x, y), thirds, thick);
    }

    // The thirds say where to put the subject; the cross says whether the crop
    // is where the operator thinks it is, which is the other half of aiming.
    const ImVec2 mid(min.x + size.x * 0.5f, min.y + size.y * 0.5f);
    const float  arm = theme::scaled(9.0f);
    drawList->AddLine(ImVec2(mid.x - arm, mid.y), ImVec2(mid.x + arm, mid.y), centre, thick);
    drawList->AddLine(ImVec2(mid.x, mid.y - arm), ImVec2(mid.x, mid.y + arm), centre, thick);
}

struct PickDrag
{
    bool  active = false;
    float x0     = 0.0f;
    float y0     = 0.0f;
    float x1     = 0.0f;
    float y1     = 0.0f;
};

bool imageUv(ImVec2 imageMin, ImVec2 imageSize, float& x, float& y)
{
    if (imageSize.x <= 1.0f || imageSize.y <= 1.0f)
    {
        return false;
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    x = (mouse.x - imageMin.x) / imageSize.x;
    y = (mouse.y - imageMin.y) / imageSize.y;
    return x >= 0.0f && x <= 1.0f && y >= 0.0f && y <= 1.0f;
}

void commitPick(UiFrameState& state, float centerX, float centerY, float width, float height)
{
    if (!state.requestedLock || !state.pickSubjectMode)
    {
        return;
    }

    fillLockRequest(*state.requestedLock, centerX, centerY, width, height);
    *state.pickSubjectMode = false;
}

void drawPickOverlaysAndHandle(UiFrameState& state)
{
    const ImVec2 imageMin  = ImGui::GetItemRectMin();
    const ImVec2 imageSize = ImGui::GetItemRectSize();
    if (imageSize.x <= 1.0f || imageSize.y <= 1.0f)
    {
        return;
    }

    static PickDrag drag;

    const bool locked =
        state.effectContext && state.effectContext->tracking.locked;
    const bool picking = state.pickSubjectMode && *state.pickSubjectMode;
    // Until the operator locks, SOURCE is the picker — no extra button.
    // After a lock, Pick subject is required so a stray click cannot steal
    // the shot.
    const bool choosing = picking || !locked;

    if (!choosing)
    {
        drag.active = false;
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    float hoverX = 0.0f;
    float hoverY = 0.0f;
    const bool hovered = ImGui::IsItemHovered() && imageUv(imageMin, imageSize, hoverX, hoverY);
    const int  hoverHit =
        (hovered && state.trackingCandidates)
            ? hitTestCandidate(state.trackingCandidates->data(), state.trackingCandidates->size(),
                               hoverX, hoverY)
            : -1;

    if (state.trackingCandidates)
    {
        for (std::size_t i = 0; i < state.trackingCandidates->size(); ++i)
        {
            const TrackingCandidate& candidate = (*state.trackingCandidates)[i];
            const bool               hot       = static_cast<int>(i) == hoverHit && !drag.active;
            overlayRect(drawList, imageMin, imageSize, candidate.centerX, candidate.centerY,
                        candidate.width * 0.5f, candidate.height * 0.5f,
                        hot ? theme::kKeyLightU32 : theme::kRackGreyU32,
                        theme::scaled(hot ? 2.0f : 1.0f));
        }
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        drag.active = true;
        drag.x0 = drag.x1 = hoverX;
        drag.y0 = drag.y1 = hoverY;
    }

    if (drag.active)
    {
        float x = hoverX;
        float y = hoverY;
        if (!hovered)
        {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            x = std::clamp((mouse.x - imageMin.x) / imageSize.x, 0.0f, 1.0f);
            y = std::clamp((mouse.y - imageMin.y) / imageSize.y, 0.0f, 1.0f);
        }
        drag.x1 = x;
        drag.y1 = y;

        if (pickIsDrag(drag.x0, drag.y0, drag.x1, drag.y1))
        {
            const float cx = (drag.x0 + drag.x1) * 0.5f;
            const float cy = (drag.y0 + drag.y1) * 0.5f;
            overlayRect(drawList, imageMin, imageSize, cx, cy,
                        std::abs(drag.x1 - drag.x0) * 0.5f, std::abs(drag.y1 - drag.y0) * 0.5f,
                        theme::kSplitCyanU32, theme::scaled(1.5f));
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            if (pickIsDrag(drag.x0, drag.y0, drag.x1, drag.y1))
            {
                commitPick(state, (drag.x0 + drag.x1) * 0.5f, (drag.y0 + drag.y1) * 0.5f,
                           std::abs(drag.x1 - drag.x0), std::abs(drag.y1 - drag.y0));
            }
            else if (state.trackingCandidates)
            {
                const int hit = hitTestCandidate(state.trackingCandidates->data(),
                                                 state.trackingCandidates->size(), drag.x0, drag.y0);
                if (hit >= 0)
                {
                    const TrackingCandidate& candidate =
                        (*state.trackingCandidates)[static_cast<std::size_t>(hit)];
                    commitPick(state, candidate.centerX, candidate.centerY, candidate.width,
                               candidate.height);
                }
                else
                {
                    float cx = 0.0f;
                    float cy = 0.0f;
                    float w  = 0.0f;
                    float h  = 0.0f;
                    defaultLockRect(drag.x0, drag.y0, cx, cy, w, h);
                    commitPick(state, cx, cy, w, h);
                }
            }
            else
            {
                float cx = 0.0f;
                float cy = 0.0f;
                float w  = 0.0f;
                float h  = 0.0f;
                defaultLockRect(drag.x0, drag.y0, cx, cy, w, h);
                commitPick(state, cx, cy, w, h);
            }
            drag.active = false;
        }
    }
}

void drawMonitorLabel(const char* title, const ImVec4& accent, const char* tag)
{
    ui::pushMono();
    // Frame-height so this row matches the bus selector opposite it and the
    // two images keep the same top edge.
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    if (tag && tag[0] != '\0')
    {
        ImGui::SameLine(0.0f, theme::scaled(10.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, theme::tungsten);
        ImGui::TextUnformatted(tag);
        ImGui::PopStyleColor();
    }
    ui::popMono();
}

// Which picture the left monitor is showing. The operator's choice, so it
// lives for the session rather than being derived from program state: a look
// being built behind Freeze must not disappear the moment Freeze is released.
enum class PreviewBus
{
    Source,
    Fx,
};

PreviewBus g_bus = PreviewBus::Source;

// Equal widths so the two read as one selector with a position, the way a
// mixer's bus buttons do, rather than as two buttons that happen to be
// adjacent.
bool busButton(const char* label, bool active)
{
    ImGui::PushStyleColor(ImGuiCol_Button, active ? theme::splitCyan : theme::surface);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? theme::splitCyan : theme::surfaceHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::surfaceActive);
    ImGui::PushStyleColor(ImGuiCol_Text, active ? theme::studioBlack : theme::rackGrey);
    const bool pressed = ImGui::Button(label, ImVec2(theme::scaled(72.0f), 0.0f));
    ImGui::PopStyleColor(4);
    return pressed;
}

// The output window as UVs on the canvas, when framing has narrowed it. Both
// monitors need this: PROGRAM to show what is going out, the FX bus to show
// what would go out if it were taken.
bool framingWindowUv(bool  framingActive,
                     float outputAspect,
                     float canvasAspect,
                     ImVec2& uv0,
                     ImVec2& uv1,
                     float&  aspect)
{
    if (!framingActive)
    {
        return false;
    }

    const FramingRect window = framingOutputWindow(outputAspect, canvasAspect);
    if (window.halfWidth >= 0.5f - 1.0e-4f)
    {
        return false;
    }

    uv0    = ImVec2(window.centerX - window.halfWidth, window.centerY - window.halfHeight);
    uv1    = ImVec2(window.centerX + window.halfWidth, window.centerY + window.halfHeight);
    aspect = outputAspect;
    return true;
}

// Picking a subject is done by clicking the camera image, so a live pick owns
// the left monitor until it is finished: an operator told to click SOURCE must
// be looking at SOURCE.
//
// Not having picked yet is not a live pick. The tracker chooses for itself
// until the first Pick, and while it is following someone the operator is free
// to watch the FX bus. The monitor is taken only when they asked for a Pick,
// or when there are people in shot and nobody is being followed - which is the
// one state where clicking SOURCE is the next thing to do.
bool pickOwnsMonitor(const UiFrameState& state)
{
    if (!state.trackingAvailable)
    {
        return false;
    }
    if (state.pickSubjectMode && *state.pickSubjectMode)
    {
        return true;
    }

    const bool locked    = state.effectContext && state.effectContext->tracking.locked;
    const bool following = state.effectContext && state.effectContext->tracking.valid;
    return !locked && !following && state.trackingCandidates &&
           !state.trackingCandidates->empty();
}

void drawSourceBus(UiFrameState& state, float canvasAspect, bool grid)
{
    const GpuTexture* source = state.sourcePreview ? state.sourcePreview : state.preview;
    if (!drawFittedImage(source, canvasAspect, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                         theme::kSplitCyanU32))
    {
        return;
    }

    const ImVec2 imageMin = ImGui::GetItemRectMin();
    const ImVec2 imageMax = ImGui::GetItemRectMax();

    if (state.effectContext)
    {
        drawSourceOverlays(*state.effectContext);
    }

    if (grid)
    {
        // Inside the crop when there is one: what the operator is composing is
        // the picture that leaves, not the whole sensor. Thirds over the full
        // frame would be measuring the wrong rectangle.
        if (state.effectContext && state.effectContext->framingActive)
        {
            const FramingRect& framing  = state.effectContext->framing;
            const ImVec2       imageSize(imageMax.x - imageMin.x, imageMax.y - imageMin.y);
            drawAlignmentGrid(
                ImVec2(imageMin.x + (framing.centerX - framing.halfWidth) * imageSize.x,
                       imageMin.y + (framing.centerY - framing.halfHeight) * imageSize.y),
                ImVec2(imageMin.x + (framing.centerX + framing.halfWidth) * imageSize.x,
                       imageMin.y + (framing.centerY + framing.halfHeight) * imageSize.y));
        }
        else
        {
            drawAlignmentGrid(imageMin, imageMax);
        }
    }

    drawPickOverlaysAndHandle(state);
}

// The chain's own image: what FX would put on air right now. No subject or
// framing overlays, because after Auto Frame has run the crop is the whole
// picture and a rectangle drawn on it would be a lie.
void drawFxBus(UiFrameState& state, float canvasAspect, bool grid)
{
    ImVec2 uv0(0.0f, 0.0f);
    ImVec2 uv1(1.0f, 1.0f);
    float  aspect = canvasAspect;
    framingWindowUv(state.chainFramingActive, state.chainAspect, canvasAspect, uv0, uv1, aspect);
    if (drawFittedImage(state.chainPreview, aspect, uv0, uv1, theme::kSplitCyanU32) && grid)
    {
        drawAlignmentGrid(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    }
}

} // namespace

void drawPreviewPanel(UiFrameState& state)
{
    if (state.processingHeight == 0)
    {
        ImGui::TextDisabled("No signal");
        return;
    }

    const float canvasAspect =
        static_cast<float>(state.processingWidth) / static_cast<float>(state.processingHeight);

    const ImVec2 available = ImGui::GetContentRegionAvail();
    if (available.x <= 1.0f || available.y <= 1.0f)
    {
        return;
    }

    const float gap   = theme::scaled(10.0f);
    const float paneW = (available.x - gap) * 0.5f;
    const ImVec2 pane(paneW, available.y);

    // Only while the framing is being set, which is the one moment thirds and
    // a centre cross help and every other moment they are clutter over a live
    // picture.
    const bool grid = ui::adjustingFraming() && ui::framingGridEnabled();

    if (ImGui::BeginChild("##source_preview", pane, ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar))
    {
        // Two buses on one monitor, the way a mixer offers preview and
        // program: the camera as it arrives, or the chain's own image, which
        // is what FX would take. Before this, a look could only be judged
        // after it was already on air.
        const bool picking = pickOwnsMonitor(state);
        if (picking)
        {
            g_bus = PreviewBus::Source;
        }

        ui::pushMono();
        ImGui::BeginDisabled(picking);
        if (busButton("SOURCE", g_bus == PreviewBus::Source))
        {
            g_bus = PreviewBus::Source;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip(picking ? "Pick the subject here first."
                                      : "The camera as it arrives, before the chain.");
        }
        ImGui::SameLine(0.0f, theme::scaled(6.0f));
        if (busButton("FX", g_bus == PreviewBus::Fx))
        {
            g_bus = PreviewBus::Fx;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip(picking
                ? "Pick the subject here first."
                : "The chain's image: what FX would take. Build a look behind Freeze or Black,\nwatch it here, then press FX.");
        }
        ImGui::EndDisabled();

        // Clean is the chain ramped out, so the FX bus honestly shows no look
        // at all. Say so rather than leaving the operator to wonder whether
        // their effect is broken.
        if (g_bus == PreviewBus::Fx && state.programMix < 0.999f)
        {
            ImGui::SameLine(0.0f, theme::scaled(12.0f));
            ImGui::AlignTextToFramePadding();
            ImGui::PushStyleColor(ImGuiCol_Text, theme::rackGrey);
            ImGui::Text("MIX %.0f%%", static_cast<double>(state.programMix) * 100.0);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Clean ramps the look out of the chain itself, so the preview\nfollows it. Use Freeze or Black to build a look off air.");
            }
        }
        ui::popMono();

        if (g_bus == PreviewBus::Fx)
        {
            drawFxBus(state, canvasAspect, grid);
        }
        else
        {
            drawSourceBus(state, canvasAspect, grid);
        }
    }
    ImGui::EndChild();

    ImGui::SameLine(0.0f, gap);

    if (ImGui::BeginChild("##program_preview", pane, ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar))
    {
        const char* tag = nullptr;
        if (state.outputActive)
        {
            tag = state.inputHealthy ? "LIVE" : "FROZEN";
        }
        drawMonitorLabel("PROGRAM", theme::splitMagenta, tag);

        ImVec2 uv0(0.0f, 0.0f);
        ImVec2 uv1(1.0f, 1.0f);
        float  programAspect = canvasAspect;

        if (state.effectContext)
        {
            framingWindowUv(state.effectContext->framingActive,
                            state.effectContext->outputAspect, canvasAspect, uv0, uv1,
                            programAspect);
        }

        if (drawFittedImage(state.preview, programAspect, uv0, uv1, theme::kSplitMagentaU32) &&
            grid)
        {
            drawAlignmentGrid(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        }
    }
    ImGui::EndChild();
}

} // namespace atemfx
