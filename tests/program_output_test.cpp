#include "video/program_output.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "effects/EffectChain.h"

namespace {

using namespace atemfx;
using Pixel = std::array<uint8_t, 4>;

constexpr Pixel kBlack = {0, 0, 0, 255};
constexpr Pixel kFirst = {12, 34, 56, 255};
constexpr Pixel kNext  = {78, 90, 123, 255};

int checks = 0;
int failures = 0;

void expect(bool condition, const char* description)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", description);
    }
}

bool near(double actual, double expected)
{
    return std::isfinite(actual) && std::abs(actual - expected) < 0.00001;
}

class TestTexture final : public GpuTexture
{
public:
    explicit TestTexture(uint32_t width = 4, uint32_t height = 2)
        : pixels(static_cast<std::size_t>(width) * height), width_(width), height_(height)
    {
    }

    uint32_t width() const override { return width_; }
    uint32_t height() const override { return height_; }
    bool valid() const override { return valid_; }
    void* nativeTexture() const override { return nullptr; }

    void fill(Pixel pixel) { std::fill(pixels.begin(), pixels.end(), pixel); }

    std::vector<Pixel> pixels;
    bool valid_ = true;

private:
    uint32_t width_;
    uint32_t height_;
};

class TestTargets final : public TargetPool
{
public:
    GpuTexture& scratch(std::size_t index) override { return scratch_[index % scratch_.size()]; }

    GpuTexture& persistent(const std::string& key) override
    {
        auto& target = persistent_[key];
        if (!target) target = std::make_unique<TestTexture>(width(), height());
        return *target;
    }

    GpuTexture& uploadTarget(const std::string& key, uint32_t width, uint32_t height) override
    {
        auto& target = uploads_[key];
        if (!target || target->width() != width || target->height() != height)
            target = std::make_unique<TestTexture>(width, height);
        return *target;
    }

    bool upload(GpuTexture& texture, const void* bgra8, std::size_t rowBytes) override
    {
        auto& target = static_cast<TestTexture&>(texture);
        if (!bgra8 || rowBytes < texture.width() * 4) return false;
        const auto* bytes = static_cast<const uint8_t*>(bgra8);
        for (uint32_t y = 0; y < texture.height(); ++y)
        {
            for (uint32_t x = 0; x < texture.width(); ++x)
            {
                const uint8_t* pixel = bytes + y * rowBytes + x * 4;
                target.pixels[y * texture.width() + x] = {pixel[2], pixel[1], pixel[0], pixel[3]};
            }
        }
        return true;
    }

    uint32_t width() const override { return 4; }
    uint32_t height() const override { return 2; }

private:
    std::array<TestTexture, 2> scratch_;
    std::map<std::string, std::unique_ptr<TestTexture>> persistent_;
    std::map<std::string, std::unique_ptr<TestTexture>> uploads_;
};

class TestShaders final : public ShaderLibrary
{
public:
    ShaderHandle shader(const std::string& name, std::string* error = nullptr) override
    {
        if (available && name == "passthrough") return this;
        if (available && crossfadeAvailable && name == "crossfade") return &crossfade_;
        if (error) *error = "Test shader unavailable";
        return nullptr;
    }

    bool reloadAll(std::string&) override { return available; }
    std::string directory() const override { return {}; }

    ShaderHandle crossfadeHandle() { return &crossfade_; }

    bool available = true;
    bool crossfadeAvailable = true;

private:
    char crossfade_ = 0;
};

class TestPass final : public FullscreenPass
{
public:
    void draw(GpuTexture& target, ShaderHandle shader, const GpuTexture* source,
              const EffectConstants& constants, SamplerFilter,
              const GpuTexture* history = nullptr) override
    {
        expect(source && source != &target && shader, "output copies between distinct valid resources");
        if (!source || source == &target || !shader) return;

        // The mix reads a wet and a dry image and writes a third. A backend
        // that had them overlap would sample a target it is writing, which is
        // exactly the bug the chain's ping-pong exists to avoid.
        const bool mixing = crossfade && shader == crossfade;
        if (mixing)
        {
            ++mixes;
            expect(history && history != &target && history != source,
                   "the dissolve reads two distinct images into a third");
            if (!history || history == &target || history == source) return;
        }
        const float amount = mixing ? constants.params[0][0] : 1.0f;

        auto& destination = static_cast<TestTexture&>(target);
        const auto& input = static_cast<const TestTexture&>(*source);
        for (uint32_t y = 0; y < target.height(); ++y)
        {
            for (uint32_t x = 0; x < target.width(); ++x)
            {
                const uint32_t sx = x * source->width() / target.width();
                const uint32_t sy = y * source->height() / target.height();
                Pixel pixel = input.pixels[sy * source->width() + sx];
                if (mixing)
                {
                    const Pixel dry =
                        static_cast<const TestTexture&>(*history).pixels[sy * history->width() + sx];
                    for (std::size_t channel = 0; channel < pixel.size(); ++channel)
                    {
                        const float wet = static_cast<float>(pixel[channel]);
                        pixel[channel] = static_cast<uint8_t>(
                            std::lround(static_cast<float>(dry[channel]) +
                                        (wet - static_cast<float>(dry[channel])) * amount));
                    }
                }
                destination.pixels[y * target.width() + x] = pixel;
            }
        }
        ++draws;
    }

