#include "platform/display_routing.h"

#include <cstdio>
#include <limits>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

void expect(bool condition, const char* description)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", description);
    }
}

atemfx::DisplayInfo display(const char* id)
{
    atemfx::DisplayInfo result;
    result.id = id;
    result.name = id;
    result.width = 1920;
    result.height = 1080;
    result.refreshHz = 59.94;
    return result;
}

void expectLoss(const atemfx::DisplayRoutingUpdate& update, atemfx::DisplayRouteLoss loss,
                const char* description)
{
    expect(update.loss == loss && update.selected == -1 && update.requested == -1, description);
}

void checkUnrelatedTopologyChanges()
{
    const std::vector previous{display("operator"), display("wall"), display("spare")};
    std::vector current{display("spare"), display("wall"), display("operator")};
    auto update = atemfx::reconcileDisplayRouting(previous, current, 1, 2);
    expect(update.selected == 1 && update.requested == 0 &&
               update.loss == atemfx::DisplayRouteLoss::None,
           "reordering preserves the live and pending destinations by ID");

    current = {display("wall"), display("new")};
    update = atemfx::reconcileDisplayRouting(previous, current, 1, 1);
    expect(update.selected == 0 && update.requested == 0 &&
               update.loss == atemfx::DisplayRouteLoss::None,
           "unrelated removal and addition preserve the wall at its new index");

    current[0].name = "Renamed wall";
    current[0].primary = true;
    update = atemfx::reconcileDisplayRouting(previous, current, 1, 1);
    expect(update.selected == 0 && update.requested == 0 &&
               update.loss == atemfx::DisplayRouteLoss::None,
           "name and primary metadata do not invalidate the route");
}

void checkLostLiveRoute()
{
    const std::vector previous{display("operator"), display("wall"), display("spare")};
    const std::vector current{display("operator"), display("spare")};
    expectLoss(atemfx::reconcileDisplayRouting(previous, current, 1, 2),
               atemfx::DisplayRouteLoss::Disconnected,
               "a missing live wall closes output and cancels even a valid pending destination");

    expectLoss(atemfx::reconcileDisplayRouting(previous, {}, 1, 1),
               atemfx::DisplayRouteLoss::Disconnected,
               "losing every display closes output");

    const auto returned = atemfx::reconcileDisplayRouting(current, previous, -1, -1);
    expect(returned.selected == -1 && returned.requested == -1 &&
               returned.loss == atemfx::DisplayRouteLoss::None,
           "a returning display never reopens an inactive route");
}

void checkPendingRoute()
{
    const std::vector previous{display("operator"), display("wall"), display("spare")};
    const std::vector current{display("operator"), display("spare")};
    auto update = atemfx::reconcileDisplayRouting(previous, current, 0, 1);
    expect(update.selected == 0 && update.requested == -1 &&
               update.loss == atemfx::DisplayRouteLoss::None,
           "a missing pending destination cannot become the display at its stale index");

    update = atemfx::reconcileDisplayRouting(previous, current, -1, 2);
    expect(update.selected == -1 && update.requested == 1 &&
               update.loss == atemfx::DisplayRouteLoss::None,
           "an explicit pending request maps by ID while output is inactive");

    update = atemfx::reconcileDisplayRouting(previous, current, 0, -1);
    expect(update.selected == 0 && update.requested == -1,
           "a pending request to stop output remains off");
}

