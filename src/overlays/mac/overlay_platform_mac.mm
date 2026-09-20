#include "overlays/overlay_platform.h"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>

#include <limits>
#include <mutex>
#include <system_error>

namespace atemfx {
namespace {

struct PickerSharedState
{
    std::mutex          mutex;
    OverlayPickerResult result;
    std::uint64_t       generation = 0;
};

class MacOverlaySourcePicker final : public OverlaySourcePicker
{
public:
    MacOverlaySourcePicker() : shared_(std::make_shared<PickerSharedState>()) {}

    ~MacOverlaySourcePicker() override
    {
        cancel();
        panel_ = nil;
    }

    bool begin(OverlayPickerMode mode, void* nativeParent, std::string& error) override
    {
        if (![NSThread isMainThread])
        {
            error = "Overlay picker must be opened on the main thread";
            return false;
        }
        std::uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(shared_->mutex);
            if (shared_->result.state == OverlayPickerState::Picking)
            {
                error = "An overlay picker is already open";
                return false;
            }
            shared_->result = {};
            shared_->result.state = OverlayPickerState::Picking;
            generation = ++shared_->generation;
        }

        panel_ = [NSOpenPanel openPanel];
        panel_.allowsMultipleSelection = NO;
        panel_.resolvesAliases = YES;
        panel_.canCreateDirectories = NO;
        panel_.canChooseDirectories = mode == OverlayPickerMode::SequenceDirectory;
        panel_.canChooseFiles = mode == OverlayPickerMode::StillPng;
        panel_.title = mode == OverlayPickerMode::StillPng
            ? @"Import Overlay PNG" : @"Import Overlay PNG Sequence";
        panel_.prompt = @"Import";
        if (mode == OverlayPickerMode::StillPng)
        {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            panel_.allowedFileTypes = @[@"png"];
#pragma clang diagnostic pop
        }

        const std::shared_ptr<PickerSharedState> shared = shared_;
        NSOpenPanel* selectedPanel = panel_;
        auto completion = ^(NSModalResponse response) {
            OverlayPickerResult result;
            if (response == NSModalResponseOK && selectedPanel.URL)
            {
                result.state = OverlayPickerState::Selected;
                result.path = std::filesystem::path(selectedPanel.URL.fileSystemRepresentation);
            }
            else
            {
                result.state = OverlayPickerState::Cancelled;
            }
            std::lock_guard<std::mutex> lock(shared->mutex);
            if (shared->generation == generation &&
                shared->result.state == OverlayPickerState::Picking)
                shared->result = std::move(result);
        };

        NSView* parentView = (__bridge NSView*)nativeParent;
        if (parentView.window)
            [panel_ beginSheetModalForWindow:parentView.window completionHandler:completion];
        else
            [panel_ beginWithCompletionHandler:completion];
        return true;
    }

    OverlayPickerState state() const override
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        return shared_->result.state;
    }

    bool poll(OverlayPickerResult& out) override
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        const OverlayPickerState current = shared_->result.state;
        if (current != OverlayPickerState::Selected &&
            current != OverlayPickerState::Cancelled &&
            current != OverlayPickerState::Failed)
            return false;
        out = std::move(shared_->result);
        shared_->result = {};
        panel_ = nil;
        return true;
    }

    void cancel() override
    {
        if (panel_)
        {
            [panel_ cancel:nil];
        }
        std::lock_guard<std::mutex> lock(shared_->mutex);
        if (shared_->result.state == OverlayPickerState::Picking)
            shared_->result.state = OverlayPickerState::Cancelled;
    }

private:
    NSOpenPanel*                        panel_ = nil;
    std::shared_ptr<PickerSharedState> shared_;
};

} // namespace

