#include <volk.h>

#include "render/VulkanRmlRenderHost.h"

#include "platform/AppServices.h"
#include "render/IVulkanRmlFrameContext.h"

#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/RenderInterface.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rocket {
namespace {

constexpr std::uint32_t kDescriptorSetsPerPool = 128;

bool vkSucceeded(VkResult result, const char* operation)
{
    if (result == VK_SUCCESS) {
        return true;
    }
    Rml::Log::Message(
        Rml::Log::LT_ERROR,
        "Vulkan RmlUi %s failed with VkResult %d.",
        operation,
        static_cast<int>(result));
    return false;
}

struct RmlPushConstants {
    float scale[2] = {};
    float offset[2] = {};
    float translation[2] = {};
    std::uint32_t hasTexture = 0;
};

static_assert(sizeof(RmlPushConstants) == 28);
static_assert(sizeof(int) == sizeof(std::uint32_t));

struct VulkanRmlGeometry {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkDeviceSize indexOffset = 0;
    std::uint32_t indexCount = 0;
    std::uint64_t lastUsedSerial = 0;
};

struct VulkanRmlTexture {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    std::uint64_t lastUsedSerial = 0;
};

struct RetiredGeometry {
    VulkanRmlGeometry* geometry = nullptr;
    std::uint64_t retireSerial = 0;
};

struct RetiredTexture {
    VulkanRmlTexture* texture = nullptr;
    std::uint64_t retireSerial = 0;
};

struct RetiredPipeline {
    VkPipeline pipeline = VK_NULL_HANDLE;
    std::uint64_t retireSerial = 0;
};

bool sameRect(const VkRect2D& lhs, const VkRect2D& rhs)
{
    return lhs.offset.x == rhs.offset.x
        && lhs.offset.y == rhs.offset.y
        && lhs.extent.width == rhs.extent.width
        && lhs.extent.height == rhs.extent.height;
}

} // namespace

class VulkanRmlRenderHost::Impl final : public Rml::RenderInterface {
public:
    Impl(
        IVulkanRmlFrameContext& frameContext,
        const VulkanRmlShaderBytecode& shaders)
        : frameContext_(frameContext),
          vertexShader_(shaders.vertex.begin(), shaders.vertex.end()),
          fragmentShader_(shaders.fragment.begin(), shaders.fragment.end())
    {
    }

    ~Impl() override
    {
        shutdown();
    }

    bool initialize()
    {
        if (initialized_) {
            return true;
        }

        device_ = frameContext_.device();
        allocator_ = frameContext_.allocator();
        allocationCallbacks_ = frameContext_.allocationCallbacks();
        if (device_ == VK_NULL_HANDLE
            || frameContext_.physicalDevice() == VK_NULL_HANDLE
            || allocator_ == VK_NULL_HANDLE
            || vertexShader_.empty()
            || fragmentShader_.empty()) {
            Rml::Log::Message(
                Rml::Log::LT_ERROR,
                "Vulkan RmlUi initialization is missing a device, allocator, physical device, or shader bytecode.");
            return false;
        }

        VkPhysicalDeviceProperties properties {};
        vkGetPhysicalDeviceProperties(frameContext_.physicalDevice(), &properties);
        if (properties.limits.maxPushConstantsSize < sizeof(RmlPushConstants)) {
            Rml::Log::Message(
                Rml::Log::LT_ERROR,
                "Vulkan RmlUi requires %zu push-constant bytes; device exposes %u.",
                sizeof(RmlPushConstants),
                properties.limits.maxPushConstantsSize);
            return false;
        }

        if (!createDescriptorSetLayout()
            || !createPipelineLayout()
            || !createSampler()
            || !createDescriptorPool()) {
            shutdown();
            return false;
        }

        const std::array<Rml::byte, 4> whitePixel {255, 255, 255, 255};
        whiteTexture_ = createTexture(whitePixel.data(), whitePixel.size(), {1, 1});
        if (!whiteTexture_) {
            shutdown();
            return false;
        }

        pipelineFormat_ = frameContext_.swapchainFormat();
        if (pipelineFormat_ != VK_FORMAT_UNDEFINED && !createPipeline(pipelineFormat_, pipeline_)) {
            shutdown();
            return false;
        }

        initialized_ = true;
        return true;
    }