    int draws = 0;
    int mixes = 0;
    ShaderHandle crossfade = nullptr;
};

struct Fixture
{
    Fixture()
    {
        context.targets = &targets;
        context.shaders = &shaders;
        context.fullscreen = &pass;
        context.width = targets.width();
        context.height = targets.height();
        context.deltaTime = 0.25f;
        pass.crossfade = shaders.crossfadeHandle();
        std::string error;
        expect(output.initialize(context, error), "program safety resources initialize");
    }

    TestTargets targets;
    TestShaders shaders;
    TestPass pass;
    EffectContext context;
    ProgramOutput output;
    ProgramMode mode = ProgramMode::Effects;
};

bool solid(const GpuTexture* texture, Pixel expected)
{
    if (!texture || !texture->valid()) return false;
    const auto& pixels = static_cast<const TestTexture&>(*texture).pixels;
    return !pixels.empty() && std::all_of(pixels.begin(), pixels.end(),
                                         [expected](Pixel actual) { return actual == expected; });
}

// Renders until PROGRAM has arrived at the mode on the buttons. Every mode is
// a dissolve now, so a test that wants the destination has to let it land —
// the same handful of frames the operator waits through.
GpuTexture* settle(Fixture& f, GpuTexture* frame, bool healthy)
{
    GpuTexture* result = f.output.render(f.context, frame, healthy, f.mode);
    for (int guard = 0; guard < 64 && f.output.transitioning(); ++guard)
    {
        result = f.output.render(f.context, frame, healthy, f.mode);
    }
    expect(!f.output.transitioning(), "a dissolve always finishes in bounded time");
    return result;
}

bool sameCrop(const FramingRect& a, const FramingRect& b)
{
    return near(a.centerX, b.centerX) && near(a.centerY, b.centerY) &&
           near(a.halfWidth, b.halfWidth) && near(a.halfHeight, b.halfHeight);
}

void checkStartupAndRestart()
{
    Fixture f;
    TestTexture input;
    input.fill(kFirst);

    for (ProgramMode mode : {ProgramMode::Effects, ProgramMode::Clean,
                             ProgramMode::Freeze, ProgramMode::Black})
    {
        f.mode = mode;
        GpuTexture* frame = f.output.render(f.context, nullptr, false, f.mode);
        expect(solid(frame, kBlack), "every mode starts with opaque black when no image exists");
        expect(frame && frame->width() == f.context.width && frame->height() == f.context.height,
               "startup black fills the processing canvas");
        expect(f.mode == mode, "absence of an initial frame does not invent an operator choice");
    }
    expect(f.pass.draws == 1, "startup black is prepared once and reused");

    f.mode = ProgramMode::Freeze;
    expect(solid(f.output.render(f.context, &input, true, f.mode), kBlack),
           "startup Freeze cannot silently take the first camera frame live");
    f.mode = ProgramMode::Black;
    f.output.render(f.context, &input, true, f.mode);
    f.mode = ProgramMode::Freeze;
    expect(solid(f.output.render(f.context, &input, true, f.mode), kBlack),
           "a camera behind startup Black does not replace the empty held image");

    f.mode = ProgramMode::Effects;
    expect(solid(f.output.render(f.context, &input, true, f.mode), kFirst),
           "explicit FX after startup makes the first healthy image live");
    f.output.shutdown();
    expect(f.output.render(f.context, &input, true, f.mode) == nullptr,
           "shutdown releases the output's borrowed resources");
    std::string error;
    expect(f.output.initialize(f.context, error), "program output can restart with an existing pool");
    expect(solid(f.output.render(f.context, nullptr, false, f.mode), kBlack),
           "restart cannot expose a previous session's held picture");
}

