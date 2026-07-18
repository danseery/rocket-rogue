#pragma once

#include "game/IRmlRenderHost.h"

#include <cstdint>
#include <memory>
#include <span>

namespace rocket {

class IVulkanRmlFrameContext;

struct VulkanRmlShaderBytecode {
    std::span<const std::uint32_t> vertex;
    std::span<const std::uint32_t> fragment;
};

class VulkanRmlRenderHost final : public IRmlRenderHost {
public:
    VulkanRmlRenderHost(
        IVulkanRmlFrameContext& frameContext,
        const VulkanRmlShaderBytecode& shaders);
    ~VulkanRmlRenderHost() override;

    VulkanRmlRenderHost(const VulkanRmlRenderHost&) = delete;
    VulkanRmlRenderHost& operator=(const VulkanRmlRenderHost&) = delete;

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
