#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rocket {

enum class FrameCapturePixelFormat {
    Rgba8Unorm,
    Bgra8Unorm,
};

// CPU-owned pixels copied from a completed graphics frame. Rows are stored
// top-to-bottom and tightly packed. Capture is an explicit diagnostic path;
// ordinary rendering never allocates or copies this data.
struct FrameCapture {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    FrameCapturePixelFormat format = FrameCapturePixelFormat::Rgba8Unorm;
    std::vector<std::uint8_t> pixels;
};

// Validates a capture and canonicalizes BGRA swapchain data to RGBA8 so image
// encoders and perceptual-diff tools see one backend-neutral byte layout.
bool canonicalizeFrameCapture(FrameCapture& capture, std::string& error);

} // namespace rocket