void checkFreezeAndBlackPreserveTheImage()
{
    Fixture f;
    auto& scratch = static_cast<TestTexture&>(f.targets.scratch(0));
    scratch.fill(kFirst);
    scratch.pixels.back() = kNext;
    const auto expected = scratch.pixels;
    GpuTexture* held = f.output.render(f.context, &scratch, true, f.mode);
    expect(held && held != &scratch && held != &f.targets.scratch(1),
           "PROGRAM storage is independent of both chain scratch targets");
    expect(held && static_cast<TestTexture&>(*held).pixels == expected,
           "PROGRAM retains the entire image, including nonuniform pixels");

    scratch.fill(kNext);
    static_cast<TestTexture&>(f.targets.scratch(1)).fill(kBlack);
    f.mode = ProgramMode::Freeze;

    // The still is chosen the instant the button is pressed. Chain frames keep
    // arriving all through the dissolve — that is the picture being faded away
    // from — but none of them may reach the one being faded to.
    GpuTexture* mixing = f.output.render(f.context, &scratch, true, f.mode);
    expect(f.output.transitioning() && mixing != held && mixing != &scratch,
           "Freeze dissolves off the live picture instead of cutting to the still");
    expect(held && static_cast<TestTexture&>(*held).pixels == expected,
           "new chain frames cannot overwrite a frozen PROGRAM");

    expect(settle(f, &scratch, true) == held &&
               held && static_cast<TestTexture&>(*held).pixels == expected,
           "the dissolve lands on exactly the frame that was live when Freeze was pressed");
    const int drawsBeforeFreeze = f.pass.draws;
    expect(f.output.render(f.context, &scratch, true, f.mode) == held &&
               f.pass.draws == drawsBeforeFreeze,
           "settled Freeze reuses its stable image without another copy");

    f.mode = ProgramMode::Black;
    GpuTexture* black = settle(f, &scratch, true);
    expect(black != held && solid(black, kBlack), "Black uses a separate opaque safety image");
    const int drawsBeforeBlack = f.pass.draws;
    f.output.render(f.context, nullptr, false, f.mode);
    expect(f.mode == ProgramMode::Black && f.pass.draws == drawsBeforeBlack,
           "loss cannot override an operator's Black, and settled Black costs no pass either");
    f.mode = ProgramMode::Freeze;
    expect(settle(f, &scratch, true) == held &&
               held && static_cast<TestTexture&>(*held).pixels == expected,
           "Freeze after Black recalls the exact last PROGRAM before Black");
    f.mode = ProgramMode::Effects;
    expect(solid(settle(f, &scratch, true), kNext),
           "explicit FX replaces the held image with the current camera picture");
}

void checkLossLatchesUntilManualResume()
{
    for (ProgramMode liveMode : {ProgramMode::Effects, ProgramMode::Clean})
    {
        for (int loss = 0; loss < 3; ++loss)
        {
            Fixture f;
            f.mode = liveMode;
            TestTexture input;
            input.fill(kFirst);
            f.output.render(f.context, &input, true, f.mode);
            input.fill(kNext);
            input.valid_ = loss != 2;
            GpuTexture* lostFrame = loss == 1 ? nullptr : &input;
            expect(solid(f.output.render(f.context, lostFrame, loss != 0, f.mode), kFirst),
                   "unhealthy, missing or invalid input preserves the last good PROGRAM");
            expect(f.mode == ProgramMode::Freeze, "input loss latches Freeze from either live mode");
            input.valid_ = true;
            for (int frame = 0; frame < 3; ++frame)
                expect(solid(f.output.render(f.context, &input, true, f.mode), kFirst) &&
                           f.mode == ProgramMode::Freeze,
                       "camera recovery cannot take moving video live by itself");
            f.mode = liveMode;
            expect(solid(settle(f, &input, true), kNext),
                   "the operator can resume the chosen live mode after recovery");
        }
    }
}

void checkHeldFramingMetadata()
{
    Fixture f;
    TestTexture input;
    input.fill(kFirst);
    const FramingRect portraitCrop = {0.32f, 0.41f, 0.12f, 0.37f};
    f.context.framing = portraitCrop;
    f.context.framingActive = true;
    f.context.outputAspect = 9.0f / 16.0f;
    f.output.render(f.context, &input, true, f.mode);

    for (ProgramMode mode : {ProgramMode::Freeze, ProgramMode::Black})
    {
        f.mode = mode;
        for (int frame = 0; frame < 4; ++frame)
        {
            f.context.framing = {};
            f.context.framingActive = false;
            f.context.outputAspect = 16.0f / 9.0f;
            f.output.render(f.context, &input, true, f.mode);
            expect(sameCrop(f.context.framing, portraitCrop) && f.context.framingActive &&
                       near(f.context.outputAspect, 9.0f / 16.0f),
                   "held preview metadata cannot follow a live crop or aspect change behind "
                   "Freeze/Black, during the dissolve or after it");
        }
    }

    f.mode = ProgramMode::Clean;
    f.context.framing = {};
    f.context.framingActive = false;
    f.context.outputAspect = 16.0f / 9.0f;
    f.output.render(f.context, &input, true, f.mode);
    f.context.framing = portraitCrop;
    f.context.framingActive = true;
    f.context.outputAspect = 9.0f / 16.0f;
    f.output.render(f.context, nullptr, false, f.mode);
    expect(f.mode == ProgramMode::Freeze && sameCrop(f.context.framing, {}) &&
               !f.context.framingActive && near(f.context.outputAspect, 16.0f / 9.0f),
           "loss holds metadata from the last accepted image, including disabled framing");
}