    void shutdown()
    {
        if (device_ == VK_NULL_HANDLE) {
            return;
        }

        if (initialized_ && !frameContext_.waitForRmlFrames()) {
            Rml::Log::Message(
                Rml::Log::LT_WARNING,
                "Vulkan RmlUi could not confirm completion of all frame fences during shutdown.");
        }

        frameActive_ = false;
        commandBuffer_ = VK_NULL_HANDLE;

        for (VulkanRmlGeometry* geometry : liveGeometries_) {
            destroyGeometry(geometry);
        }
        liveGeometries_.clear();
        for (const RetiredGeometry& retired : retiredGeometries_) {
            destroyGeometry(retired.geometry);
        }
        retiredGeometries_.clear();

        for (VulkanRmlTexture* texture : liveTextures_) {
            destroyTexture(texture);
        }
        liveTextures_.clear();
        for (const RetiredTexture& retired : retiredTextures_) {
            destroyTexture(retired.texture);
        }
        retiredTextures_.clear();
        destroyTexture(whiteTexture_);
        whiteTexture_ = nullptr;

        if (pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, pipeline_, allocationCallbacks_);
            pipeline_ = VK_NULL_HANDLE;
        }
        for (const RetiredPipeline& retired : retiredPipelines_) {
            if (retired.pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device_, retired.pipeline, allocationCallbacks_);
            }
        }
        retiredPipelines_.clear();

        for (VkDescriptorPool descriptorPool : descriptorPools_) {
            vkDestroyDescriptorPool(device_, descriptorPool, allocationCallbacks_);
        }
        descriptorPools_.clear();
        if (sampler_ != VK_NULL_HANDLE) {
            vkDestroySampler(device_, sampler_, allocationCallbacks_);
            sampler_ = VK_NULL_HANDLE;
        }
        if (pipelineLayout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, pipelineLayout_, allocationCallbacks_);
            pipelineLayout_ = VK_NULL_HANDLE;
        }
        if (descriptorSetLayout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, allocationCallbacks_);
            descriptorSetLayout_ = VK_NULL_HANDLE;
        }

        pipelineFormat_ = VK_FORMAT_UNDEFINED;
        allocator_ = VK_NULL_HANDLE;
        device_ = VK_NULL_HANDLE;
        initialized_ = false;
    }

    void setViewport(const RmlRenderViewport& viewport)
    {
        logicalWidth_ = std::max(1, viewport.logicalWidth);
        logicalHeight_ = std::max(1, viewport.logicalHeight);
    }

    void setRootClip(const RmlRenderClip& clip)
    {
        rootClip_ = clip;
        if (frameActive_) {
            applyScissor();
        }
    }

    bool beginFrame()
    {
        if (!initialized_) {
            return false;
        }

        collectRetiredResources();
        commandBuffer_ = frameContext_.activeCommandBuffer();
        framebufferExtent_ = frameContext_.swapchainExtent();
        if (commandBuffer_ == VK_NULL_HANDLE
            || framebufferExtent_.width == 0
            || framebufferExtent_.height == 0) {
            commandBuffer_ = VK_NULL_HANDLE;
            return false;
        }

        const VkFormat format = frameContext_.swapchainFormat();
        if (format == VK_FORMAT_UNDEFINED) {
            commandBuffer_ = VK_NULL_HANDLE;
            return false;
        }
        if (pipeline_ == VK_NULL_HANDLE || format != pipelineFormat_) {
            VkPipeline replacement = VK_NULL_HANDLE;
            if (!createPipeline(format, replacement)) {
                commandBuffer_ = VK_NULL_HANDLE;
                return false;
            }
            if (pipeline_ != VK_NULL_HANDLE) {
                retiredPipelines_.push_back({pipeline_, frameContext_.frameSerial()});
            }
            pipeline_ = replacement;
            pipelineFormat_ = format;
        }

        vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        const VkViewport viewport {
            0.0F,
            0.0F,
            static_cast<float>(framebufferExtent_.width),
            static_cast<float>(framebufferExtent_.height),
            0.0F,
            1.0F,
        };
        vkCmdSetViewport(commandBuffer_, 0, 1, &viewport);

        frameActive_ = true;
        frameSerial_ = frameContext_.frameSerial();
        compiledGeometryThisFrame_ = compiledGeometryPending_;
        compiledGeometryPending_ = 0;
        renderedGeometryThisFrame_ = 0;
        boundGeometry_ = nullptr;
        boundTexture_ = nullptr;
        scissorValid_ = false;
        applyScissor();
        return true;
    }

    void endFrame()
    {
        frameActive_ = false;
        commandBuffer_ = VK_NULL_HANDLE;
        boundGeometry_ = nullptr;
        boundTexture_ = nullptr;
    }

    Rml::CompiledGeometryHandle CompileGeometry(
        Rml::Span<const Rml::Vertex> vertices,
        Rml::Span<const int> indices) override
    {
        if (frameActive_) {
            ++compiledGeometryThisFrame_;
        } else {
            ++compiledGeometryPending_;
        }

        auto geometry = std::make_unique<VulkanRmlGeometry>();
        geometry->indexCount = static_cast<std::uint32_t>(indices.size());
        if (vertices.empty() || indices.empty()) {
            VulkanRmlGeometry* handle = geometry.release();
            liveGeometries_.insert(handle);
            return reinterpret_cast<Rml::CompiledGeometryHandle>(handle);
        }

        const VkDeviceSize vertexBytes = vertices.size() * sizeof(Rml::Vertex);
        geometry->indexOffset = (vertexBytes + 3) & ~VkDeviceSize(3);
        const VkDeviceSize indexBytes = indices.size() * sizeof(int);

        VkBufferCreateInfo bufferInfo {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = geometry->indexOffset + indexBytes;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocationInfo {};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
            | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo allocationResult {};
        if (!vkSucceeded(
                vmaCreateBuffer(
                    allocator_,
                    &bufferInfo,
                    &allocationInfo,
                    &geometry->buffer,
                    &geometry->allocation,
                    &allocationResult),
                "geometry buffer allocation")) {
            return 0;
        }

        if (!allocationResult.pMappedData) {
            Rml::Log::Message(Rml::Log::LT_ERROR, "Vulkan RmlUi geometry allocation was not mapped.");
            vmaDestroyBuffer(allocator_, geometry->buffer, geometry->allocation);
            return 0;
        }
        auto* destination = static_cast<std::byte*>(allocationResult.pMappedData);
        std::memcpy(destination, vertices.data(), static_cast<std::size_t>(vertexBytes));
        std::memcpy(destination + geometry->indexOffset, indices.data(), static_cast<std::size_t>(indexBytes));
        if (!vkSucceeded(
                vmaFlushAllocation(
                    allocator_,
                    geometry->allocation,
                    0,
                    geometry->indexOffset + indexBytes),
                "geometry buffer flush")) {
            vmaDestroyBuffer(allocator_, geometry->buffer, geometry->allocation);
            return 0;
        }

        VulkanRmlGeometry* handle = geometry.release();
        liveGeometries_.insert(handle);
        return reinterpret_cast<Rml::CompiledGeometryHandle>(handle);
    }

    void RenderGeometry(
        Rml::CompiledGeometryHandle handle,
        Rml::Vector2f translation,
        Rml::TextureHandle textureHandle) override
    {
        auto* geometry = reinterpret_cast<VulkanRmlGeometry*>(handle);
        if (!frameActive_ || scissorEmpty_ || !geometry
            || geometry->indexCount == 0 || geometry->buffer == VK_NULL_HANDLE) {
            return;
        }

        auto* texture = textureHandle != 0
            ? reinterpret_cast<VulkanRmlTexture*>(textureHandle)
            : whiteTexture_;
        if (!texture || texture->descriptorSet == VK_NULL_HANDLE) {
            return;
        }

        ++renderedGeometryThisFrame_;
        geometry->lastUsedSerial = frameSerial_;
        texture->lastUsedSerial = frameSerial_;

        if (boundGeometry_ != geometry) {
            const VkDeviceSize vertexOffset = 0;
            vkCmdBindVertexBuffers(commandBuffer_, 0, 1, &geometry->buffer, &vertexOffset);
            vkCmdBindIndexBuffer(
                commandBuffer_,
                geometry->buffer,
                geometry->indexOffset,
                VK_INDEX_TYPE_UINT32);
            boundGeometry_ = geometry;
        }
        if (boundTexture_ != texture) {
            vkCmdBindDescriptorSets(
                commandBuffer_,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelineLayout_,
                0,
                1,
                &texture->descriptorSet,
                0,
                nullptr);
            boundTexture_ = texture;
        }

        RmlPushConstants pushConstants;
        pushConstants.scale[0] = 2.0F / static_cast<float>(logicalWidth_);
        pushConstants.scale[1] = 2.0F / static_cast<float>(logicalHeight_);
        pushConstants.offset[0] = -1.0F;
        pushConstants.offset[1] = -1.0F;
        pushConstants.translation[0] = translation.x;
        pushConstants.translation[1] = translation.y;
        pushConstants.hasTexture = textureHandle != 0 ? 1U : 0U;
        vkCmdPushConstants(
            commandBuffer_,
            pipelineLayout_,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(pushConstants),
            &pushConstants);
        vkCmdDrawIndexed(commandBuffer_, geometry->indexCount, 1, 0, 0, 0);
    }

    void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override
    {
        auto* geometry = reinterpret_cast<VulkanRmlGeometry*>(handle);
        if (!geometry) {
            return;
        }
        liveGeometries_.erase(geometry);
        retiredGeometries_.push_back({
            geometry,
            std::max(geometry->lastUsedSerial, frameContext_.frameSerial()),
        });
        if (boundGeometry_ == geometry) {
            boundGeometry_ = nullptr;
        }
    }

    Rml::TextureHandle LoadTexture(
        Rml::Vector2i& textureDimensions,
        const Rml::String& source) override
    {
        (void)textureDimensions;
        Rml::Log::Message(
            Rml::Log::LT_WARNING,
            "RmlUi texture file loading is not used by OREBIT: %s",
            source.c_str());
        return 0;
    }

    Rml::TextureHandle GenerateTexture(
        Rml::Span<const Rml::byte> sourceData,
        Rml::Vector2i sourceDimensions) override
    {
        VulkanRmlTexture* texture = createTexture(
            sourceData.data(),
            sourceData.size(),
            sourceDimensions);
        if (!texture) {
            return 0;
        }
        liveTextures_.insert(texture);
        return reinterpret_cast<Rml::TextureHandle>(texture);
    }

    void ReleaseTexture(Rml::TextureHandle textureHandle) override
    {
        auto* texture = reinterpret_cast<VulkanRmlTexture*>(textureHandle);
        if (!texture) {
            return;
        }
        liveTextures_.erase(texture);
        retiredTextures_.push_back({
            texture,
            std::max(texture->lastUsedSerial, frameContext_.frameSerial()),
        });
        if (boundTexture_ == texture) {
            boundTexture_ = nullptr;
        }
    }

    void EnableScissorRegion(bool enable) override
    {
        scissorEnabled_ = enable;
        if (frameActive_) {
            applyScissor();
        }
    }

    void SetScissorRegion(Rml::Rectanglei region) override
    {
        scissorRegion_ = region;
        if (frameActive_ && scissorEnabled_) {
            applyScissor();
        }
    }

    UiDiagnostics diagnostics() const
    {
        UiDiagnostics diagnostics;
        diagnostics.compiledGeometry = compiledGeometryThisFrame_;
        diagnostics.renderedGeometry = renderedGeometryThisFrame_;
        return diagnostics;
    }

private:
    bool createDescriptorSetLayout()
    {
        const VkDescriptorSetLayoutBinding binding {
            0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            1,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            nullptr,
        };
        VkDescriptorSetLayoutCreateInfo createInfo {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        createInfo.bindingCount = 1;
        createInfo.pBindings = &binding;
        return vkSucceeded(
            vkCreateDescriptorSetLayout(
                device_,
                &createInfo,
                allocationCallbacks_,
                &descriptorSetLayout_),
            "descriptor set layout creation");
    }

    bool createPipelineLayout()
    {
        const VkPushConstantRange pushConstantRange {
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(RmlPushConstants),
        };
        VkPipelineLayoutCreateInfo createInfo {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        createInfo.setLayoutCount = 1;
        createInfo.pSetLayouts = &descriptorSetLayout_;
        createInfo.pushConstantRangeCount = 1;
        createInfo.pPushConstantRanges = &pushConstantRange;
        return vkSucceeded(
            vkCreatePipelineLayout(
                device_,
                &createInfo,
                allocationCallbacks_,
                &pipelineLayout_),
            "pipeline layout creation");
    }

    bool createSampler()
    {
        VkSamplerCreateInfo createInfo {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        createInfo.magFilter = VK_FILTER_LINEAR;
        createInfo.minFilter = VK_FILTER_LINEAR;
        createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        createInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        createInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        createInfo.minLod = 0.0F;
        createInfo.maxLod = 0.0F;
        return vkSucceeded(
            vkCreateSampler(device_, &createInfo, allocationCallbacks_, &sampler_),
            "sampler creation");
    }

    bool createDescriptorPool()
    {
        const VkDescriptorPoolSize poolSize {
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            kDescriptorSetsPerPool,
        };
        VkDescriptorPoolCreateInfo createInfo {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        createInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        createInfo.maxSets = kDescriptorSetsPerPool;
        createInfo.poolSizeCount = 1;
        createInfo.pPoolSizes = &poolSize;

        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        if (!vkSucceeded(
                vkCreateDescriptorPool(
                    device_,
                    &createInfo,
                    allocationCallbacks_,
                    &descriptorPool),
                "descriptor pool creation")) {
            return false;
        }
        descriptorPools_.push_back(descriptorPool);
        return true;
    }

    bool createShaderModule(
        const std::vector<std::uint32_t>& bytecode,
        VkShaderModule& shaderModule) const
    {
        VkShaderModuleCreateInfo createInfo {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize = bytecode.size() * sizeof(std::uint32_t);
        createInfo.pCode = bytecode.data();
        return vkSucceeded(
            vkCreateShaderModule(
                device_,
                &createInfo,
                allocationCallbacks_,
                &shaderModule),
            "shader module creation");
    }

    bool createPipeline(VkFormat format, VkPipeline& destination)
    {
        VkShaderModule vertexModule = VK_NULL_HANDLE;
        VkShaderModule fragmentModule = VK_NULL_HANDLE;
        if (!createShaderModule(vertexShader_, vertexModule)
            || !createShaderModule(fragmentShader_, fragmentModule)) {
            if (vertexModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device_, vertexModule, allocationCallbacks_);
            }
            if (fragmentModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device_, fragmentModule, allocationCallbacks_);
            }
            return false;
        }

        const std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages {{
            {
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                nullptr,
                0,
                VK_SHADER_STAGE_VERTEX_BIT,
                vertexModule,
                "main",
                nullptr,
            },
            {
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                nullptr,
                0,
                VK_SHADER_STAGE_FRAGMENT_BIT,
                fragmentModule,
                "main",
                nullptr,
            },
        }};

        const VkVertexInputBindingDescription vertexBinding {
            0,
            sizeof(Rml::Vertex),
            VK_VERTEX_INPUT_RATE_VERTEX,
        };
        const std::array<VkVertexInputAttributeDescription, 3> vertexAttributes {{
            {0, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<std::uint32_t>(offsetof(Rml::Vertex, position))},
            {1, 0, VK_FORMAT_R8G8B8A8_UNORM, static_cast<std::uint32_t>(offsetof(Rml::Vertex, colour))},
            {2, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<std::uint32_t>(offsetof(Rml::Vertex, tex_coord))},
        }};
        VkPipelineVertexInputStateCreateInfo vertexInput {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &vertexBinding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(vertexAttributes.size());
        vertexInput.pVertexAttributeDescriptions = vertexAttributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterization {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterization.polygonMode = VK_POLYGON_MODE_FILL;
        rasterization.cullMode = VK_CULL_MODE_NONE;
        rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterization.lineWidth = 1.0F;

        VkPipelineMultisampleStateCreateInfo multisample {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blendAttachment {};
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT
            | VK_COLOR_COMPONENT_G_BIT
            | VK_COLOR_COMPONENT_B_BIT
            | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo colorBlend {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &blendAttachment;

        const std::array<VkDynamicState, 2> dynamicStates {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        VkPipelineDynamicStateCreateInfo dynamicState {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineRenderingCreateInfo renderingInfo {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &format;

        VkGraphicsPipelineCreateInfo createInfo {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        createInfo.pNext = &renderingInfo;
        createInfo.stageCount = static_cast<std::uint32_t>(shaderStages.size());
        createInfo.pStages = shaderStages.data();
        createInfo.pVertexInputState = &vertexInput;
        createInfo.pInputAssemblyState = &inputAssembly;
        createInfo.pViewportState = &viewportState;
        createInfo.pRasterizationState = &rasterization;
        createInfo.pMultisampleState = &multisample;
        createInfo.pColorBlendState = &colorBlend;
        createInfo.pDynamicState = &dynamicState;
        createInfo.layout = pipelineLayout_;
        createInfo.renderPass = VK_NULL_HANDLE;

        const bool success = vkSucceeded(
            vkCreateGraphicsPipelines(
                device_,
                frameContext_.pipelineCache(),
                1,
                &createInfo,
                allocationCallbacks_,
                &destination),
            "graphics pipeline creation");
        vkDestroyShaderModule(device_, fragmentModule, allocationCallbacks_);
        vkDestroyShaderModule(device_, vertexModule, allocationCallbacks_);
        if (success) frameContext_.recordPipelineCreation();
        return success;
    }

    bool allocateTextureDescriptor(VulkanRmlTexture& texture)
    {
        for (VkDescriptorPool descriptorPool : descriptorPools_) {
            VkDescriptorSetAllocateInfo allocateInfo {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocateInfo.descriptorPool = descriptorPool;
            allocateInfo.descriptorSetCount = 1;
            allocateInfo.pSetLayouts = &descriptorSetLayout_;
            const VkResult result = vkAllocateDescriptorSets(device_, &allocateInfo, &texture.descriptorSet);
            if (result == VK_SUCCESS) {
                texture.descriptorPool = descriptorPool;
                return true;
            }
            if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL) {
                return vkSucceeded(result, "descriptor set allocation");
            }
        }

        if (!createDescriptorPool()) {
            return false;
        }
        VkDescriptorSetAllocateInfo allocateInfo {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocateInfo.descriptorPool = descriptorPools_.back();
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &descriptorSetLayout_;
        if (!vkSucceeded(
                vkAllocateDescriptorSets(device_, &allocateInfo, &texture.descriptorSet),
                "descriptor set allocation")) {
            return false;
        }
        texture.descriptorPool = descriptorPools_.back();
        return true;
    }

    VulkanRmlTexture* createTexture(
        const Rml::byte* sourceData,
        std::size_t sourceSize,
        Rml::Vector2i dimensions)
    {
        if (!sourceData || dimensions.x <= 0 || dimensions.y <= 0) {
            Rml::Log::Message(Rml::Log::LT_ERROR, "Vulkan RmlUi received invalid texture data or dimensions.");
            return nullptr;
        }
        const std::uint64_t expectedSize = static_cast<std::uint64_t>(dimensions.x)
            * static_cast<std::uint64_t>(dimensions.y) * 4U;
        if (expectedSize != sourceSize) {
            Rml::Log::Message(
                Rml::Log::LT_ERROR,
                "Vulkan RmlUi texture byte count does not match its dimensions.");
            return nullptr;
        }

        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VmaAllocation stagingAllocation = VK_NULL_HANDLE;
        VkBufferCreateInfo stagingBufferInfo {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        stagingBufferInfo.size = sourceSize;
        stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo stagingAllocationInfo {};
        stagingAllocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
        stagingAllocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
            | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo stagingResult {};
        if (!vkSucceeded(
                vmaCreateBuffer(
                    allocator_,
                    &stagingBufferInfo,
                    &stagingAllocationInfo,
                    &stagingBuffer,
                    &stagingAllocation,
                    &stagingResult),
                "texture staging allocation")) {
            return nullptr;
        }
        if (!stagingResult.pMappedData) {
            Rml::Log::Message(Rml::Log::LT_ERROR, "Vulkan RmlUi staging allocation was not mapped.");
            vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);
            return nullptr;
        }
        std::memcpy(stagingResult.pMappedData, sourceData, sourceSize);
        if (!vkSucceeded(
                vmaFlushAllocation(allocator_, stagingAllocation, 0, sourceSize),
                "texture staging flush")) {
            vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);
            return nullptr;
        }

        auto texture = std::make_unique<VulkanRmlTexture>();
        VkImageCreateInfo imageInfo {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.extent = {
            static_cast<std::uint32_t>(dimensions.x),
            static_cast<std::uint32_t>(dimensions.y),
            1,
        };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VmaAllocationCreateInfo imageAllocationInfo {};
        imageAllocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        if (!vkSucceeded(
                vmaCreateImage(
                    allocator_,
                    &imageInfo,
                    &imageAllocationInfo,
                    &texture->image,
                    &texture->allocation,
                    nullptr),
                "texture image allocation")) {
            vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);
            return nullptr;
        }

        const VkImage image = texture->image;
        const VkExtent3D imageExtent = imageInfo.extent;
        const bool uploadSucceeded = frameContext_.submitSynchronousUpload(
            [stagingBuffer, image, imageExtent](VkCommandBuffer commandBuffer) {
                VkImageMemoryBarrier2 toTransfer {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
                toTransfer.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
                toTransfer.srcAccessMask = VK_ACCESS_2_NONE;
                toTransfer.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                toTransfer.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toTransfer.image = image;
                toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                toTransfer.subresourceRange.levelCount = 1;
                toTransfer.subresourceRange.layerCount = 1;
                VkDependencyInfo toTransferDependency {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                toTransferDependency.imageMemoryBarrierCount = 1;
                toTransferDependency.pImageMemoryBarriers = &toTransfer;
                vkCmdPipelineBarrier2(commandBuffer, &toTransferDependency);

                VkBufferImageCopy copyRegion {};
                copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copyRegion.imageSubresource.layerCount = 1;
                copyRegion.imageExtent = imageExtent;
                vkCmdCopyBufferToImage(
                    commandBuffer,
                    stagingBuffer,
                    image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &copyRegion);

                VkImageMemoryBarrier2 toShaderRead {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
                toShaderRead.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                toShaderRead.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                toShaderRead.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                toShaderRead.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
                toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toShaderRead.image = image;
                toShaderRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                toShaderRead.subresourceRange.levelCount = 1;
                toShaderRead.subresourceRange.layerCount = 1;
                VkDependencyInfo toShaderReadDependency {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                toShaderReadDependency.imageMemoryBarrierCount = 1;
                toShaderReadDependency.pImageMemoryBarriers = &toShaderRead;
                vkCmdPipelineBarrier2(commandBuffer, &toShaderReadDependency);
            });
        vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);
        if (!uploadSucceeded) {
            vmaDestroyImage(allocator_, texture->image, texture->allocation);
            return nullptr;
        }

        VkImageViewCreateInfo viewInfo {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = texture->image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (!vkSucceeded(
                vkCreateImageView(
                    device_,
                    &viewInfo,
                    allocationCallbacks_,
                    &texture->view),
                "texture view creation")
            || !allocateTextureDescriptor(*texture)) {
            if (texture->view != VK_NULL_HANDLE) {
                vkDestroyImageView(device_, texture->view, allocationCallbacks_);
            }
            vmaDestroyImage(allocator_, texture->image, texture->allocation);
            return nullptr;
        }

        const VkDescriptorImageInfo descriptorImageInfo {
            sampler_,
            texture->view,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        VkWriteDescriptorSet write {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = texture->descriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &descriptorImageInfo;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
        return texture.release();
    }

    void destroyGeometry(VulkanRmlGeometry* geometry)
    {
        if (!geometry) {
            return;
        }
        if (geometry->buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator_, geometry->buffer, geometry->allocation);
        }
        delete geometry;
    }

    void destroyTexture(VulkanRmlTexture* texture)
    {
        if (!texture) {
            return;
        }
        if (texture->descriptorSet != VK_NULL_HANDLE
            && texture->descriptorPool != VK_NULL_HANDLE) {
            const VkResult result = vkFreeDescriptorSets(
                device_,
                texture->descriptorPool,
                1,
                &texture->descriptorSet);
            if (result != VK_SUCCESS) {
                Rml::Log::Message(
                    Rml::Log::LT_WARNING,
                    "Vulkan RmlUi descriptor release returned VkResult %d.",
                    static_cast<int>(result));
            }
        }
        if (texture->view != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, texture->view, allocationCallbacks_);
        }
        if (texture->image != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator_, texture->image, texture->allocation);
        }
        delete texture;
    }

    void collectRetiredResources()
    {
        const std::uint64_t completedSerial = frameContext_.completedFrameSerial();
        retiredGeometries_.erase(
            std::remove_if(
                retiredGeometries_.begin(),
                retiredGeometries_.end(),
                [&](const RetiredGeometry& retired) {
                    if (retired.retireSerial > completedSerial) {
                        return false;
                    }
                    destroyGeometry(retired.geometry);
                    return true;
                }),
            retiredGeometries_.end());
        retiredTextures_.erase(
            std::remove_if(
                retiredTextures_.begin(),
                retiredTextures_.end(),
                [&](const RetiredTexture& retired) {
                    if (retired.retireSerial > completedSerial) {
                        return false;
                    }
                    destroyTexture(retired.texture);
                    return true;
                }),
            retiredTextures_.end());
        retiredPipelines_.erase(
            std::remove_if(
                retiredPipelines_.begin(),
                retiredPipelines_.end(),
                [&](const RetiredPipeline& retired) {
                    if (retired.retireSerial > completedSerial) {
                        return false;
                    }
                    vkDestroyPipeline(device_, retired.pipeline, allocationCallbacks_);
                    return true;
                }),
            retiredPipelines_.end());
    }

    void applyScissor()
    {
        if (!frameActive_ || commandBuffer_ == VK_NULL_HANDLE) {
            return;
        }

        RmlRenderClip clip {
            0,
            0,
            logicalWidth_,
            logicalHeight_,
        };
        if (scissorEnabled_ && scissorRegion_.Valid()) {
            clip = {
                scissorRegion_.Left(),
                scissorRegion_.Top(),
                scissorRegion_.Right(),
                scissorRegion_.Bottom(),
            };
        }
        if (rootClip_.valid()) {
            clip.left = std::max(clip.left, rootClip_.left);
            clip.top = std::max(clip.top, rootClip_.top);
            clip.right = std::min(clip.right, rootClip_.right);
            clip.bottom = std::min(clip.bottom, rootClip_.bottom);
        }
        clip.right = std::max(clip.left, clip.right);
        clip.bottom = std::max(clip.top, clip.bottom);
        scissorEmpty_ = clip.right == clip.left || clip.bottom == clip.top;

        const double scaleX = static_cast<double>(framebufferExtent_.width)
            / static_cast<double>(logicalWidth_);
        const double scaleY = static_cast<double>(framebufferExtent_.height)
            / static_cast<double>(logicalHeight_);
        const int framebufferWidth = static_cast<int>(std::min<std::uint32_t>(
            framebufferExtent_.width,
            static_cast<std::uint32_t>(std::numeric_limits<int>::max())));
        const int framebufferHeight = static_cast<int>(std::min<std::uint32_t>(
            framebufferExtent_.height,
            static_cast<std::uint32_t>(std::numeric_limits<int>::max())));
        const int left = std::clamp(
            static_cast<int>(std::floor(static_cast<double>(clip.left) * scaleX)),
            0,
            framebufferWidth);
        const int top = std::clamp(
            static_cast<int>(std::floor(static_cast<double>(clip.top) * scaleY)),
            0,
            framebufferHeight);
        const int right = std::clamp(
            static_cast<int>(std::ceil(static_cast<double>(clip.right) * scaleX)),
            left,
            framebufferWidth);
        const int bottom = std::clamp(
            static_cast<int>(std::ceil(static_cast<double>(clip.bottom) * scaleY)),
            top,
            framebufferHeight);
        const VkRect2D scissor = scissorEmpty_
            ? VkRect2D {{0, 0}, {1, 1}}
            : VkRect2D {
                {left, top},
                {
                    static_cast<std::uint32_t>(right - left),
                    static_cast<std::uint32_t>(bottom - top),
                },
            };
        if (!scissorValid_ || !sameRect(scissor_, scissor)) {
            vkCmdSetScissor(commandBuffer_, 0, 1, &scissor);
            scissor_ = scissor;
            scissorValid_ = true;
        }
    }

    IVulkanRmlFrameContext& frameContext_;
    std::vector<std::uint32_t> vertexShader_;
    std::vector<std::uint32_t> fragmentShader_;
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    const VkAllocationCallbacks* allocationCallbacks_ = nullptr;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> descriptorPools_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkFormat pipelineFormat_ = VK_FORMAT_UNDEFINED;
    VulkanRmlTexture* whiteTexture_ = nullptr;
    std::unordered_set<VulkanRmlGeometry*> liveGeometries_;
    std::unordered_set<VulkanRmlTexture*> liveTextures_;
    std::vector<RetiredGeometry> retiredGeometries_;
    std::vector<RetiredTexture> retiredTextures_;
    std::vector<RetiredPipeline> retiredPipelines_;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkExtent2D framebufferExtent_ {};
    RmlRenderClip rootClip_;
    Rml::Rectanglei scissorRegion_ = Rml::Rectanglei::MakeInvalid();
    VkRect2D scissor_ {};
    VulkanRmlGeometry* boundGeometry_ = nullptr;
    VulkanRmlTexture* boundTexture_ = nullptr;
    std::uint64_t frameSerial_ = 0;
    int logicalWidth_ = 1;
    int logicalHeight_ = 1;
    int compiledGeometryPending_ = 0;
    int compiledGeometryThisFrame_ = 0;
    int renderedGeometryThisFrame_ = 0;
    bool initialized_ = false;
    bool frameActive_ = false;
    bool scissorEnabled_ = false;
    bool scissorValid_ = false;
    bool scissorEmpty_ = false;
};

VulkanRmlRenderHost::VulkanRmlRenderHost(
    IVulkanRmlFrameContext& frameContext,
    const VulkanRmlShaderBytecode& shaders)
    : impl_(std::make_unique<Impl>(frameContext, shaders))
{
}

VulkanRmlRenderHost::~VulkanRmlRenderHost() = default;

bool VulkanRmlRenderHost::initialize()
{
    return impl_->initialize();
}

Rml::RenderInterface& VulkanRmlRenderHost::renderInterface()
{
    return *impl_;
}

void VulkanRmlRenderHost::setViewport(const RmlRenderViewport& viewport)
{
    impl_->setViewport(viewport);
}

void VulkanRmlRenderHost::setRootClip(const RmlRenderClip& clip)
{
    impl_->setRootClip(clip);
}

bool VulkanRmlRenderHost::beginFrame()
{
    return impl_->beginFrame();
}

void VulkanRmlRenderHost::endFrame()
{
    impl_->endFrame();
}

UiDiagnostics VulkanRmlRenderHost::diagnostics() const
{
    return impl_->diagnostics();
}

void VulkanRmlRenderHost::shutdown()
{
    impl_->shutdown();
}

} // namespace rocket
