#include "overlays/overlay_compositor.h"

#include <array>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace atemfx;

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

class TestTexture final : public GpuTexture
{
public:
    explicit TestTexture(int label = 0, std::uint32_t width = 1920,
                         std::uint32_t height = 1080)
        : label(label), width_(width), height_(height)
    {
    }

    std::uint32_t width() const override { return width_; }
    std::uint32_t height() const override { return height_; }
    bool valid() const override { return valid_; }
    void* nativeTexture() const override { return nullptr; }

    int label = 0;
    bool valid_ = true;

private:
    std::uint32_t width_;
    std::uint32_t height_;
};

class TestTargets final : public TargetPool
{
public:
    GpuTexture& scratch(std::size_t index) override { return scratch_[index % 2]; }

    GpuTexture& persistent(const std::string& key) override
    {
        auto& target = persistent_[key];
        if (!target) target = std::make_unique<TestTexture>(nextLabel_++);
        return *target;
    }

    GpuTexture& uploadTarget(const std::string& key, std::uint32_t width,
                             std::uint32_t height) override
    {
        auto& target = uploads_[key];
        if (!target) target = std::make_unique<TestTexture>(nextLabel_++, width, height);
        return *target;
    }

    bool upload(GpuTexture&, const void*, std::size_t) override { return true; }
    std::uint32_t width() const override { return 1920; }
    std::uint32_t height() const override { return 1080; }

private:
    std::array<TestTexture, 2> scratch_ = {TestTexture{10}, TestTexture{11}};
    std::map<std::string, std::unique_ptr<TestTexture>> persistent_;
    std::map<std::string, std::unique_ptr<TestTexture>> uploads_;
    int nextLabel_ = 100;
};

class TestShaders final : public ShaderLibrary
{
public:
    ShaderHandle shader(const std::string& name, std::string* error = nullptr) override
    {
        if (available && name == "overlay_composite") return this;
        if (error) *error = "not available";
        return nullptr;
    }

    bool reloadAll(std::string&) override { return available; }
    std::string directory() const override { return {}; }

    bool available = true;
};

class TestPass final : public FullscreenPass
{
public:
    struct Draw
    {
        const GpuTexture* target;
        const GpuTexture* overlay;
        const GpuTexture* under;
        float opacity;
        bool portrait;
        SamplerFilter filter;
    };

    void draw(GpuTexture& target, ShaderHandle shader, const GpuTexture* source,
              const EffectConstants& constants, SamplerFilter filter,
              const GpuTexture* history = nullptr) override
    {
        expect(shader && source && history, "overlay draw binds both pictures and a shader");
        expect(&target != source && &target != history && source != history,
               "overlay ping-pong never reads the resource it writes");
        draws.push_back({&target, source, history, constants.params[0][0],
                         constants.params[0][1] > 0.5f, filter});
    }

    std::vector<Draw> draws;
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
        std::string error;
        expect(compositor.initialize(context, error), "compositor prepares shader and targets");
    }

    TestTargets targets;
    TestShaders shaders;
    TestPass pass;
    EffectContext context;
    OverlayCompositor compositor;
};

void checkBypasses()
{
    Fixture f;
    TestTexture input(1);
    TestTexture overlay(2);
    const OverlayCompositeLayer layer{&overlay, OverlayAspect::Landscape16x9, 1.0f};

    expect(&f.compositor.composite(f.context, input, {}, 1.0f, false) == &input,
           "an empty stack is a zero-pass identity");
    expect(&f.compositor.composite(f.context, input, {&layer, 1}, 0.0f, false) == &input,
           "Clean removes overlays with no redundant pass at its endpoint");
    expect(&f.compositor.composite(f.context, input, {&layer, 1}, 1.0f, true) == &input,
           "calibration bypass cannot leak an overlay into the guide image");
    expect(f.pass.draws.empty(), "all identity cases avoid GPU work");

    f.shaders.available = false;
    expect(&f.compositor.composite(f.context, input, {&layer, 1}) == &input,
           "a missing shader preserves the unmodified input");
}

void checkOrderingAndConstants()
{
    Fixture f;
    TestTexture input(1);
    std::array<TestTexture, 4> textures = {
        TestTexture{2}, TestTexture{3}, TestTexture{4}, TestTexture{5}};
    std::array<OverlayCompositeLayer, 4> layers = {{
        {&textures[0], OverlayAspect::Landscape16x9, 1.0f},
        {&textures[1], OverlayAspect::Portrait9x16, 0.8f},
        {&textures[2], OverlayAspect::Landscape16x9, 0.0f},
        {&textures[3], OverlayAspect::Landscape16x9, 0.5f},
    }};

    GpuTexture& result = f.compositor.composite(f.context, input, layers, 0.5f);
    expect(f.pass.draws.size() == 3 && f.compositor.lastPassCount() == 3,
           "only visible valid layers consume passes");
    expect(f.pass.draws[0].overlay == &textures[0] && f.pass.draws[0].under == &input,
           "the first array entry is the bottom overlay");
    expect(f.pass.draws[1].overlay == &textures[1] &&
               f.pass.draws[1].under == f.pass.draws[0].target,
           "each higher layer samples the completed lower stack");
    expect(&result == f.pass.draws.back().target,
           "the compositor returns the last completed ping-pong target");
    expect(f.pass.draws[0].target != f.pass.draws[1].target &&
               f.pass.draws[0].target == f.pass.draws[2].target,
           "two private targets alternate without allocation");
    expect(f.pass.draws[0].opacity == 0.5f && f.pass.draws[1].opacity == 0.4f &&
               f.pass.draws[2].opacity == 0.25f,
           "layer opacity is multiplied by the existing FX/Clean amount");
    expect(!f.pass.draws[0].portrait && f.pass.draws[1].portrait,
           "the shader receives the per-variant portrait mapping flag");
    expect(f.pass.draws[0].filter == SamplerFilter::Linear,
           "PNG overlays use linear sampling when mapped into the output window");
}

void checkInvalidLayerIsolation()
{
    Fixture f;
    TestTexture input(1);
    TestTexture broken(2);
    broken.valid_ = false;
    TestTexture good(3);
    std::array<OverlayCompositeLayer, 2> layers = {{
        {&broken, OverlayAspect::Landscape16x9, 1.0f},
        {&good, OverlayAspect::Landscape16x9, 1.0f},
    }};

    f.compositor.composite(f.context, input, layers);
    expect(f.pass.draws.size() == 1 && f.pass.draws[0].overlay == &good,
           "one bad layer cannot take the remaining stack off air");
}

} // namespace

int main()
{
    checkBypasses();
    checkOrderingAndConstants();
    checkInvalidLayerIsolation();

    if (failures == 0)
    {
        std::printf("overlay compositor: %d checks passed\n", checks);
    }
    return failures == 0 ? 0 : 1;
}