class TestEffect final : public Effect
{
public:
    TestEffect(const char* name, EffectRole role)
        : Effect({name, name, "Test", ""}), role_(role)
    {
        Parameter parameter = Parameter::makeFloat("level", "Level", 0.2f, 0.0f, 1.0f);
        parameter.automation.enabled = true;
        parameter.automation.waveform = AutomationWaveform::RampUp;
        parameter.automation.periodSeconds = 2.0;
        parameters_.add(parameter);
    }

    bool initialize(EffectContext&) override { return true; }
    void shutdown() override {}
    EffectRole role() const override { return role_; }

    bool process(EffectContext& context, const GpuTexture& source, GpuTexture& destination) override
    {
        ++calls;
        auto& result = static_cast<TestTexture&>(destination);
        result.pixels = static_cast<const TestTexture&>(source).pixels;
        for (auto& pixel : result.pixels) ++pixel[role_ == EffectRole::Framing ? 0 : 1];
        if (role_ == EffectRole::Framing)
        {
            context.framing = {0.3f, 0.4f, 0.1f, 0.35f};
            context.framingActive = true;
            context.outputAspect = 9.0f / 16.0f;
        }
        sampledValue = parameters_.valueOr("level", -1.0f);
        return true;
    }

    int calls = 0;
    float sampledValue = -1.0f;

private:
    EffectRole role_;
};

void checkCleanRetainsFramingAndAutomation()
{
    Fixture f;
    EffectChain chain;
    std::string error;
    auto framing = std::make_unique<TestEffect>("composition", EffectRole::Framing);
    auto visual = std::make_unique<TestEffect>("colour", EffectRole::Visual);
    auto disabled = std::make_unique<TestEffect>("unused crop", EffectRole::Framing);
    TestEffect* framingEffect = framing.get();
    TestEffect* visualEffect = visual.get();
    TestEffect* disabledEffect = disabled.get();
    disabled->setEnabled(false);
    expect(chain.add(std::move(visual), f.context, error) &&
               chain.add(std::move(framing), f.context, error) &&
               chain.add(std::move(disabled), f.context, error),
           "mixed visual and framing effects initialize");
    if (chain.size() != 3) return;

    TestTexture input;
    input.fill(kFirst);
    f.mode = ProgramMode::Clean;
    GpuTexture& clean = chain.process(f.context, input, 0.0f);
    expect(solid(&clean, {13, 34, 56, 255}) && framingEffect->calls == 1 &&
               visualEffect->calls == 0 && disabledEffect->calls == 0,
           "Clean runs enabled framing wherever it is in the chain and skips visual effects");
    expect(solid(f.output.render(f.context, &clean, true, f.mode), {13, 34, 56, 255}) &&
               f.context.framingActive && near(f.context.outputAspect, 9.0f / 16.0f),
           "Clean PROGRAM preserves the composed portrait image and its preview metadata");
    expect(visualEffect->enabled() && framingEffect->enabled() && !disabledEffect->enabled() &&
               chain.enabledCount() == 2,
           "Clean leaves every operator enabled flag unchanged");
    expect(near(visualEffect->parameters().find("level")->automation.phase(), 0.125) &&
               near(disabledEffect->parameters().find("level")->automation.phase(), 0.125) &&
               near(framingEffect->sampledValue, 0.125),
           "all effect clocks advance once during Clean, including skipped and disabled nodes");

    f.context.deltaTime = 0.5f;
    f.mode = ProgramMode::Effects;
    GpuTexture& effects = chain.process(f.context, input);
    expect(solid(f.output.render(f.context, &effects, true, f.mode), {13, 35, 56, 255}) &&
               visualEffect->calls == 1 && framingEffect->calls == 2 && disabledEffect->calls == 0,
           "returning to FX restores the same enabled visual chain without enabling unused framing");
    expect(near(visualEffect->sampledValue, 0.375) && near(framingEffect->sampledValue, 0.375),
           "resumed visual effects use the clock advanced while Clean was on air");
    expect(near(visualEffect->parameters().find("level")->value, 0.2),
           "Clean and FX never overwrite the operator's saved manual parameter value");
    chain.shutdown();
}

