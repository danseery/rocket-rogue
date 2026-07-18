#include "render/FrameCapture.h"

#include <limits>

namespace rocket {

bool canonicalizeFrameCapture(FrameCapture& capture, std::string& error)
{
    error.clear();
    if (capture.width == 0 || capture.height == 0) {
        error = "Captured frame dimensions must be non-zero.";
        return false;
    }
    const std::size_t width = capture.width;
    const std::size_t height = capture.height;
    if (width > std::numeric_limits<std::size_t>::max() / height
        || width * height > std::numeric_limits<std::size_t>::max() / 4U) {
        error = "Captured frame dimensions overflow the RGBA byte count.";
        return false;
    }
    const std::size_t expectedBytes = width * height * 4U;
    if (capture.pixels.size() != expectedBytes) {
        error = "Captured frame byte count does not match its dimensions.";
        return false;
    }
    if (capture.format == FrameCapturePixelFormat::Bgra8Unorm) {
        for (std::size_t offset = 0; offset < capture.pixels.size(); offset += 4U) {
            const std::uint8_t blue = capture.pixels[offset];
            capture.pixels[offset] = capture.pixels[offset + 2U];
            capture.pixels[offset + 2U] = blue;
        }
        capture.format = FrameCapturePixelFormat::Rgba8Unorm;
    }
    return true;
}

} // namespace rocket
