#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace rocket::vulkan_policy {

struct ApiVersion {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;

    friend bool operator==(const ApiVersion&, const ApiVersion&) = default;
};

constexpr bool apiVersionAtLeast(ApiVersion actual, ApiVersion required)
{
    if (actual.major != required.major) return actual.major > required.major;
    if (actual.minor != required.minor) return actual.minor > required.minor;
    return actual.patch >= required.patch;
}

inline constexpr ApiVersion requiredApiVersion {1, 3, 0};

enum class DeviceKind {
    Other,
    Integrated,
    Discrete,
    Virtual,
    Cpu,
};

struct QueueFamilySupport {
    std::uint32_t index = 0;
    std::uint32_t queueCount = 0;
    bool graphics = false;
    bool present = false;
};

std::optional<std::uint32_t> chooseUnifiedGraphicsPresentQueue(
    std::span<const QueueFamilySupport> queueFamilies);

enum class SurfaceFormat {
    Other,
    B8G8R8A8UnormSrgbNonlinear,
    R8G8B8A8UnormSrgbNonlinear,
};

std::optional<SurfaceFormat> chooseRequiredUnormSurfaceFormat(
    std::span<const SurfaceFormat> formats);

struct DeviceCapabilities {
    std::string name;
    ApiVersion apiVersion;
    DeviceKind kind = DeviceKind::Other;
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    std::uint32_t driverVersion = 0;
    std::uint64_t deviceLocalMemoryBytes = 0;
    std::uint32_t maxImageDimension2D = 0;
    bool unifiedGraphicsPresentQueue = false;
    bool dynamicRendering = false;
    bool synchronization2 = false;
    bool timelineSemaphore = false;
    bool swapchain = false;
    bool requiredUnormSurfaceFormat = false;
};

enum class CapabilityFailure : std::uint32_t {
    None = 0,
    ApiVersion13 = 1U << 0,
    UnifiedGraphicsPresentQueue = 1U << 1,
    DynamicRendering = 1U << 2,
    Synchronization2 = 1U << 3,
    TimelineSemaphore = 1U << 4,
    Swapchain = 1U << 5,
    RequiredUnormSurfaceFormat = 1U << 6,
};

constexpr CapabilityFailure operator|(CapabilityFailure lhs, CapabilityFailure rhs)
{
    return static_cast<CapabilityFailure>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

constexpr CapabilityFailure& operator|=(CapabilityFailure& lhs, CapabilityFailure rhs)
{
    lhs = lhs | rhs;
    return lhs;
}

constexpr bool hasCapabilityFailure(CapabilityFailure failures, CapabilityFailure failure)
{
    return (static_cast<std::uint32_t>(failures) & static_cast<std::uint32_t>(failure)) != 0;
}

struct DeviceEvaluation {
    CapabilityFailure failures = CapabilityFailure::None;
    std::int64_t score = 0;

    bool supported() const { return failures == CapabilityFailure::None; }
};

std::int64_t scoreDevice(const DeviceCapabilities& device);
DeviceEvaluation evaluateDevice(const DeviceCapabilities& device);
std::string describeCapabilityFailures(CapabilityFailure failures);
std::optional<std::size_t> selectPreferredDevice(
    std::span<const DeviceCapabilities> devices);

enum class PresentMode {
    Fifo,
    FifoRelaxed,
    Mailbox,
    Immediate,
    Other,
};

std::optional<PresentMode> chooseFifoPresentMode(
    std::span<const PresentMode> availableModes);

// A max count of zero means the surface imposes no maximum, matching Vulkan.
std::optional<std::uint32_t> chooseSwapchainImageCount(
    std::uint32_t minimumImageCount,
    std::uint32_t maximumImageCount);

struct Extent2D {
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    friend bool operator==(const Extent2D&, const Extent2D&) = default;
};

struct SurfaceExtentConstraints {
    std::optional<Extent2D> currentExtent;
    Extent2D minimumExtent;
    Extent2D maximumExtent;
};

enum class SwapchainExtentStatus {
    Ready,
    DeferredZeroExtent,
    InvalidSurfaceLimits,
};

struct SwapchainExtentDecision {
    SwapchainExtentStatus status = SwapchainExtentStatus::InvalidSurfaceLimits;
    Extent2D extent;
};

SwapchainExtentDecision chooseSwapchainExtent(
    const SurfaceExtentConstraints& constraints,
    Extent2D requestedDrawableExtent);

inline constexpr std::uint32_t framesInFlight = 2;

constexpr std::uint32_t frameSlotForSerial(std::uint64_t frameSerial)
{
    return frameSerial == 0
        ? 0
        : static_cast<std::uint32_t>((frameSerial - 1) % framesInFlight);
}

constexpr bool wouldExceedFramesInFlight(
    std::uint64_t nextFrameSerial,
    std::uint64_t completedFrameSerial)
{
    return nextFrameSerial > completedFrameSerial
        && nextFrameSerial - completedFrameSerial > framesInFlight;
}

enum class FrameRetirementAction {
    UseUnusedSlot,
    ReuseCompletedSlot,
    RetireSignaledSlot,
    WaitThenRetireSlot,
    InvalidSerialState,
};

struct FrameRetirementDecision {
    FrameRetirementAction action = FrameRetirementAction::InvalidSerialState;
    std::uint32_t frameSlot = 0;
    std::uint64_t completedSerialAfterRetirement = 0;
    bool waitForFence = false;
    bool retireFrameResources = false;
};

FrameRetirementDecision decideFrameRetirement(
    std::uint64_t nextFrameSerial,
    std::uint64_t slotSubmittedSerial,
    std::uint64_t completedFrameSerial,
    bool slotFenceSignaled);

struct PipelineCacheKey {
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    std::uint32_t driverVersion = 0;
    std::uint64_t shaderAbi = 0;

    friend bool operator==(const PipelineCacheKey&, const PipelineCacheKey&) = default;
};

std::string pipelineCacheKeyString(const PipelineCacheKey& key);

} // namespace rocket::vulkan_policy