void checkCalibrationBypassPreservesTheShow()
{
    Fixture f;
    EffectChain chain;
    std::string error;
    auto framing = std::make_unique<TestEffect>("composition", EffectRole::Framing);
    auto visual = std::make_unique<TestEffect>("colour", EffectRole::Visual);
    auto disabled = std::make_unique<TestEffect>("unused", EffectRole::Visual);
    TestEffect* framingEffect = framing.get();
    TestEffect* visualEffect = visual.get();
    TestEffect* disabledEffect = disabled.get();
    disabled->setEnabled(false);
    expect(chain.add(std::move(framing), f.context, error) &&
               chain.add(std::move(visual), f.context, error) &&
               chain.add(std::move(disabled), f.context, error),
           "the calibration regression starts with a configured effect chain");
    if (chain.size() != 3) return;

    TestTexture camera;
    camera.fill(kFirst);
    GpuTexture& live = chain.process(f.context, camera, 1.0f, false);
    GpuTexture* held = f.output.render(f.context, &live, true, f.mode);
    const FramingRect liveCrop = f.context.framing;

    TestTexture chart;
    chart.fill(kNext);
    chart.pixels.front() = kFirst;
    const auto originalChart = chart.pixels;

    // Each iteration is a new App frame: clear live framing before running
    // the source, then let PROGRAM restore metadata only for its held image.
    int frameNumber = 1;
    for (ProgramMode mode : {ProgramMode::Freeze, ProgramMode::Black, ProgramMode::Effects})
    {
        f.context.framing = {};
        f.context.framingActive = false;
        f.context.outputAspect = 16.0f / 9.0f;
        const int drawsBefore = f.pass.draws;
        const int mixesBefore = f.pass.mixes;
        GpuTexture& result = chain.process(f.context, chart, 0.5f, true);
        ++frameNumber;
        expect(&result == &chart && chart.pixels == originalChart,
               "calibration preserves every source pixel even during an FX/Clean dissolve");
        expect(framingEffect->calls == 1 && visualEffect->calls == 1 &&
                   disabledEffect->calls == 0 && f.pass.draws == drawsBefore &&
                   f.pass.mixes == mixesBefore,
               "calibration runs no crop, visual effect or mix pass");
        expect(!f.context.framingActive && sameCrop(f.context.framing, {}) &&
                   near(f.context.outputAspect, 16.0f / 9.0f),
               "calibration keeps the full canvas in the live preview metadata");
        for (const TestEffect* effect : {framingEffect, visualEffect, disabledEffect})
        {
            const Parameter* level = effect->parameters().find("level");
            expect(level && near(level->automation.phase(), frameNumber * 0.125) &&
                       near(level->value, 0.2) && level->automation.enabled &&
                       level->automation.waveform == AutomationWaveform::RampUp &&
                       near(level->automation.periodSeconds, 2.0),
                   "calibration advances each loop once without changing its saved settings");
        }
        expect(framingEffect->enabled() && visualEffect->enabled() &&
                   !disabledEffect->enabled() && chain.enabledCount() == 2,
               "calibration leaves the operator's enabled effect selection intact");

        f.mode = mode;
        GpuTexture* program = settle(f, &result, true);
        expect(f.mode == mode, "a calibration source never selects a PROGRAM mode");
        if (mode == ProgramMode::Effects)
        {
            expect(program && static_cast<TestTexture&>(*program).pixels == originalChart &&
                       !f.context.framingActive && near(f.context.outputAspect, 16.0f / 9.0f),
                   "explicit FX takes the untouched calibration canvas live");
        }
        else
        {
            expect(mode == ProgramMode::Freeze
                       ? program == held && solid(program, {13, 35, 56, 255})
                       : solid(program, kBlack),
                   "calibration cannot replace a frozen picture or operator Black");
            expect(f.context.framingActive && sameCrop(f.context.framing, liveCrop) &&
                       near(f.context.outputAspect, 9.0f / 16.0f),
                   "the held portrait framing survives a calibration chart prepared off air");
        }
    }

    GpuTexture& resumed = chain.process(f.context, camera, 1.0f, false);
    expect(solid(&resumed, {13, 35, 56, 255}) && framingEffect->calls == 2 &&
               visualEffect->calls == 2 && disabledEffect->calls == 0 && f.context.framingActive,
           "leaving calibration resumes the same framing and visual chain");
    expect(near(framingEffect->sampledValue, 0.625) && near(visualEffect->sampledValue, 0.625),
           "resumed effects use the clocks kept running during calibration");
    chain.shutdown();
}

// Writes one flat colour, so what a dissolve did to the picture is readable
// as arithmetic rather than inferred from a look.
class FillEffect final : public Effect
{
public:
    FillEffect(const char* name, EffectRole role, Pixel colour)
        : Effect({name, name, "Test", ""}), role_(role), colour_(colour)
    {
    }

