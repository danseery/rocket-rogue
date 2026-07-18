#pragma once

#include "platform/AppServices.h"
#include "platform/FrameLimitPolicy.h"
#include "render/FrameCapture.h"
#include "render/IVulkanRmlFrameContext.h"
#include "render/SceneAtlas.h"
#include "render/SceneComposer.h"

#include <SDL3/SDL.h>
#include <vk_mem_alloc.h>
#include <volk.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace rocket {

class VulkanGraphicsBackend final : public IGameRenderer, public IVulkanRmlFrameContext {
public:
    static constexpr std::uint32_t framesInFlight = 2;

    VulkanGraphicsBackend(
        IPlatformHost& host,
        ITextureSource& textures,
        SDL_Window* window,
        std::filesystem::path runtimeRoot,
        std::filesystem::path cacheDirectory);
    ~VulkanGraphicsBackend() override;

    VulkanGraphicsBackend(const VulkanGraphicsBackend&) = delete;
    VulkanGraphicsBackend& operator=(const VulkanGraphicsBackend&) = delete;

    bool initialize() override;
    void render(const RenderSnapshot& snapshot) override;
    GraphicsFrameStatus endFrameAndPresent() override;
    GraphicsFrameStatus frameStatus() const override;
    void shutdown() override;
    void setPreferences(const AppPreferences& preferences) override;
    RendererDiagnostics diagnostics() const override;
    std::string_view description() const override;

    VkDevice device() const override;
    VkPhysicalDevice physicalDevice() const override;
    VmaAllocator allocator() const override;
    const VkAllocationCallbacks* allocationCallbacks() const override;
    VkPipelineCache pipelineCache() const override;
    VkCommandBuffer activeCommandBuffer() const override;
    VkFormat swapchainFormat() const override;
    VkExtent2D swapchainExtent() const override;
    std::uint64_t frameSerial() const override;
    std::uint64_t completedFrameSerial() const override;
    void recordPipelineCreation() override;
    bool submitSynchronousUpload(const std::function<void(VkCommandBuffer)>& recorder) override;
    bool waitForRmlFrames() override;

    void requestSwapchainRebuild() noexcept;
    // Schedules one diagnostic readback of the next fully composed scene + UI
    // frame. The ordinary frame path remains allocation/copy free.
    bool requestFrameCapture();
    std::optional<FrameCapture> takeFrameCapture();
    std::string_view frameCaptureError() const noexcept;
    std::string_view deviceName() const noexcept;
    std::string_view lastError() const noexcept;
    double targetFramesPerSecond() const noexcept;
    void setSteamDeckRuntimeDetector(SteamDeckRuntimeDetector detector) noexcept;

private:
    struct FrameResources {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkQueryPool timestampQueries = VK_NULL_HANDLE;
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VmaAllocation vertexAllocation = VK_NULL_HANDLE;
        void* mappedVertices = nullptr;
        VkDeviceSize vertexCapacity = 0;
        VkBuffer instanceBuffer = VK_NULL_HANDLE;
        VmaAllocation instanceAllocation = VK_NULL_HANDLE;
        void* mappedInstances = nullptr;
        VkDeviceSize instanceCapacity = 0;
        VkBuffer miningTerrainVertexBuffer = VK_NULL_HANDLE;
        VmaAllocation miningTerrainVertexAllocation = VK_NULL_HANDLE;
        void* mappedMiningTerrainVertices = nullptr;
        VkDeviceSize miningTerrainVertexCapacity = 0;
        VkBuffer miningTerrainInstanceBuffer = VK_NULL_HANDLE;
        VmaAllocation miningTerrainInstanceAllocation = VK_NULL_HANDLE;
        void* mappedMiningTerrainInstances = nullptr;
        VkDeviceSize miningTerrainInstanceCapacity = 0;
        std::uint64_t miningTerrainRevision = 0;
        std::uint64_t submittedSerial = 0;
        bool submissionPending = false;
    };

    struct TextureResource {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        int width = 0;
        int height = 0;
        bool ready = false;
    };

    struct ScenePushConstants {
        float positionScale[2] {};
        float positionOffset[2] {};
        float effectColor[4] {};
        float effectParams[4] {};
        float effectSize[2] {};
        std::uint32_t effectMode = 0;
        std::uint32_t useTexture = 0;
    };

    static_assert(sizeof(ScenePushConstants) == 64U);

