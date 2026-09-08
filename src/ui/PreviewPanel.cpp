#include "ui/Panels.h"

#include "effects/Effect.h"
#include "imgui.h"
#include "tracking/framing.h"
#include "ui/Fonts.h"
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

void drawMonitorLabel(const char* title, const ImVec4& accent, const char* tag)
{
    ui::pushMono();
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

    if (ImGui::BeginChild("##source_preview", pane, ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar))
    {
        drawMonitorLabel("SOURCE", theme::splitCyan, nullptr);
        const GpuTexture* source = state.sourcePreview ? state.sourcePreview : state.preview;
        if (drawFittedImage(source, canvasAspect, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                            theme::kSplitCyanU32) &&
            state.effectContext)
        {
            drawSourceOverlays(*state.effectContext);
        }
    }
    ImGui::EndChild();

    ImGui::SameLine(0.0f, gap);

    if (ImGui::BeginChild("##program_preview", pane, ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar))
    {
        const char* tag = state.outputActive ? "LIVE" : nullptr;
        drawMonitorLabel("PROGRAM", theme::splitMagenta, tag);

        ImVec2 uv0(0.0f, 0.0f);
        ImVec2 uv1(1.0f, 1.0f);
        float  programAspect = canvasAspect;

        if (state.effectContext && state.effectContext->framingActive)
        {
            const FramingRect window =
                framingOutputWindow(state.effectContext->outputAspect, canvasAspect);
            if (window.halfWidth < 0.5f - 1.0e-4f)
            {
                uv0 = ImVec2(window.centerX - window.halfWidth,
                             window.centerY - window.halfHeight);
                uv1 = ImVec2(window.centerX + window.halfWidth,
                             window.centerY + window.halfHeight);
                programAspect = state.effectContext->outputAspect;
            }
        }

        drawFittedImage(state.preview, programAspect, uv0, uv1, theme::kSplitMagentaU32);
    }
    ImGui::EndChild();
}

} // namespace atemfx