    bool initialize(EffectContext&) override { return true; }
    void shutdown() override {}
    EffectRole role() const override { return role_; }

    bool process(EffectContext&, const GpuTexture&, GpuTexture& destination) override
    {
        ++calls;
        if (!runs) return false;
        auto& result = static_cast<TestTexture&>(destination);
        std::fill(result.pixels.begin(), result.pixels.end(), colour_);
        return true;
    }

    int  calls = 0;
    bool runs  = true;

private:
    EffectRole role_;
    Pixel      colour_;
};

Pixel blend(Pixel dry, Pixel wet, float amount)
{
    Pixel result{};
    for (std::size_t channel = 0; channel < result.size(); ++channel)
    {
        result[channel] = static_cast<uint8_t>(
            std::lround(static_cast<float>(dry[channel]) +
                        (static_cast<float>(wet[channel]) - static_cast<float>(dry[channel])) *
                            amount));
    }
    return result;
}

void checkTransitionRamp()
{
    ProgramTransition transition;
    expect(near(transition.amount(), 1.0) && !transition.active(),
           "a chain that was never switched starts fully on air");

    transition.snapTo(ProgramMode::Clean);
    expect(near(transition.amount(), 0.0) && !transition.active(),
           "start-up lands on its mode with no dissolve to sit through");

    transition.setDuration(1.0f);
    float previous = transition.amount();
    int   frames   = 0;
    while (frames < 20 && transition.amount() < 1.0f)
    {
        const float amount = transition.update(ProgramMode::Effects, 0.1f);
        expect(amount > previous - 0.000001f, "returning to FX never moves the mix backwards");
        previous = amount;
        ++frames;
    }
    // Ten steps of a tenth: the ramp must not need an eleventh to finish, and
    // must not have arrived early either.
    expect(frames == 10 && near(transition.amount(), 1.0) && !transition.active(),
           "the dissolve takes exactly the duration it was given");

    transition.update(ProgramMode::Clean, 0.5f);
    const float midway = transition.amount();
    expect(transition.active() && midway > 0.0f && midway < 1.0f,
           "half a duration leaves the chain half dissolved");
    for (ProgramMode covered : {ProgramMode::Freeze, ProgramMode::Black})
    {
        for (int frame = 0; frame < 5; ++frame)
        {
            expect(near(transition.update(covered, 0.1f), static_cast<double>(midway)),
                   "Freeze and Black hold the mix instead of finishing it off air");
        }
    }
    expect(near(transition.update(ProgramMode::Clean, 0.0f), static_cast<double>(midway)),
           "a frame with no elapsed time moves nothing");

    // Reversing mid-dissolve continues from where the picture actually is.
    const float reversed = transition.update(ProgramMode::Effects, 0.1f);
    expect(reversed > midway && reversed < 1.0f && transition.active(),
           "changing the operator's mind mid-mix resumes from the picture on air");

    transition.setDuration(0.4f);
    expect(near(transition.update(ProgramMode::Clean, 9.0f), 0.0),
           "one very long frame lands on the endpoint rather than overshooting it");
    transition.setDuration(0.0f);
    expect(transition.duration() > 0.0f && near(transition.update(ProgramMode::Effects, 0.05f), 1.0),
           "a zero duration degrades to an instant switch, never to a divide by zero");
}

