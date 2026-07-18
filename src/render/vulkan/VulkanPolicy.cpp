#include "render/vulkan/VulkanPolicy.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <limits>
#include <sstream>
#include <tuple>

namespace rocket::vulkan_policy {
namespace {

constexpr std::uint64_t bytesPerMebibyte = 1024ULL * 1024ULL;

std::int64_t deviceKindScore(DeviceKind kind)
{
    switch (kind) {
    case DeviceKind::Discrete: return 4'000'000;
    case DeviceKind::Integrated: return 3'000'000;
    case DeviceKind::Virtual: return 2'000'000;
    case DeviceKind::Other: return 1'000'000;
    case DeviceKind::Cpu: return 0;
    }
    return 0;
}

bool preferredTieBreak(
    const DeviceCapabilities& candidate,
    const DeviceCapabilities& current)
{
    if (candidate.apiVersion != current.apiVersion) {
        if (candidate.apiVersion.major != current.apiVersion.major) {
            return candidate.apiVersion.major > current.apiVersion.major;
        }
        if (candidate.apiVersion.minor != current.apiVersion.minor) {
            return candidate.apiVersion.minor > current.apiVersion.minor;
        }
        return candidate.apiVersion.patch > current.apiVersion.patch;
    }
    if (candidate.vendorId != current.vendorId) return candidate.vendorId < current.vendorId;
    if (candidate.deviceId != current.deviceId) return candidate.deviceId < current.deviceId;
    if (candidate.driverVersion != current.driverVersion) {
        return candidate.driverVersion > current.driverVersion;
    }
    return candidate.name < current.name;
}

} // namespace

std::optional<std::uint32_t> chooseUnifiedGraphicsPresentQueue(
    std::span<const QueueFamilySupport> queueFamilies)
{
    std::optional<std::uint32_t> selected;
    for (const QueueFamilySupport& queue : queueFamilies) {
        if (queue.queueCount == 0 || !queue.graphics || !queue.present) continue;
        if (!selected.has_value() || queue.index < *selected) selected = queue.index;
    }
    return selected;
}

std::optional<SurfaceFormat> chooseRequiredUnormSurfaceFormat(
    std::span<const SurfaceFormat> formats)
{
    for (const SurfaceFormat format : formats) {
        if (format == SurfaceFormat::B8G8R8A8UnormSrgbNonlinear) return format;
    }
    for (const SurfaceFormat format : formats) {
        if (format == SurfaceFormat::R8G8B8A8UnormSrgbNonlinear) return format;
    }
    return std::nullopt;
}

std::int64_t scoreDevice(const DeviceCapabilities& device)
{
    const std::uint64_t memoryMebibytes = std::min<std::uint64_t>(
        device.deviceLocalMemoryBytes / bytesPerMebibyte,
        65'535);
    const std::uint32_t imageDimensionScore = std::min(
        device.maxImageDimension2D / 1024U,
        255U);
    return deviceKindScore(device.kind)
        + static_cast<std::int64_t>(memoryMebibytes * 10ULL)
        + static_cast<std::int64_t>(imageDimensionScore);
}

DeviceEvaluation evaluateDevice(const DeviceCapabilities& device)
{
    DeviceEvaluation result;
    result.score = scoreDevice(device);
    if (!apiVersionAtLeast(device.apiVersion, requiredApiVersion)) {
        result.failures |= CapabilityFailure::ApiVersion13;
    }
    if (!device.unifiedGraphicsPresentQueue) {
        result.failures |= CapabilityFailure::UnifiedGraphicsPresentQueue;
    }
    if (!device.dynamicRendering) result.failures |= CapabilityFailure::DynamicRendering;
    if (!device.synchronization2) result.failures |= CapabilityFailure::Synchronization2;
    if (!device.timelineSemaphore) result.failures |= CapabilityFailure::TimelineSemaphore;
    if (!device.swapchain) result.failures |= CapabilityFailure::Swapchain;
    if (!device.requiredUnormSurfaceFormat) {
        result.failures |= CapabilityFailure::RequiredUnormSurfaceFormat;
    }
    return result;
}

std::string describeCapabilityFailures(CapabilityFailure failures)
{
    if (failures == CapabilityFailure::None) return {};
    constexpr std::array<std::pair<CapabilityFailure, const char*>, 7> names {{
        {CapabilityFailure::ApiVersion13, "Vulkan API 1.3"},
        {CapabilityFailure::UnifiedGraphicsPresentQueue, "one queue with graphics and presentation"},
        {CapabilityFailure::DynamicRendering, "Dynamic Rendering"},
        {CapabilityFailure::Synchronization2, "Synchronization2"},
        {CapabilityFailure::TimelineSemaphore, "timeline semaphores"},
        {CapabilityFailure::Swapchain, "VK_KHR_swapchain"},
        {CapabilityFailure::RequiredUnormSurfaceFormat, "BGRA8 or RGBA8 UNORM surface format"},
    }};
    std::ostringstream out;
    bool first = true;
    for (const auto& [failure, name] : names) {
        if (!hasCapabilityFailure(failures, failure)) continue;
        if (!first) out << ", ";
        out << name;
        first = false;
    }
    return out.str();
}

std::optional<std::size_t> selectPreferredDevice(
    std::span<const DeviceCapabilities> devices)
{
    std::optional<std::size_t> selected;
    std::int64_t selectedScore = std::numeric_limits<std::int64_t>::min();
    for (std::size_t index = 0; index < devices.size(); ++index) {
        const DeviceEvaluation evaluation = evaluateDevice(devices[index]);
        if (!evaluation.supported()) continue;
        if (!selected.has_value()
            || evaluation.score > selectedScore
            || (evaluation.score == selectedScore
                && preferredTieBreak(devices[index], devices[*selected]))) {
            selected = index;
            selectedScore = evaluation.score;
        }
    }
    return selected;
}

std::optional<PresentMode> chooseFifoPresentMode(
    std::span<const PresentMode> availableModes)
{
    return std::find(availableModes.begin(), availableModes.end(), PresentMode::Fifo)
            != availableModes.end()
        ? std::optional(PresentMode::Fifo)
        : std::nullopt;
}

std::optional<std::uint32_t> chooseSwapchainImageCount(
    std::uint32_t minimumImageCount,
    std::uint32_t maximumImageCount)
{
    if (minimumImageCount == 0
        || (maximumImageCount != 0 && maximumImageCount < minimumImageCount)) {
        return std::nullopt;
    }
    std::uint32_t imageCount = std::max(3U, minimumImageCount);
    if (maximumImageCount != 0) imageCount = std::min(imageCount, maximumImageCount);
    return imageCount;
}

SwapchainExtentDecision chooseSwapchainExtent(
    const SurfaceExtentConstraints& constraints,
    Extent2D requestedDrawableExtent)
{
    if (requestedDrawableExtent.width == 0 || requestedDrawableExtent.height == 0) {
        return {SwapchainExtentStatus::DeferredZeroExtent, {}};
    }
    if (constraints.minimumExtent.width > constraints.maximumExtent.width
        || constraints.minimumExtent.height > constraints.maximumExtent.height
        || constraints.maximumExtent.width == 0
        || constraints.maximumExtent.height == 0) {
        return {SwapchainExtentStatus::InvalidSurfaceLimits, {}};
    }
    if (constraints.currentExtent.has_value()) {
        const Extent2D current = *constraints.currentExtent;
        if (current.width == 0 || current.height == 0) {
            return {SwapchainExtentStatus::DeferredZeroExtent, {}};
        }
        if (current.width < constraints.minimumExtent.width
            || current.width > constraints.maximumExtent.width
            || current.height < constraints.minimumExtent.height
            || current.height > constraints.maximumExtent.height) {
            return {SwapchainExtentStatus::InvalidSurfaceLimits, {}};
        }
        return {SwapchainExtentStatus::Ready, current};
    }
    return {
        SwapchainExtentStatus::Ready,
        {
            std::clamp(
                requestedDrawableExtent.width,
                constraints.minimumExtent.width,
                constraints.maximumExtent.width),
            std::clamp(
                requestedDrawableExtent.height,
                constraints.minimumExtent.height,
                constraints.maximumExtent.height),
        },
    };
}

FrameRetirementDecision decideFrameRetirement(
    std::uint64_t nextFrameSerial,
    std::uint64_t slotSubmittedSerial,
    std::uint64_t completedFrameSerial,
    bool slotFenceSignaled)
{
    FrameRetirementDecision result;
    result.frameSlot = frameSlotForSerial(nextFrameSerial);
    result.completedSerialAfterRetirement = completedFrameSerial;
    if (nextFrameSerial == 0
        || completedFrameSerial >= nextFrameSerial
        || slotSubmittedSerial >= nextFrameSerial
        || (slotSubmittedSerial != 0
            && frameSlotForSerial(slotSubmittedSerial) != result.frameSlot)) {
        return result;
    }
    if (slotSubmittedSerial == 0) {
        result.action = FrameRetirementAction::UseUnusedSlot;
        return result;
    }
    if (slotSubmittedSerial <= completedFrameSerial) {
        result.action = FrameRetirementAction::ReuseCompletedSlot;
        return result;
    }

    result.completedSerialAfterRetirement = slotSubmittedSerial;
    result.retireFrameResources = true;
    if (slotFenceSignaled) {
        result.action = FrameRetirementAction::RetireSignaledSlot;
    } else {
        result.action = FrameRetirementAction::WaitThenRetireSlot;
        result.waitForFence = true;
    }
    return result;
}

std::string pipelineCacheKeyString(const PipelineCacheKey& key)
{
    std::ostringstream out;
    out << "vk13-v" << std::hex << std::setfill('0')
        << std::setw(8) << key.vendorId
        << "-d" << std::setw(8) << key.deviceId
        << "-r" << std::setw(8) << key.driverVersion
        << "-abi" << std::setw(16) << key.shaderAbi;
    return out.str();
}

} // namespace rocket::vulkan_policy
