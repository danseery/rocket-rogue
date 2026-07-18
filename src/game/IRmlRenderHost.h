#pragma once

namespace Rml {
class RenderInterface;
}

namespace rocket {

struct UiDiagnostics;

struct RmlRenderViewport {
    int logicalWidth = 1;
    int logicalHeight = 1;
    int drawableWidth = 1;
    int drawableHeight = 1;
};

struct RmlRenderClip {
    int left = 0;
    int top = 0;
    int right = -1;
    int bottom = -1;

    constexpr bool valid() const
    {
        return right >= left && bottom >= top;
    }
};

// Owns only the rendering resources used by RmlUi. Presentation and the
// lifetime of the enclosing graphics frame belong to the platform graphics
// backend, allowing a Vulkan implementation to record UI work into the same
// active command buffer as the scene.
class IRmlRenderHost {
public:
    virtual ~IRmlRenderHost() = default;

    virtual bool initialize() = 0;
    virtual Rml::RenderInterface& renderInterface() = 0;
    virtual void setViewport(const RmlRenderViewport& viewport) = 0;
    virtual void setRootClip(const RmlRenderClip& clip) = 0;
    virtual bool beginFrame() = 0;
    virtual void endFrame() = 0;
    virtual UiDiagnostics diagnostics() const = 0;
    virtual void shutdown() = 0;
};

} // namespace rocket