void checkChainDissolvesOnlyVisualEffects()
{
    const Pixel kWet     = {212, 234, 56, 255};
    const Pixel kFraming = {40, 60, 80, 255};

    // The dissolve itself: one visual node laid back over the picture it was
    // handed, at every point of the ramp including both ends.
    for (float mix : {0.0f, 0.25f, 0.5f, 1.0f})
    {
        Fixture f;
        EffectChain chain;
        std::string error;
        auto visual = std::make_unique<FillEffect>("look", EffectRole::Visual, kWet);
        FillEffect* node = visual.get();
        expect(chain.add(std::move(visual), f.context, error), "the dissolving node initializes");

        TestTexture input;
        input.fill(kFirst);
        const int mixesBefore = f.pass.mixes;
        GpuTexture& result = chain.process(f.context, input, mix);
        expect(solid(&result, blend(kFirst, kWet, mix)),
               "a visual node reaches the output in exactly the proportion asked for");
        expect(node->calls == (mix > 0.0f ? 1 : 0),
               "a node dissolved to nothing costs nothing, and any other amount runs once");
        expect(f.pass.mixes - mixesBefore == (mix > 0.0f && mix < 1.0f ? 1 : 0),
               "the endpoints pay for no mix pass; only a transition does");
        chain.shutdown();
    }

    // Framing is not part of the look and must not move while it fades.
    {
        Fixture f;
        EffectChain chain;
        std::string error;
        auto framing = std::make_unique<FillEffect>("crop", EffectRole::Framing, kFraming);
        FillEffect* node = framing.get();
        expect(chain.add(std::move(framing), f.context, error), "the framing node initializes");
        TestTexture input;
        input.fill(kFirst);
        GpuTexture& result = chain.process(f.context, input, 0.5f);
        expect(solid(&result, kFraming) && node->calls == 1 && f.pass.mixes == 0,
               "framing runs whole through a dissolve, never half applied");
        chain.shutdown();
    }

    // Two dissolving nodes compose, and the ping-pong still never lets a pass
    // read the target it is writing (asserted inside TestPass).
    {
        Fixture f;
        EffectChain chain;
        std::string error;
        const Pixel kSecond = {0, 100, 200, 255};
        expect(chain.add(std::make_unique<FillEffect>("first", EffectRole::Visual, kWet),
                         f.context, error) &&
                   chain.add(std::make_unique<FillEffect>("second", EffectRole::Visual, kSecond),
                             f.context, error),
               "a two node visual chain initializes");
        TestTexture input;
        input.fill(kFirst);
        GpuTexture& result = chain.process(f.context, input, 0.5f);
        expect(solid(&result, blend(blend(kFirst, kWet, 0.5f), kSecond, 0.5f)) && f.pass.mixes == 2,
               "each node dissolves over the picture the one before it produced");
        chain.shutdown();
    }

    // A node that declines to run is still a no-op, dissolving or not.
    {
        Fixture f;
        EffectChain chain;
        std::string error;
        auto visual = std::make_unique<FillEffect>("broken", EffectRole::Visual, kWet);
        visual->runs = false;
        expect(chain.add(std::move(visual), f.context, error), "the declining node initializes");
        TestTexture input;
        input.fill(kFirst);
        GpuTexture& result = chain.process(f.context, input, 0.5f);
        expect(&result == &input && f.pass.mixes == 0,
               "a node that declines mid-dissolve leaves the picture exactly as it found it");
        chain.shutdown();
    }

    // Without the mix shader the chain still has to send a picture: the
    // dissolve is what is lost, not the look and not the frame.
    for (float mix : {0.6f, 0.4f})
    {
        Fixture f;
        f.shaders.crossfadeAvailable = false;
        EffectChain chain;
        std::string error;
        auto visual = std::make_unique<FillEffect>("look", EffectRole::Visual, kWet);
        FillEffect* node = visual.get();
        expect(chain.add(std::move(visual), f.context, error), "the node initializes");
        TestTexture input;
        input.fill(kFirst);
        GpuTexture& result = chain.process(f.context, input, mix);
        const bool wet = mix >= 0.5f;
        expect(solid(&result, wet ? kWet : kFirst) && node->calls == (wet ? 1 : 0) &&
                   f.pass.mixes == 0,
               "a missing mix shader falls back to the nearest end, never to a lost frame");
        chain.shutdown();
    }
}