void checkModeChanges()
{
    const std::vector previous{display("wall"), display("spare")};
    auto current = previous;
    current[0].width = 1280;
    expectLoss(atemfx::reconcileDisplayRouting(previous, current, 0, 1),
               atemfx::DisplayRouteLoss::ModeChanged,
               "a live width change closes output and cancels the pending route");

    current = previous;
    current[0].height = 720;
    expectLoss(atemfx::reconcileDisplayRouting(previous, current, 0, 0),
               atemfx::DisplayRouteLoss::ModeChanged,
               "a live height change closes output");

    current = previous;
    current[0].refreshHz = 60.0;
    expectLoss(atemfx::reconcileDisplayRouting(previous, current, 0, 0),
               atemfx::DisplayRouteLoss::ModeChanged,
               "59.94 to 60 Hz is a real live mode change");

    current = previous;
    current[1].width = 1280;
    auto update = atemfx::reconcileDisplayRouting(previous, current, 0, 1);
    expect(update.selected == 0 && update.requested == 1 &&
               update.loss == atemfx::DisplayRouteLoss::None,
           "a mode change on an unrelated display preserves the live route");

    current = previous;
    current[0].desktopX = 1920;
    expectLoss(atemfx::reconcileDisplayRouting(previous, current, 0, 1),
               atemfx::DisplayRouteLoss::ModeChanged,
               "a live desktop origin change closes output and cancels the pending route");

    current = previous;
    current[0].desktopY = -1080;
    expectLoss(atemfx::reconcileDisplayRouting(previous, current, 0, 0),
               atemfx::DisplayRouteLoss::ModeChanged,
               "a live desktop origin Y change closes output");

    current = previous;
    current[0].desktopWidth = 1280;
    expectLoss(atemfx::reconcileDisplayRouting(previous, current, 0, 0),
               atemfx::DisplayRouteLoss::ModeChanged,
               "a live logical width change closes output while the native raster stays");

    current = previous;
    current[0].desktopHeight = 800;
    expectLoss(atemfx::reconcileDisplayRouting(previous, current, 0, 0),
               atemfx::DisplayRouteLoss::ModeChanged,
               "a live logical height change closes output");

    current = previous;
    current[0].scaleFactor = 2.0f;
    expectLoss(atemfx::reconcileDisplayRouting(previous, current, 0, 0),
               atemfx::DisplayRouteLoss::ModeChanged,
               "a live backing-scale change closes output");

    current = previous;
    current[1].desktopX = 1920;
    current[1].desktopWidth = 1280;
    current[1].scaleFactor = 2.0f;
    update = atemfx::reconcileDisplayRouting(previous, current, 0, 1);
    expect(update.selected == 0 && update.requested == 1 &&
               update.loss == atemfx::DisplayRouteLoss::None,
           "geometry change on an unrelated display preserves the live route");
}

void checkRefreshPrecision()
{
    const std::vector previous{display("wall")};
    auto current = previous;
    current[0].refreshHz = 59.94006;
    auto update = atemfx::reconcileDisplayRouting(previous, current, 0, 0);
    expect(update.selected == 0 && update.loss == atemfx::DisplayRouteLoss::None,
           "refresh reporting noise keeps output live");

    current[0].refreshHz = 0.0;
    update = atemfx::reconcileDisplayRouting(previous, current, 0, 0);
    expect(update.selected == 0 && update.loss == atemfx::DisplayRouteLoss::None,
           "a newly unknown refresh rate cannot establish a mode change");
    update = atemfx::reconcileDisplayRouting(current, previous, 0, 0);
    expect(update.selected == 0 && update.loss == atemfx::DisplayRouteLoss::None,
           "a newly known refresh rate cannot establish a mode change");

    current[0].refreshHz = std::numeric_limits<double>::quiet_NaN();
    update = atemfx::reconcileDisplayRouting(previous, current, 0, 0);
    expect(update.selected == 0 && update.loss == atemfx::DisplayRouteLoss::None,
           "a nonfinite refresh report is treated as unknown");
}

void checkInvalidIndexes()
{
    const std::vector previous{display("wall")};
    const std::vector current{display("wall"), display("new")};
    const auto update = atemfx::reconcileDisplayRouting(previous, current, 1, 1);
    expect(update.selected == -1 && update.requested == -1 &&
               update.loss == atemfx::DisplayRouteLoss::None,
           "indexes outside the old snapshot cannot select a newly added display");

    const auto negative = atemfx::reconcileDisplayRouting(previous, current, -2, -2);
    expect(negative.selected == -1 && negative.requested == -1 &&
               negative.loss == atemfx::DisplayRouteLoss::None,
           "invalid negative indexes remain off");

    const auto empty = atemfx::reconcileDisplayRouting({}, current, 0, 0);
    expect(empty.selected == -1 && empty.requested == -1 &&
               empty.loss == atemfx::DisplayRouteLoss::None,
           "an empty old snapshot never selects the first new display");
}

} // namespace

int main()
{
    checkUnrelatedTopologyChanges();
    checkLostLiveRoute();
    checkPendingRoute();
    checkModeChanges();
    checkRefreshPrecision();
    checkInvalidIndexes();

    if (failures != 0)
    {
        std::fprintf(stderr, "%d / %d checks failed\n", failures, checks);
        return 1;
    }
    std::printf("%d checks passed\n", checks);
    return 0;
}
