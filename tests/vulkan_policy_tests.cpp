#include "render/vulkan/VulkanPolicy.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <string>
#include <utility>

namespace {

rocket::vulkan_policy::DeviceCapabilities supportedDevice(
    rocket::vulkan_policy::DeviceKind kind,
    std::string name)
{
    rocket::vulkan_policy::DeviceCapabilities device;
    device.name = std::move(name);
    device.apiVersion = {1, 3, 0};
    device.kind = kind;
    device.vendorId = 0x10de;
    device.deviceId = 0x1234;
    device.driverVersion = 10;
    device.deviceLocalMemoryBytes = 8ULL * 1024ULL * 1024ULL * 1024ULL;
    device.maxImageDimension2D = 16'384;
    device.unifiedGraphicsPresentQueue = true;
    device.dynamicRendering = true;
    device.synchronization2 = true;
    device.timelineSemaphore = true;
    device.swapchain = true;
    device.requiredUnormSurfaceFormat = true;
    return device;
}

} // namespace

int main()
{
    using namespace rocket::vulkan_policy;

    assert(apiVersionAtLeast({1, 3, 0}, requiredApiVersion));
    assert(apiVersionAtLeast({1, 3, 275}, requiredApiVersion));
    assert(apiVersionAtLeast({2, 0, 0}, requiredApiVersion));
    assert(!apiVersionAtLeast({1, 2, 999}, requiredApiVersion));

    const std::array queueFamilies {
        QueueFamilySupport {3, 1, true, true},
        QueueFamilySupport {1, 1, true, false},
        QueueFamilySupport {2, 2, true, true},
        QueueFamilySupport {0, 0, true, true},
    };
    assert(chooseUnifiedGraphicsPresentQueue(queueFamilies) == 2U);
    const std::array noUnifiedQueue {
        QueueFamilySupport {0, 1, true, false},
        QueueFamilySupport {1, 1, false, true},
    };
    assert(!chooseUnifiedGraphicsPresentQueue(noUnifiedQueue).has_value());

    const std::array formats {
        SurfaceFormat::Other,
        SurfaceFormat::R8G8B8A8UnormSrgbNonlinear,
        SurfaceFormat::B8G8R8A8UnormSrgbNonlinear,
    };
    assert(chooseRequiredUnormSurfaceFormat(formats)
        == SurfaceFormat::B8G8R8A8UnormSrgbNonlinear);
    const std::array rgbaOnly {SurfaceFormat::R8G8B8A8UnormSrgbNonlinear};
    assert(chooseRequiredUnormSurfaceFormat(rgbaOnly)
        == SurfaceFormat::R8G8B8A8UnormSrgbNonlinear);
    const std::array unsupportedFormats {SurfaceFormat::Other};
    assert(!chooseRequiredUnormSurfaceFormat(unsupportedFormats).has_value());

    DeviceCapabilities missing;
    missing.apiVersion = {1, 2, 0};
    const DeviceEvaluation rejected = evaluateDevice(missing);
    assert(!rejected.supported());
    assert(hasCapabilityFailure(rejected.failures, CapabilityFailure::ApiVersion13));
    assert(hasCapabilityFailure(rejected.failures, CapabilityFailure::UnifiedGraphicsPresentQueue));
    assert(hasCapabilityFailure(rejected.failures, CapabilityFailure::DynamicRendering));
    assert(hasCapabilityFailure(rejected.failures, CapabilityFailure::Synchronization2));
    assert(hasCapabilityFailure(rejected.failures, CapabilityFailure::TimelineSemaphore));
    assert(hasCapabilityFailure(rejected.failures, CapabilityFailure::Swapchain));
    assert(hasCapabilityFailure(rejected.failures, CapabilityFailure::RequiredUnormSurfaceFormat));
    const std::string rejectionText = describeCapabilityFailures(rejected.failures);
    assert(rejectionText.find("Vulkan API 1.3") != std::string::npos);
    assert(rejectionText.find("VK_KHR_swapchain") != std::string::npos);
    assert(rejectionText.find("UNORM") != std::string::npos);

    DeviceCapabilities integrated = supportedDevice(DeviceKind::Integrated, "Integrated");
    DeviceCapabilities discrete = supportedDevice(DeviceKind::Discrete, "Discrete");
    DeviceCapabilities unsupportedDiscrete = discrete;
    unsupportedDiscrete.name = "Unsupported discrete";
    unsupportedDiscrete.timelineSemaphore = false;
    const std::array devices {integrated, unsupportedDiscrete, discrete};
    assert(scoreDevice(discrete) > scoreDevice(integrated));
    assert(selectPreferredDevice(devices) == 2U);

    // Enumeration order cannot change a score tie: stable identity fields are
    // used before the source index.
    DeviceCapabilities highVendor = discrete;
    highVendor.name = "Z device";
    highVendor.vendorId = 0x8086;
    DeviceCapabilities lowVendor = discrete;
    lowVendor.name = "A device";
    lowVendor.vendorId = 0x1002;
    const std::array tiedForward {highVendor, lowVendor};
    const std::array tiedReverse {lowVendor, highVendor};
    assert(tiedForward[*selectPreferredDevice(tiedForward)].vendorId == 0x1002);
    assert(tiedReverse[*selectPreferredDevice(tiedReverse)].vendorId == 0x1002);

    const std::array presentModes {
        PresentMode::Mailbox,
        PresentMode::Immediate,
        PresentMode::Fifo,
        PresentMode::FifoRelaxed,
    };
    assert(chooseFifoPresentMode(presentModes) == PresentMode::Fifo);
    const std::array noFifo {PresentMode::Mailbox, PresentMode::FifoRelaxed};
    assert(!chooseFifoPresentMode(noFifo).has_value());

    assert(chooseSwapchainImageCount(1, 0) == 3U);
    assert(chooseSwapchainImageCount(2, 2) == 2U);
    assert(chooseSwapchainImageCount(4, 0) == 4U);
    assert(chooseSwapchainImageCount(4, 5) == 4U);
    assert(!chooseSwapchainImageCount(0, 0).has_value());
    assert(!chooseSwapchainImageCount(3, 2).has_value());

    SurfaceExtentConstraints variableExtent;
    variableExtent.minimumExtent = {320, 200};
    variableExtent.maximumExtent = {3840, 2160};
    SwapchainExtentDecision extent = chooseSwapchainExtent(variableExtent, {1280, 800});
    assert(extent.status == SwapchainExtentStatus::Ready);
    assert((extent.extent == Extent2D {1280, 800}));
    extent = chooseSwapchainExtent(variableExtent, {8000, 100});
    assert((extent.extent == Extent2D {3840, 200}));
    extent = chooseSwapchainExtent(variableExtent, {0, 800});
    assert(extent.status == SwapchainExtentStatus::DeferredZeroExtent);

    SurfaceExtentConstraints fixedExtent = variableExtent;
    fixedExtent.currentExtent = Extent2D {1920, 1080};
    extent = chooseSwapchainExtent(fixedExtent, {1280, 800});
    assert(extent.status == SwapchainExtentStatus::Ready);
    assert((extent.extent == Extent2D {1920, 1080}));
    fixedExtent.currentExtent = Extent2D {0, 0};
    assert(chooseSwapchainExtent(fixedExtent, {1280, 800}).status
        == SwapchainExtentStatus::DeferredZeroExtent);
    SurfaceExtentConstraints invalidExtent = variableExtent;
    invalidExtent.minimumExtent = {1000, 700};
    invalidExtent.maximumExtent = {800, 600};
    assert(chooseSwapchainExtent(invalidExtent, {1280, 800}).status
        == SwapchainExtentStatus::InvalidSurfaceLimits);

    assert(framesInFlight == 2);
    assert(frameSlotForSerial(1) == 0);
    assert(frameSlotForSerial(2) == 1);
    assert(frameSlotForSerial(3) == 0);
    assert(!wouldExceedFramesInFlight(2, 0));
    assert(wouldExceedFramesInFlight(3, 0));
    assert(!wouldExceedFramesInFlight(3, 1));

    SceneVertexBindingState binding;
    assert(updateSceneVertexBinding(binding, SceneVertexBindingKind::Instances, 0));
    assert(!updateSceneVertexBinding(binding, SceneVertexBindingKind::Instances, 0));
    // Movement exhaust alternates a triangle draw between instanced effects
    // and the textured mining rig. Binding 0 must be restored at both edges.
    assert(updateSceneVertexBinding(binding, SceneVertexBindingKind::Vertices, 0));
    assert(updateSceneVertexBinding(binding, SceneVertexBindingKind::Instances, 0));
    assert(!updateSceneVertexBinding(binding, SceneVertexBindingKind::Instances, 0));
    assert(updateSceneVertexBinding(binding, SceneVertexBindingKind::Instances, 1));

    FrameRetirementDecision retirement = decideFrameRetirement(1, 0, 0, false);
    assert(retirement.action == FrameRetirementAction::UseUnusedSlot);
    assert(retirement.frameSlot == 0 && !retirement.waitForFence);
    retirement = decideFrameRetirement(3, 1, 1, false);
    assert(retirement.action == FrameRetirementAction::ReuseCompletedSlot);
    retirement = decideFrameRetirement(3, 1, 0, true);
    assert(retirement.action == FrameRetirementAction::RetireSignaledSlot);
    assert(retirement.retireFrameResources && !retirement.waitForFence);
    assert(retirement.completedSerialAfterRetirement == 1);
    retirement = decideFrameRetirement(3, 1, 0, false);
    assert(retirement.action == FrameRetirementAction::WaitThenRetireSlot);
    assert(retirement.retireFrameResources && retirement.waitForFence);
    retirement = decideFrameRetirement(3, 2, 0, false);
    assert(retirement.action == FrameRetirementAction::InvalidSerialState);

    const PipelineCacheKey cacheKey {0x10de, 0x2687, 0x12345678, 0xAABBCCDDEEFF0011ULL};
    const std::string keyText = pipelineCacheKeyString(cacheKey);
    assert(keyText == "vk13-v000010de-d00002687-r12345678-abiaabbccddeeff0011");
    assert(pipelineCacheKeyString({0x10de, 0x2687, 0x12345679, 0xAABBCCDDEEFF0011ULL}) != keyText);
    assert(pipelineCacheKeyString({0x10de, 0x2687, 0x12345678, 0xAABBCCDDEEFF0012ULL}) != keyText);

    return 0;
}