void checkEveryModeDissolves()
{
    // A whole ramp in two frames, so a single render lands on an exact,
    // readable point of the fade instead of somewhere near it.
    const float halfRamp    = Dissolve::kDefaultSeconds * 0.5f;
    const float quarterRamp = Dissolve::kDefaultSeconds * 0.25f;
    const float quarterEase = 0.15625f;   // smoothstep(0.25)

    {
        Fixture f;
        f.context.deltaTime = halfRamp;
        TestTexture live;
        live.fill(kFirst);
        GpuTexture* held = f.output.render(f.context, &live, true, f.mode);
        expect(solid(held, kFirst) && !f.output.transitioning(),
               "a live mode that never changed is not a transition");

        // The still is latched at the press; the chain goes on producing a
        // different picture, and that is what the audience watches fade out.
        f.mode = ProgramMode::Freeze;
        live.fill(kNext);
        GpuTexture* midway = f.output.render(f.context, &live, true, f.mode);
        expect(f.output.transitioning() && near(f.output.progress(), 0.5) &&
                   solid(midway, blend(kNext, kFirst, 0.5f)),
               "Freeze fades the still up over a picture that is still moving");
        expect(settle(f, &live, true) == held && solid(held, kFirst),
               "the fade lands on the frame that was live when Freeze was pressed");

        f.mode = ProgramMode::Effects;
        expect(solid(settle(f, &live, true), kNext),
               "coming back from Freeze arrives at the picture the chain is making now");

        f.mode = ProgramMode::Black;
        expect(solid(f.output.render(f.context, &live, true, f.mode), blend(kNext, kBlack, 0.5f)),
               "Black fades down rather than cutting the wall to nothing");
        expect(solid(settle(f, &live, true), kBlack), "and it arrives at opaque black");

        f.mode = ProgramMode::Freeze;
        expect(solid(f.output.render(f.context, &live, true, f.mode), blend(kBlack, kNext, 0.5f)),
               "Freeze out of Black fades the still up from black");
    }

    // FX and Clean differ by a chain mix. Fading them here as well would fade
    // the same change twice.
    {
        Fixture f;
        TestTexture live;
        live.fill(kFirst);
        f.output.render(f.context, &live, true, f.mode);
        const int mixesBefore = f.pass.mixes;
        f.mode = ProgramMode::Clean;
        GpuTexture* out = f.output.render(f.context, &live, true, f.mode);
        expect(!f.output.transitioning() && f.pass.mixes == mixesBefore && solid(out, kFirst),
               "Clean is a chain mix, not an output dissolve");
        f.mode = ProgramMode::Effects;
        f.output.render(f.context, &live, true, f.mode);
        expect(!f.output.transitioning() && f.pass.mixes == mixesBefore,
               "and neither is going back to FX");
    }

    // Changing your mind mid-fade, both ways round. Neither may jump the
    // picture: a transition the operator interrupts is still on air.
    {
        Fixture f;
        TestTexture live;
        live.fill(kFirst);
        f.output.render(f.context, &live, true, f.mode);
        live.fill(kNext);
        f.context.deltaTime = quarterRamp;
        f.mode = ProgramMode::Black;
        GpuTexture* onAir = f.output.render(f.context, &live, true, f.mode);
        expect(solid(onAir, blend(kNext, kBlack, quarterEase)), "a quarter of the way down");

        // Same instant, only the button changed.
        f.context.deltaTime = 0.0f;
        f.mode = ProgramMode::Effects;
        expect(solid(f.output.render(f.context, &live, true, f.mode),
                     blend(kNext, kBlack, quarterEase)),
               "turning straight back keeps the picture and only reverses the ramp");
        expect(f.output.transitioning(), "and it still has the rest of the way to travel");
    }

    {
        Fixture f;
        TestTexture live;
        live.fill(kFirst);
        f.output.render(f.context, &live, true, f.mode);
        live.fill(kNext);
        f.context.deltaTime = quarterRamp;
        f.mode = ProgramMode::Black;
        f.output.render(f.context, &live, true, f.mode);

        // Not back the way it came, but somewhere else entirely. What is on air
        // is a blend of two pictures, so the blend is what has to be faded away
        // from — anything else snaps back to a source already left.
        f.context.deltaTime = 0.0f;
        f.mode = ProgramMode::Freeze;
        expect(solid(f.output.render(f.context, &live, true, f.mode),
                     blend(kNext, kBlack, quarterEase)),
               "re-aiming mid-fade dissolves away from the composite on air");
        // Freeze grabs the picture that was on its way out, not one from
        // before the fade started: nothing was reading the still while Black
        // was the target, so it went on tracking the live chain.
        f.context.deltaTime = quarterRamp;
        expect(solid(settle(f, &live, true), kNext),
               "and arrives at the picture that was live when Freeze was pressed");
    }

    // Safety is not a gesture. The last good picture has to be there on the
    // frame the input dies, not 0.35 s later.
    {
        Fixture f;
        TestTexture live;
        live.fill(kFirst);
        f.output.render(f.context, &live, true, f.mode);
        live.fill(kNext);
        GpuTexture* latched = f.output.render(f.context, nullptr, false, f.mode);
        expect(f.mode == ProgramMode::Freeze && !f.output.transitioning() && solid(latched, kFirst),
               "input loss cuts to the held picture, it never fades to it");
    }

    // A missing mix shader costs the transition, never the button.
    {
        Fixture f;
        f.shaders.crossfadeAvailable = false;
        std::string error;
        expect(f.output.initialize(f.context, error), "PROGRAM restarts without a mix shader");
        TestTexture live;
        live.fill(kFirst);
        f.output.render(f.context, &live, true, f.mode);
        for (ProgramMode mode : {ProgramMode::Freeze, ProgramMode::Black, ProgramMode::Effects})
        {
            f.mode = mode;
            GpuTexture* out = f.output.render(f.context, &live, true, f.mode);
            expect(!f.output.transitioning() && f.pass.mixes == 0 &&
                       solid(out, mode == ProgramMode::Black ? kBlack : kFirst),
                   "without a mix shader every mode cuts, which is what it did before");
        }
    }
}

} // namespace

int main()
{
    checkStartupAndRestart();
    checkFreezeAndBlackPreserveTheImage();
    checkLossLatchesUntilManualResume();
    checkHeldFramingMetadata();
    checkCleanRetainsFramingAndAutomation();
    checkCalibrationBypassPreservesTheShow();
    checkTransitionRamp();
    checkChainDissolvesOnlyVisualEffects();
    checkEveryModeDissolves();
    std::printf("program output: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
