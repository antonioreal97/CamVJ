#include "effects/BuiltinEffects.h"
#include "effects/ShaderEffect.h"
#include "effects/face_tiles.h"
#include "tracking/framing.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace atemfx {

namespace {

constexpr const char* kTileShader = "face_tile";

// Faces from the crowd, copied across the frame as independent tiles.
//
// Two draws a frame: the camera as background (face_mosaic, fullscreen), then
// every tile in one batch (face_tile, SpritePass). The tiles sample the chain's
// input, which is the live picture, so each copy moves with its person between
// detections. Detection and identities are control plane (FaceSensor); what
// the tiles do is FaceTileManager; this class only wires them to the GPU.
//
// Phase 1 of docs/plans/2026-09-19-face-mosaic.md: random placement, no frame
// history, no feedback.
class FaceMosaicEffect final : public ShaderEffect
{
public:
    FaceMosaicEffect()
        : ShaderEffect({"face_mosaic", "Face Mosaic", "Crowd",
                        "Copies every face in shot across the frame."},
                       "face_mosaic",
                       SamplerFilter::Linear)
    {
        parameters_.add(Parameter::makeFloat("background", "Background", 1.0f, 0.0f, 1.0f));
        parameters_.add(Parameter::makeFloat("threshold", "Detection Threshold", 0.50f, 0.30f, 1.0f));
        parameters_.add(Parameter::makeInt("max_faces", "Max Faces", 8, 1, static_cast<int>(kMaxFaces)));
        parameters_.add(Parameter::makeFloat("padding", "Face Padding", 0.35f, 0.0f, 1.5f));
        parameters_.add(Parameter::makeInt("copies", "Copies Per Face", 4, 1, 8));
        parameters_.add(Parameter::makeInt("max_tiles", "Max Tiles", 32, 1,
                                           static_cast<int>(kMaxSpriteInstances)));
        parameters_.add(Parameter::makeFloat("min_scale", "Min Scale", 0.7f, 0.2f, 4.0f));
        parameters_.add(Parameter::makeFloat("max_scale", "Max Scale", 1.8f, 0.2f, 4.0f));
        parameters_.add(Parameter::makeFloat("rotation", "Rotation", 0.0f, 0.0f, 45.0f));
        parameters_.add(Parameter::makeFloat("spread", "Random Position", 1.0f, 0.0f, 1.0f));
        parameters_.add(Parameter::makeFloat("opacity", "Tile Opacity", 1.0f, 0.0f, 1.0f));
        parameters_.add(Parameter::makeFloat("feather", "Soft Edge", 0.15f, 0.0f, 1.0f));
        parameters_.add(Parameter::makeFloat("roundness", "Rounded Corners", 0.25f, 0.0f, 1.0f));
        parameters_.add(Parameter::makeFloat("fade", "Fade (s)", 0.30f, 0.0f, 2.0f));
        parameters_.add(Parameter::makeInt("seed", "Layout Seed", 0, 0, 99));
    }

    uint32_t inputs() const override { return kEffectInputFaces; }

    bool initialize(EffectContext& context) override
    {
        if (!ShaderEffect::initialize(context))
        {
            return false;
        }

        if (!context.sprites)
        {
            lastError_ = "Face Mosaic needs the sprite pass";
            return false;
        }

        // Compile the tile shader now so a broken one is reported at load,
        // not when the operator first enables the effect on air.
        if (!context.shaders->shader(kTileShader, &lastError_))
        {
            return false;
        }

        tiles_.reset();
        return true;
    }

    bool process(EffectContext&    context,
                 const GpuTexture& source,
                 GpuTexture&       destination) override
    {
        // Background: the camera, dimmed or gone. Written first so the tiles
        // have a picture to land on; the sprite pass keeps what is there.
        if (!ShaderEffect::process(context, source, destination))
        {
            return false;
        }

        const FaceTileSettings configured = settings(context);

        FacesSnapshot faces;
        mapThroughFraming(context, faces);

        tiles_.update(faces, configured, context.deltaTime);
        const std::size_t count = tiles_.build(instances_.data(), instances_.size(), configured);
        if (count == 0 || !context.sprites)
        {
            return true;
        }

        const ShaderHandle tileShader = context.shaders->shader(kTileShader);
        if (!tileShader)
        {
            // Background only: a broken tile shader costs the tiles, never
            // the frame.
            return true;
        }

        EffectConstants constants;
        setFrameConstants(constants, context.width, context.height, context.time, context.deltaTime);
        setParameterConstant(constants, 0, parameters_.valueOr("feather", 0.15f));
        setParameterConstant(constants, 1, parameters_.valueOr("roundness", 0.25f));

        context.sprites->draw(destination, tileShader, source, constants, instances_.data(), count,
                              SamplerFilter::Linear);
        return true;
    }

