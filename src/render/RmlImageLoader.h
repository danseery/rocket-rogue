#pragma once
#include "lodepng.h"
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/RenderInterface.h>
#include <vector>

namespace rocket {
inline Rml::TextureHandle loadRmlImage(Rml::RenderInterface &renderer, Rml::Vector2i &dimensions,
                                       const Rml::String &source) {
    std::vector<unsigned char> rgba;
    unsigned width = 0, height = 0;
    const unsigned error = lodepng::decode(rgba, width, height, source);
    if (error || !width || !height || width > 8192 || height > 8192) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "Cannot load UI portrait %s: %s", source.c_str(),
                          lodepng_error_text(error));
        return 0;
    }
    // RmlUi renderers blend premultiplied alpha, including their generated font textures.
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        for (std::size_t channel = 0; channel < 3; ++channel)
            rgba[i + channel] = static_cast<unsigned char>(
                (static_cast<unsigned>(rgba[i + channel]) * rgba[i + 3] + 127) / 255);
    }
    dimensions = {static_cast<int>(width), static_cast<int>(height)};
    return renderer.GenerateTexture(Rml::Span<const Rml::byte>(rgba.data(), rgba.size()), dimensions);
}
} // namespace rocket