bool decodeOverlayPng(const std::filesystem::path& path,
                      DecodedOverlayImage& out, std::string& error)
{
    out.width = 0;
    out.height = 0;
    out.rowBytes = 0;
    out.bgra8.clear();
    @autoreleasepool
    {
        NSURL* url = [NSURL fileURLWithPath:
            [NSString stringWithUTF8String:path.string().c_str()]];
        if (!url)
        {
            error = "Could not represent overlay PNG path";
            return false;
        }
        CGImageSourceRef source = CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr);
        if (!source || CGImageSourceGetCount(source) != 1)
        {
            if (source) CFRelease(source);
            error = "Overlay is not a single-frame PNG image";
            return false;
        }
        CFStringRef type = CGImageSourceGetType(source);
        if (!type || CFStringCompare(type, CFSTR("public.png"), 0) != kCFCompareEqualTo)
        {
            CFRelease(source);
            error = "Overlay file is not PNG data";
            return false;
        }

        std::size_t metadataWidth = 0;
        std::size_t metadataHeight = 0;
        CFDictionaryRef properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nullptr);
        if (properties)
        {
            CFNumberRef widthValue = static_cast<CFNumberRef>(
                CFDictionaryGetValue(properties, kCGImagePropertyPixelWidth));
            CFNumberRef heightValue = static_cast<CFNumberRef>(
                CFDictionaryGetValue(properties, kCGImagePropertyPixelHeight));
            if (widthValue) CFNumberGetValue(widthValue, kCFNumberSInt64Type, &metadataWidth);
            if (heightValue) CFNumberGetValue(heightValue, kCFNumberSInt64Type, &metadataHeight);
            CFRelease(properties);
        }
        const bool supportedRaster =
            (metadataWidth == 1920 && metadataHeight == 1080) ||
            (metadataWidth == 1080 && metadataHeight == 1920);
        if (!supportedRaster)
        {
            CFRelease(source);
            error = "Overlay must be exactly 1920x1080 or 1080x1920";
            return false;
        }
        CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
        CFRelease(source);
        if (!image)
        {
            error = "Could not decode overlay PNG";
            return false;
        }

        const std::size_t width = CGImageGetWidth(image);
        const std::size_t height = CGImageGetHeight(image);
        if (width == 0 || height == 0 || width > std::numeric_limits<std::uint32_t>::max() ||
            height > std::numeric_limits<std::uint32_t>::max() ||
            width > std::numeric_limits<std::size_t>::max() / 4U / height)
        {
            CGImageRelease(image);
            error = "Overlay PNG dimensions are invalid";
            return false;
        }

        out.width = static_cast<std::uint32_t>(width);
        out.height = static_cast<std::uint32_t>(height);
        out.rowBytes = width * 4U;
        out.bgra8.resize(out.rowBytes * height);

        CGColorSpaceRef colour = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGBitmapInfo info = static_cast<CGBitmapInfo>(
            static_cast<std::uint32_t>(kCGBitmapByteOrder32Little) |
            static_cast<std::uint32_t>(kCGImageAlphaPremultipliedFirst));
        CGContextRef context = CGBitmapContextCreate(out.bgra8.data(), width, height, 8,
                                                     out.rowBytes, colour, info);
        CGColorSpaceRelease(colour);
        if (!context)
        {
            CGImageRelease(image);
            out.width = 0;
            out.height = 0;
            out.rowBytes = 0;
            out.bgra8.clear();
            error = "Could not create overlay decode buffer";
            return false;
        }
        CGContextSetBlendMode(context, kCGBlendModeCopy);
        CGContextTranslateCTM(context, 0.0, static_cast<CGFloat>(height));
        CGContextScaleCTM(context, 1.0, -1.0);
        CGContextDrawImage(context, CGRectMake(0.0, 0.0, width, height), image);
        CGContextRelease(context);
        CGImageRelease(image);
        return true;
    }
}

bool replaceOverlayFileAtomically(const std::filesystem::path& source,
                                  const std::filesystem::path& destination,
                                  std::string& error)
{
    std::error_code ec;
    std::filesystem::rename(source, destination, ec);
    if (ec)
    {
        error = "Could not publish overlay manifest: " + ec.message();
        return false;
    }
    return true;
}

std::unique_ptr<OverlaySourcePicker> createOverlaySourcePicker()
{
    return std::make_unique<MacOverlaySourcePicker>();
}

} // namespace atemfx