    void shutdown() override
    {
        tiles_.reset();
        ShaderEffect::shutdown();
    }

protected:
    void packConstants(const EffectContext& context, EffectConstants& constants) const override
    {
        setFrameConstants(constants, context.width, context.height, context.time, context.deltaTime);
        setParameterConstant(constants, 0, parameters_.valueOr("background", 1.0f));
    }

private:
    FaceTileSettings settings(const EffectContext& context) const
    {
        FaceTileSettings s;
        s.minConfidence      = parameters_.valueOr("threshold", 0.50f);
        s.maxFaces           = static_cast<int>(parameters_.valueOr("max_faces", 8.0f));
        s.padding            = parameters_.valueOr("padding", 0.35f);
        s.copiesPerFace      = static_cast<int>(parameters_.valueOr("copies", 4.0f));
        s.maxTiles           = static_cast<int>(parameters_.valueOr("max_tiles", 32.0f));
        s.minScale           = parameters_.valueOr("min_scale", 0.7f);
        s.maxScale           = parameters_.valueOr("max_scale", 1.8f);
        s.maxRotationDegrees = parameters_.valueOr("rotation", 0.0f);
        s.spread             = parameters_.valueOr("spread", 1.0f);
        s.opacity            = parameters_.valueOr("opacity", 1.0f);
        s.fadeSeconds        = parameters_.valueOr("fade", 0.30f);
        s.seed               = static_cast<uint32_t>(parameters_.valueOr("seed", 0.0f));

        const FramingRect window = outputWindow(context);
        s.areaMinX = window.centerX - window.halfWidth;
        s.areaMaxX = window.centerX + window.halfWidth;
        s.areaMinY = window.centerY - window.halfHeight;
        s.areaMaxY = window.centerY + window.halfHeight;
        return s;
    }

    // The picture this node receives, where Auto Frame has already run.
    static FramingRect outputWindow(const EffectContext& context)
    {
        if (!context.framingActive || context.height == 0)
        {
            return FramingRect{};
        }
        const float canvasAspect = static_cast<float>(context.width) / static_cast<float>(context.height);
        return framingOutputWindow(context.outputAspect, canvasAspect);
    }

    // Faces arrive in the coordinates of the chain's input. When Auto Frame
    // ran earlier in the chain, this node sees its crop instead, so each face
    // is carried through the same transform the auto_frame shader applies.
    // Faces the crop left out are dropped.
    static void mapThroughFraming(const EffectContext& context, FacesSnapshot& out)
    {
        out = context.faces;
        if (!context.framingActive)
        {
            return;
        }

        const FramingRect crop   = context.framing;
        const FramingRect window = outputWindow(context);
        if (crop.halfWidth <= 0.0f || crop.halfHeight <= 0.0f)
        {
            out.count = 0;
            return;
        }

        const float scaleX = window.halfWidth / crop.halfWidth;
        const float scaleY = window.halfHeight / crop.halfHeight;

        uint32_t kept = 0;
        for (uint32_t i = 0; i < context.faces.count && i < kMaxFaces; ++i)
        {
            FaceObservation face = context.faces.faces[i];
            face.centerX = window.centerX + (face.centerX - crop.centerX) * scaleX;
            face.centerY = window.centerY + (face.centerY - crop.centerY) * scaleY;
            face.width *= scaleX;
            face.height *= scaleY;

            if (std::abs(face.centerX - window.centerX) > window.halfWidth ||
                std::abs(face.centerY - window.centerY) > window.halfHeight)
            {
                continue;
            }
            out.faces[kept++] = face;
        }
        out.count = kept;
    }

    FaceTileManager                                      tiles_;
    std::array<SpriteInstance, kMaxSpriteInstances>      instances_{};
};

} // namespace

std::unique_ptr<Effect> createFaceMosaicEffect()
{
    return std::make_unique<FaceMosaicEffect>();
}

} // namespace atemfx
