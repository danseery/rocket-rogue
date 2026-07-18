#include "render/FrameCapture.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

int main()
{
    using namespace rocket;

    FrameCapture rgba {
        2,
        1,
        FrameCapturePixelFormat::Rgba8Unorm,
        {1, 2, 3, 4, 5, 6, 7, 8},
    };
    std::string error;
    assert(canonicalizeFrameCapture(rgba, error));
    assert(error.empty());
    assert((rgba.pixels == std::vector<std::uint8_t> {1, 2, 3, 4, 5, 6, 7, 8}));

    FrameCapture bgra {
        2,
        1,
        FrameCapturePixelFormat::Bgra8Unorm,
        {3, 2, 1, 4, 7, 6, 5, 8},
    };
    assert(canonicalizeFrameCapture(bgra, error));
    assert(bgra.format == FrameCapturePixelFormat::Rgba8Unorm);
    assert((bgra.pixels == std::vector<std::uint8_t> {1, 2, 3, 4, 5, 6, 7, 8}));

    FrameCapture empty;
    assert(!canonicalizeFrameCapture(empty, error));
    assert(!error.empty());

    FrameCapture truncated {
        1,
        1,
        FrameCapturePixelFormat::Rgba8Unorm,
        {1, 2, 3},
    };
    assert(!canonicalizeFrameCapture(truncated, error));
    assert(!error.empty());

    return 0;
}
