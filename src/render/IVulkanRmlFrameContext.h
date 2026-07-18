#pragma once

#include <volk.h>
#ifndef VMA_STATIC_VULKAN_FUNCTIONS
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#endif
#ifndef VMA_DYNAMIC_VULKAN_FUNCTIONS
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#endif
#include <vk_mem_alloc.h>

#include <cstdint>
#include <functional>

namespace rocket {

// Narrow bridge between the Vulkan graphics backend and RmlUi. The graphics
// backend owns every device, frame, submission, and presentation object. The
// Rml host only allocates resources through the supplied allocator and records
// commands into the currently active dynamic-rendering command buffer.
class IVulkanRmlFrameContext {
public:
    virtual ~IVulkanRmlFrameContext() = default;

    virtual VkDevice device() const = 0;
    virtual VkPhysicalDevice physicalDevice() const = 0;
    virtual VmaAllocator allocator() const = 0;
    virtual const VkAllocationCallbacks* allocationCallbacks() const = 0;
    virtual VkPipelineCache pipelineCache() const = 0;

    // Returns VK_NULL_HANDLE unless a frame is recording and dynamic rendering
    // for the swapchain color attachment is active.
    virtual VkCommandBuffer activeCommandBuffer() const = 0;
    virtual VkFormat swapchainFormat() const = 0;
    virtual VkExtent2D swapchainExtent() const = 0;

    // frameSerial identifies the current/submitted frame. completedFrameSerial
    // is the greatest serial whose fence has completed. Both are monotonic.
    virtual std::uint64_t frameSerial() const = 0;
    virtual std::uint64_t completedFrameSerial() const = 0;

    // Includes RmlUi pipeline creation in the graphics backend's per-frame
    // diagnostics so timed captures cannot silently miss UI pipeline work.
    virtual void recordPipelineCreation() = 0;

    // Records into a separate one-shot command buffer outside rendering,
    // submits it, and returns only after the upload has completed.
    virtual bool submitSynchronousUpload(
        const std::function<void(VkCommandBuffer)>& recorder) = 0;

    // Waits only the backend-owned frame fences that can reference RmlUi
    // resources. Called during RmlUi shutdown before those resources are
    // destroyed; it is never part of the normal frame loop.
    virtual bool waitForRmlFrames() = 0;
};

} // namespace rocket