    struct FrameCaptureResources {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        void* mappedPixels = nullptr;
        VkDeviceSize byteCount = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        VkFormat format = VK_FORMAT_UNDEFINED;
    };

    bool initializeLoaderAndInstance();
    bool selectPhysicalDevice();
    bool createDeviceAndAllocator();
    bool createFrameResources();
    bool createDescriptorsAndSampler();
    bool createPipelineCache();
    bool createScenePipeline();
    bool createOrRecreateSwapchain();
    bool recreateSurface();
    bool createTexture(
        TextureResource& texture,
        const std::uint8_t* rgba,
        int width,
        int height,
        std::string_view debugName);
    bool loadSceneTextures();
    bool beginFrame(const ScenePacket& packet);
    void recordScene(const ScenePacket& packet);
    bool prepareFrameCapture();
    void recordFrameCapture(VkCommandBuffer commandBuffer);
    bool completeFrameCapture();
    void destroyFrameCaptureResources();
    bool ensureVertexCapacity(FrameResources& frame, VkDeviceSize bytes);
    bool ensureInstanceCapacity(FrameResources& frame, VkDeviceSize bytes);
    bool ensureMiningTerrainVertexCapacity(FrameResources& frame, VkDeviceSize bytes);
    bool ensureMiningTerrainInstanceCapacity(FrameResources& frame, VkDeviceSize bytes);
    void retireFrame(FrameResources& frame);
    void destroySwapchain();
    void destroyScenePipeline();
    void destroyTextures();
    void destroyFrameResources();
    void savePipelineCache();
    bool waitForAllFrames();
    void updateMemoryDiagnostics();
    void applyFrameLimitAfterPresent();
    void refreshFrameLimit();
    void setError(std::string message);
    bool check(VkResult result, std::string_view operation);

    IPlatformHost& host_;
    ITextureSource& textures_;
    SDL_Window* window_ = nullptr;
    std::filesystem::path runtimeRoot_;
    std::filesystem::path cacheDirectory_;
    std::filesystem::path pipelineCachePath_;
    SceneComposer composer_;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    std::uint32_t graphicsQueueFamily_ = UINT32_MAX;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR swapchainColorSpace_ = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkExtent2D swapchainExtent_ {};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    // A presentation wait can outlive the graphics submission fence. Key the
    // signal semaphore to the acquired image so reacquiring that image proves
    // its prior present wait has completed before the semaphore is reused.
    std::vector<VkSemaphore> swapchainRenderFinished_;

    VkDescriptorSetLayout sceneDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool sceneDescriptorPool_ = VK_NULL_HANDLE;
    VkSampler sceneSampler_ = VK_NULL_HANDLE;
    VkPipelineLayout scenePipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline scenePipeline_ = VK_NULL_HANDLE;
    VkPipeline sceneInstancePipeline_ = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
    // Resource zero is the solid-draw white fallback; generated atlas pages
    // occupy the remaining descriptors.
    std::array<TextureResource, kSceneAtlasPages.size() + 1U> sceneTextures_ {};
    std::array<FrameResources, framesInFlight> frames_ {};

    FrameResources* activeFrame_ = nullptr;
    std::uint32_t activeImageIndex_ = 0;
    std::uint32_t frameSlot_ = 0;
    std::uint64_t nextFrameSerial_ = 1;
    std::uint64_t completedFrameSerial_ = 0;
    GraphicsFrameStatus frameStatus_ = GraphicsFrameStatus::Skipped;
    RendererDiagnostics diagnostics_;
    PresentIntervalDeadline presentDeadline_;
    SteamDeckRuntimeDetector steamDeckDetector_;
    FrameLimitMode frameLimitMode_ = FrameLimitMode::PlatformDefault;
    double configuredRefreshRateHz_ = 0.0;
    std::string deviceName_;
    std::string lastError_;
    std::string frameCaptureError_;
    FrameCaptureResources frameCaptureResources_;
    std::optional<FrameCapture> completedFrameCapture_;
    bool validationEnabled_ = false;
    bool debugUtilsEnabled_ = false;
    bool surfaceRebuildRequested_ = false;
    bool swapchainRebuildRequested_ = true;
    bool renderingActive_ = false;
    bool swapchainCaptureSupported_ = false;
    bool frameCaptureRequested_ = false;
    bool frameCaptureRecorded_ = false;
    bool initialized_ = false;
};

} // namespace rocket
