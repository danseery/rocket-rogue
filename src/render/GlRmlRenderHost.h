#pragma once

#include "game/IRmlRenderHost.h"

#include <memory>

namespace rocket {

// Web-only RmlUi host sharing the active WebGL2 context.
class WebGlRmlRenderHost final : public IRmlRenderHost {
public:
    WebGlRmlRenderHost();
    ~WebGlRmlRenderHost() override;

    WebGlRmlRenderHost(const WebGlRmlRenderHost&) = delete;
    WebGlRmlRenderHost& operator=(const WebGlRmlRenderHost&) = delete;

    bool initialize() override;
    Rml::RenderInterface& renderInterface() override;
    void setViewport(const RmlRenderViewport& viewport) override;
    void setRootClip(const RmlRenderClip& clip) override;
    bool beginFrame() override;
    void endFrame() override;
    UiDiagnostics diagnostics() const override;
    void shutdown() override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rocket
