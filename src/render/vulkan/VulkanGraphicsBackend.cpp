#include "render/vulkan/VulkanGraphicsBackend.h"

#include "render/vulkan/VulkanPolicy.h"

#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <utility>

namespace rocket {
namespace {

constexpr VkDeviceSize initialVertexCapacity = 16U * 1024U * 1024U;
constexpr VkDeviceSize initialInstanceCapacity = 1024U * 1024U;
constexpr std::uint32_t requiredApiVersion = VK_API_VERSION_1_3;
constexpr std::uint64_t shaderAbiVersion = 0x4f52454249545632ULL; // OREBITV2

VKAPI_ATTR VkBool32 VKAPI_CALL validationCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* userData)
{
    auto* host = static_cast<IPlatformHost*>(userData);
    if (!host || !callbackData || !callbackData->pMessage) return VK_FALSE;
    const PlatformLogLevel level = severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
        ? PlatformLogLevel::Error
        : (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                ? PlatformLogLevel::Warning
                : PlatformLogLevel::Info);
    host->log(level, std::string("Vulkan validation: ") + callbackData->pMessage);
    return VK_FALSE;
}

std::vector<std::uint8_t> readBinaryFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) return {};
    const std::streamsize size = stream.tellg();
    if (size <= 0) return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), size)) return {};
    return bytes;
}

bool containsExtension(const std::vector<VkExtensionProperties>& extensions, const char* name)
{
    return std::any_of(extensions.begin(), extensions.end(), [name](const VkExtensionProperties& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

bool containsLayer(const std::vector<VkLayerProperties>& layers, const char* name)
{
    return std::any_of(layers.begin(), layers.end(), [name](const VkLayerProperties& layer) {
        return std::strcmp(layer.layerName, name) == 0;
    });
}

std::string vkResultName(VkResult result)
{
    switch (result) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_EVENT_SET: return "VK_EVENT_SET";
    case VK_EVENT_RESET: return "VK_EVENT_RESET";
    case VK_INCOMPLETE: return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    default: return "VkResult(" + std::to_string(static_cast<int>(result)) + ")";
    }
}

std::optional<VkSurfaceFormatKHR> chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats)
{
    if (formats.size() == 1 && formats.front().format == VK_FORMAT_UNDEFINED) {
        return VkSurfaceFormatKHR {
            VK_FORMAT_B8G8R8A8_UNORM,
            formats.front().colorSpace,
        };
    }
    std::vector<vulkan_policy::SurfaceFormat> policyFormats;
    policyFormats.reserve(formats.size());
    for (const VkSurfaceFormatKHR& candidate : formats) {
        if (candidate.colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            policyFormats.push_back(vulkan_policy::SurfaceFormat::Other);
        } else if (candidate.format == VK_FORMAT_B8G8R8A8_UNORM) {
            policyFormats.push_back(vulkan_policy::SurfaceFormat::B8G8R8A8UnormSrgbNonlinear);
        } else if (candidate.format == VK_FORMAT_R8G8B8A8_UNORM) {
            policyFormats.push_back(vulkan_policy::SurfaceFormat::R8G8B8A8UnormSrgbNonlinear);
        } else {
            policyFormats.push_back(vulkan_policy::SurfaceFormat::Other);
        }
    }
    const std::optional<vulkan_policy::SurfaceFormat> selected =
        vulkan_policy::chooseRequiredUnormSurfaceFormat(policyFormats);
    if (!selected.has_value()) return std::nullopt;
    const VkFormat selectedFormat = *selected
            == vulkan_policy::SurfaceFormat::B8G8R8A8UnormSrgbNonlinear
        ? VK_FORMAT_B8G8R8A8_UNORM
        : VK_FORMAT_R8G8B8A8_UNORM;
    const auto found = std::find_if(formats.begin(), formats.end(), [selectedFormat](const auto& candidate) {
        return candidate.format == selectedFormat
            && candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    return found == formats.end() ? std::nullopt : std::optional(*found);
}

VkCompositeAlphaFlagBitsKHR chooseCompositeAlpha(VkCompositeAlphaFlagsKHR supported)
{
    constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4> choices {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
    };
    for (VkCompositeAlphaFlagBitsKHR choice : choices) {
        if ((supported & choice) != 0) return choice;
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

VkShaderModule createShaderModule(VkDevice device, std::span<const std::uint8_t> bytes)
{
    if (bytes.empty() || bytes.size() % sizeof(std::uint32_t) != 0) return VK_NULL_HANDLE;
    const VkShaderModuleCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = bytes.size(),
        .pCode = reinterpret_cast<const std::uint32_t*>(bytes.data())
    };
    VkShaderModule shader = VK_NULL_HANDLE;
    return vkCreateShaderModule(device, &createInfo, nullptr, &shader) == VK_SUCCESS ? shader : VK_NULL_HANDLE;
}

} // namespace

VulkanGraphicsBackend::VulkanGraphicsBackend(
    IPlatformHost& host,
    ITextureSource& textures,
    SDL_Window* window,
    std::filesystem::path runtimeRoot,
    std::filesystem::path cacheDirectory)
    : host_(host),
      textures_(textures),
      window_(window),
      runtimeRoot_(std::move(runtimeRoot)),
      cacheDirectory_(std::move(cacheDirectory))
{
}

VulkanGraphicsBackend::~VulkanGraphicsBackend()
{
    shutdown();
}

bool VulkanGraphicsBackend::initialize()
{
    if (initialized_) return true;
    if (!window_) {
        setError("Vulkan initialization requires an SDL Vulkan window.");
        return false;
    }
    if (!initializeLoaderAndInstance()
        || !selectPhysicalDevice()
        || !createDeviceAndAllocator()
        || !createFrameResources()
        || !createPipelineCache()
        || !createDescriptorsAndSampler()
        || !createOrRecreateSwapchain()
        || (swapchain_ && !createScenePipeline())
        || !loadSceneTextures()) {
        shutdown();
        return false;
    }
    initialized_ = true;
    frameStatus_ = GraphicsFrameStatus::Skipped;
    // Resolve the requested cadence against the active display before a
    // benchmark snapshots its descriptor. Render-time refresh checks still
    // handle display migration and mode changes during normal play.
    configuredRefreshRateHz_ = host_.displayRefreshRateHz();
    refreshFrameLimit();
    host_.log(PlatformLogLevel::Info,
        "Vulkan 1.3 initialized on " + deviceName_ + " with FIFO presentation and two frames in flight.");
    return true;
}

bool VulkanGraphicsBackend::initializeLoaderAndInstance()
{
    const auto getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        SDL_Vulkan_GetVkGetInstanceProcAddr());
    if (!getInstanceProcAddr) {
        setError(std::string("SDL could not resolve vkGetInstanceProcAddr: ") + SDL_GetError());
        return false;
    }
    volkInitializeCustom(getInstanceProcAddr);

    std::uint32_t loaderVersion = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion) vkEnumerateInstanceVersion(&loaderVersion);
    if (loaderVersion < requiredApiVersion) {
        setError("OREBIT requires a Vulkan 1.3 loader and driver; this system exposes only Vulkan "
            + std::to_string(VK_API_VERSION_MAJOR(loaderVersion)) + "."
            + std::to_string(VK_API_VERSION_MINOR(loaderVersion)) + ".");
        return false;
    }

    Uint32 sdlExtensionCount = 0;
    const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);
    if (!sdlExtensions || sdlExtensionCount == 0) {
        setError(std::string("SDL could not provide Vulkan surface extensions: ") + SDL_GetError());
        return false;
    }
    std::vector<const char*> extensions(sdlExtensions, sdlExtensions + sdlExtensionCount);

    std::uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());
    debugUtilsEnabled_ = containsExtension(availableExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (debugUtilsEnabled_) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    std::uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> layers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
#if !defined(NDEBUG)
    validationEnabled_ = containsLayer(layers, "VK_LAYER_KHRONOS_validation");
#endif
    const char* validationLayer = "VK_LAYER_KHRONOS_validation";

    const VkApplicationInfo applicationInfo {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "OREBIT",
        .applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .pEngineName = "OREBIT Direct Vulkan",
        .engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
        .apiVersion = requiredApiVersion
    };
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = validationCallback,
        .pUserData = &host_
    };
    const VkInstanceCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = debugUtilsEnabled_ ? &debugCreateInfo : nullptr,
        .pApplicationInfo = &applicationInfo,
        .enabledLayerCount = validationEnabled_ ? 1U : 0U,
        .ppEnabledLayerNames = validationEnabled_ ? &validationLayer : nullptr,
        .enabledExtensionCount = static_cast<std::uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data()
    };
    if (!check(vkCreateInstance(&createInfo, nullptr, &instance_), "vkCreateInstance")) return false;
    volkLoadInstance(instance_);

    if (debugUtilsEnabled_ && vkCreateDebugUtilsMessengerEXT) {
        check(vkCreateDebugUtilsMessengerEXT(instance_, &debugCreateInfo, nullptr, &debugMessenger_),
            "vkCreateDebugUtilsMessengerEXT");
    }
    if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_)) {
        setError(std::string("SDL could not create the Vulkan surface: ") + SDL_GetError());
        return false;
    }
    return true;
}

bool VulkanGraphicsBackend::selectPhysicalDevice()
{
    std::uint32_t deviceCount = 0;
    if (!check(vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr), "vkEnumeratePhysicalDevices")
        || deviceCount == 0) {
        if (deviceCount == 0) setError("No Vulkan physical devices were found.");
        return false;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    if (!check(vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data()), "vkEnumeratePhysicalDevices")) {
        return false;
    }

    struct Candidate {
        VkPhysicalDevice device = VK_NULL_HANDLE;
        std::uint32_t queueFamily = UINT32_MAX;
        int score = -1;
        VkPhysicalDeviceProperties properties {};
    } best;
    bool rejectedPackedVertexFormat = false;

    for (VkPhysicalDevice candidateDevice : devices) {
        VkPhysicalDeviceProperties properties {};
        vkGetPhysicalDeviceProperties(candidateDevice, &properties);
        if (properties.apiVersion < requiredApiVersion) continue;

        VkFormatProperties halfPairProperties {};
        VkFormatProperties colorProperties {};
        vkGetPhysicalDeviceFormatProperties(candidateDevice, VK_FORMAT_R16G16_SFLOAT, &halfPairProperties);
        vkGetPhysicalDeviceFormatProperties(candidateDevice, VK_FORMAT_R8G8B8A8_UNORM, &colorProperties);
        if ((halfPairProperties.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) == 0
            || (colorProperties.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) == 0) {
            rejectedPackedVertexFormat = true;
            continue;
        }

        std::uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(candidateDevice, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(candidateDevice, nullptr, &extensionCount, extensions.data());
        if (!containsExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;

        VkPhysicalDeviceVulkan13Features features13 {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
        };
        VkPhysicalDeviceVulkan12Features features12 {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
            .pNext = &features13
        };
        VkPhysicalDeviceFeatures2 features2 {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &features12
        };
        vkGetPhysicalDeviceFeatures2(candidateDevice, &features2);
        if (!features12.timelineSemaphore || !features13.dynamicRendering || !features13.synchronization2) {
            continue;
        }

        std::uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidateDevice, &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidateDevice, &queueCount, queues.data());
        std::vector<vulkan_policy::QueueFamilySupport> queueSupport;
        queueSupport.reserve(queueCount);
        for (std::uint32_t index = 0; index < queueCount; ++index) {
            queueSupport.push_back({
                index,
                queues[index].queueCount,
                (queues[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0
                    && queues[index].timestampValidBits > 0,
                SDL_Vulkan_GetPresentationSupport(instance_, candidateDevice, index),
            });
        }
        const std::optional<std::uint32_t> queueFamily =
            vulkan_policy::chooseUnifiedGraphicsPresentQueue(queueSupport);
        if (!queueFamily.has_value()) continue;

        std::uint32_t formatCount = 0;
        if (vkGetPhysicalDeviceSurfaceFormatsKHR(candidateDevice, surface_, &formatCount, nullptr) != VK_SUCCESS
            || formatCount == 0) {
            continue;
        }
        std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
        if (vkGetPhysicalDeviceSurfaceFormatsKHR(
                candidateDevice, surface_, &formatCount, surfaceFormats.data()) != VK_SUCCESS
            || !chooseSurfaceFormat(surfaceFormats).has_value()) {
            continue;
        }
        VkSurfaceCapabilitiesKHR surfaceCapabilities {};
        if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                candidateDevice, surface_, &surfaceCapabilities) != VK_SUCCESS
            || (surfaceCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
            continue;
        }
        int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 10'000 : 0;
        score += properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 5'000 : 0;
        score += static_cast<int>(properties.limits.maxImageDimension2D / 1024U);
        if (score > best.score) best = {candidateDevice, *queueFamily, score, properties};
    }

    if (!best.device) {
        setError("No supported Vulkan 1.3 device was found. OREBIT requires graphics/present on one queue, "
            "timestamp queries, Dynamic Rendering, Synchronization2, timeline semaphores, VK_KHR_swapchain, "
            "and vertex-buffer "
            "support for R16G16_SFLOAT and R8G8B8A8_UNORM, and a BGRA8/RGBA8 UNORM "
            "SRGB-nonlinear surface format usable as a color attachment."
            + std::string(rejectedPackedVertexFormat
                ? " At least one Vulkan 1.3 device was rejected because a packed scene vertex format is unsupported."
                : ""));
        return false;
    }
    physicalDevice_ = best.device;
    graphicsQueueFamily_ = best.queueFamily;
    deviceName_ = best.properties.deviceName;
    return true;
}

bool VulkanGraphicsBackend::createDeviceAndAllocator()
{
    constexpr float queuePriority = 1.0F;
    const VkDeviceQueueCreateInfo queueCreateInfo {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = graphicsQueueFamily_,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority
    };
    VkPhysicalDeviceVulkan13Features features13 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE
    };
    VkPhysicalDeviceVulkan12Features features12 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &features13,
        .timelineSemaphore = VK_TRUE
    };
    const char* deviceExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    const VkDeviceCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features12,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCreateInfo,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = &deviceExtension
    };
    if (!check(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_), "vkCreateDevice")) return false;
    volkLoadDevice(device_);
    vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);

    VmaVulkanFunctions functions {};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    const VmaAllocatorCreateInfo allocatorCreateInfo {
        .physicalDevice = physicalDevice_,
        .device = device_,
        .pVulkanFunctions = &functions,
        .instance = instance_,
        .vulkanApiVersion = requiredApiVersion
    };
    return check(vmaCreateAllocator(&allocatorCreateInfo, &allocator_), "vmaCreateAllocator");
}

bool VulkanGraphicsBackend::createFrameResources()
{
    for (FrameResources& frame : frames_) {
        const VkCommandPoolCreateInfo poolInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = graphicsQueueFamily_
        };
        if (!check(vkCreateCommandPool(device_, &poolInfo, nullptr, &frame.commandPool), "vkCreateCommandPool")) {
            return false;
        }
        const VkCommandBufferAllocateInfo commandInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = frame.commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };
        if (!check(vkAllocateCommandBuffers(device_, &commandInfo, &frame.commandBuffer), "vkAllocateCommandBuffers")) {
            return false;
        }
        const VkFenceCreateInfo fenceInfo {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT
        };
        const VkSemaphoreCreateInfo semaphoreInfo {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        const VkQueryPoolCreateInfo queryInfo {
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .queryType = VK_QUERY_TYPE_TIMESTAMP,
            .queryCount = 2
        };
        if (!check(vkCreateFence(device_, &fenceInfo, nullptr, &frame.fence), "vkCreateFence")
            || !check(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &frame.imageAvailable), "vkCreateSemaphore")
            || !check(vkCreateQueryPool(device_, &queryInfo, nullptr, &frame.timestampQueries), "vkCreateQueryPool")
            || !ensureVertexCapacity(frame, initialVertexCapacity)
            || !ensureInstanceCapacity(frame, initialInstanceCapacity)
            || !ensureMiningTerrainInstanceCapacity(frame, initialInstanceCapacity)) {
            return false;
        }
    }
    return true;
}

bool VulkanGraphicsBackend::createPipelineCache()
{
    std::error_code error;
    std::filesystem::create_directories(cacheDirectory_, error);
    VkPhysicalDeviceProperties properties {};
    vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
    const vulkan_policy::PipelineCacheKey key {
        properties.vendorID,
        properties.deviceID,
        properties.driverVersion,
        shaderAbiVersion
    };
    pipelineCachePath_ = cacheDirectory_ /
        ("orebit-vulkan-pipeline-cache-" + vulkan_policy::pipelineCacheKeyString(key) + ".bin");
    const std::vector<std::uint8_t> bytes = readBinaryFile(pipelineCachePath_);
    const VkPipelineCacheCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
        .initialDataSize = bytes.size(),
        .pInitialData = bytes.empty() ? nullptr : bytes.data()
    };
    VkResult result = vkCreatePipelineCache(device_, &createInfo, nullptr, &pipelineCache_);
    if (result != VK_SUCCESS && !bytes.empty()) {
        host_.log(PlatformLogLevel::Warning,
            "Discarding an incompatible Vulkan pipeline cache and rebuilding it.");
        const VkPipelineCacheCreateInfo emptyInfo {.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
        result = vkCreatePipelineCache(device_, &emptyInfo, nullptr, &pipelineCache_);
    }
    return check(result, "vkCreatePipelineCache");
}

bool VulkanGraphicsBackend::createDescriptorsAndSampler()
{
    const VkDescriptorSetLayoutBinding binding {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
    };
    const VkDescriptorSetLayoutCreateInfo layoutInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding
    };
    if (!check(vkCreateDescriptorSetLayout(
            device_, &layoutInfo, nullptr, &sceneDescriptorSetLayout_), "vkCreateDescriptorSetLayout")) {
        return false;
    }
    const VkDescriptorPoolSize poolSize {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = static_cast<std::uint32_t>(sceneTextures_.size())
    };
    const VkDescriptorPoolCreateInfo poolInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = static_cast<std::uint32_t>(sceneTextures_.size()),
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize
    };
    if (!check(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &sceneDescriptorPool_),
            "vkCreateDescriptorPool")) {
        return false;
    }
    std::vector<VkDescriptorSetLayout> layouts(sceneTextures_.size(), sceneDescriptorSetLayout_);
    std::vector<VkDescriptorSet> descriptorSets(sceneTextures_.size());
    const VkDescriptorSetAllocateInfo allocationInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = sceneDescriptorPool_,
        .descriptorSetCount = static_cast<std::uint32_t>(layouts.size()),
        .pSetLayouts = layouts.data()
    };
    if (!check(vkAllocateDescriptorSets(device_, &allocationInfo, descriptorSets.data()),
            "vkAllocateDescriptorSets")) {
        return false;
    }
    for (std::size_t index = 0; index < sceneTextures_.size(); ++index) {
        sceneTextures_[index].descriptorSet = descriptorSets[index];
    }
    const VkSamplerCreateInfo samplerInfo {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxAnisotropy = 1.0F,
        .minLod = 0.0F,
        .maxLod = 0.0F
    };
    return check(vkCreateSampler(device_, &samplerInfo, nullptr, &sceneSampler_), "vkCreateSampler");
}

bool VulkanGraphicsBackend::createOrRecreateSwapchain()
{
    int drawableWidth = 0;
    int drawableHeight = 0;
    if (!SDL_GetWindowSizeInPixels(window_, &drawableWidth, &drawableHeight)) {
        setError(std::string("SDL could not query Vulkan drawable size: ") + SDL_GetError());
        return false;
    }
    if (drawableWidth <= 0 || drawableHeight <= 0) {
        frameStatus_ = GraphicsFrameStatus::Skipped;
        return true;
    }
    if (!waitForAllFrames()) return false;
    if (swapchain_ && !check(vkQueueWaitIdle(graphicsQueue_), "vkQueueWaitIdle(swapchain rebuild)")) {
        return false;
    }

    VkSurfaceCapabilitiesKHR capabilities {};
    if (!check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &capabilities),
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) {
        return false;
    }
    if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
        setError("The Vulkan surface cannot be used as a color attachment.");
        return false;
    }
    std::uint32_t formatCount = 0;
    if (!check(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr),
            "vkGetPhysicalDeviceSurfaceFormatsKHR") || formatCount == 0) {
        setError("The Vulkan surface exposes no supported color formats.");
        return false;
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, formats.data());
    const std::optional<VkSurfaceFormatKHR> selectedFormat = chooseSurfaceFormat(formats);
    if (!selectedFormat.has_value()) {
        setError("The Vulkan surface does not expose the required BGRA8 or RGBA8 UNORM "
            "SRGB-nonlinear format.");
        return false;
    }

    vulkan_policy::SurfaceExtentConstraints extentConstraints;
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
        extentConstraints.currentExtent = {
            capabilities.currentExtent.width,
            capabilities.currentExtent.height,
        };
    }
    extentConstraints.minimumExtent = {
        capabilities.minImageExtent.width,
        capabilities.minImageExtent.height,
    };
    extentConstraints.maximumExtent = {
        capabilities.maxImageExtent.width,
        capabilities.maxImageExtent.height,
    };
    const vulkan_policy::SwapchainExtentDecision extentDecision =
        vulkan_policy::chooseSwapchainExtent(
            extentConstraints,
            {static_cast<std::uint32_t>(drawableWidth), static_cast<std::uint32_t>(drawableHeight)});
    if (extentDecision.status == vulkan_policy::SwapchainExtentStatus::DeferredZeroExtent) {
        frameStatus_ = GraphicsFrameStatus::Skipped;
        return true;
    }
    if (extentDecision.status != vulkan_policy::SwapchainExtentStatus::Ready) {
        setError("The Vulkan surface reported invalid swapchain extent limits.");
        return false;
    }
    const VkExtent2D extent {extentDecision.extent.width, extentDecision.extent.height};

    const std::optional<std::uint32_t> selectedImageCount =
        vulkan_policy::chooseSwapchainImageCount(capabilities.minImageCount, capabilities.maxImageCount);
    if (!selectedImageCount.has_value()) {
        setError("The Vulkan surface reported invalid swapchain image-count limits.");
        return false;
    }
    std::uint32_t imageCount = *selectedImageCount;
    const bool newCaptureSupported =
        (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    const VkImageUsageFlags imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
        | (newCaptureSupported ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0U);
    const VkSwapchainKHR oldSwapchain = swapchain_;
    const VkSwapchainCreateInfoKHR createInfo {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface_,
        .minImageCount = imageCount,
        .imageFormat = selectedFormat->format,
        .imageColorSpace = selectedFormat->colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = imageUsage,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = chooseCompositeAlpha(capabilities.supportedCompositeAlpha),
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
        .oldSwapchain = oldSwapchain
    };
    VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
    if (!check(vkCreateSwapchainKHR(device_, &createInfo, nullptr, &newSwapchain), "vkCreateSwapchainKHR")) {
        return false;
    }
    if (!check(vkGetSwapchainImagesKHR(device_, newSwapchain, &imageCount, nullptr),
            "vkGetSwapchainImagesKHR(count)")) {
        vkDestroySwapchainKHR(device_, newSwapchain, nullptr);
        return false;
    }
    std::vector<VkImage> newImages(imageCount);
    if (!check(vkGetSwapchainImagesKHR(device_, newSwapchain, &imageCount, newImages.data()),
            "vkGetSwapchainImagesKHR(images)")) {
        vkDestroySwapchainKHR(device_, newSwapchain, nullptr);
        return false;
    }
    newImages.resize(imageCount);
    std::vector<VkImageView> newImageViews(imageCount, VK_NULL_HANDLE);
    std::vector<VkSemaphore> newRenderFinished(imageCount, VK_NULL_HANDLE);
    const auto discardNewSwapchain = [&]() {
        for (VkSemaphore semaphore : newRenderFinished) {
            if (semaphore) vkDestroySemaphore(device_, semaphore, nullptr);
        }
        for (VkImageView view : newImageViews) {
            if (view) vkDestroyImageView(device_, view, nullptr);
        }
        vkDestroySwapchainKHR(device_, newSwapchain, nullptr);
    };
    const VkSemaphoreCreateInfo semaphoreInfo {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (std::size_t index = 0; index < newImages.size(); ++index) {
        const VkImageViewCreateInfo viewInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = newImages[index],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = selectedFormat->format,
            .components = {
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY
            },
            .subresourceRange = {
                VK_IMAGE_ASPECT_COLOR_BIT,
                0,
                1,
                0,
                1
            }
        };
        if (!check(vkCreateImageView(device_, &viewInfo, nullptr, &newImageViews[index]),
                "vkCreateImageView(swapchain)")
            || !check(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &newRenderFinished[index]),
                "vkCreateSemaphore(present)")) {
            discardNewSwapchain();
            return false;
        }
    }

    for (VkSemaphore semaphore : swapchainRenderFinished_) {
        if (semaphore) vkDestroySemaphore(device_, semaphore, nullptr);
    }
    for (VkImageView view : swapchainImageViews_) {
        if (view) vkDestroyImageView(device_, view, nullptr);
    }
    if (oldSwapchain) vkDestroySwapchainKHR(device_, oldSwapchain, nullptr);
    swapchain_ = newSwapchain;
    swapchainFormat_ = selectedFormat->format;
    swapchainColorSpace_ = selectedFormat->colorSpace;
    swapchainExtent_ = extent;
    swapchainImages_ = std::move(newImages);
    swapchainImageViews_ = std::move(newImageViews);
    swapchainRenderFinished_ = std::move(newRenderFinished);
    swapchainCaptureSupported_ = newCaptureSupported;
    swapchainRebuildRequested_ = false;
    return true;
}

bool VulkanGraphicsBackend::createScenePipeline()
{
    destroyScenePipeline();
    const std::vector<std::uint8_t> vertexBytes = readBinaryFile(runtimeRoot_ / "assets/shaders/scene.vert.spv");
    const std::vector<std::uint8_t> fragmentBytes = readBinaryFile(runtimeRoot_ / "assets/shaders/scene.frag.spv");
    const std::vector<std::uint8_t> instanceVertexBytes =
        readBinaryFile(runtimeRoot_ / "assets/shaders/scene_instance.vert.spv");
    const std::vector<std::uint8_t> instanceFragmentBytes =
        readBinaryFile(runtimeRoot_ / "assets/shaders/scene_instance.frag.spv");
    const VkShaderModule vertexShader = createShaderModule(device_, vertexBytes);
    const VkShaderModule fragmentShader = createShaderModule(device_, fragmentBytes);
    const VkShaderModule instanceVertexShader = createShaderModule(device_, instanceVertexBytes);
    const VkShaderModule instanceFragmentShader = createShaderModule(device_, instanceFragmentBytes);
    if (!vertexShader || !fragmentShader || !instanceVertexShader || !instanceFragmentShader) {
        if (vertexShader) vkDestroyShaderModule(device_, vertexShader, nullptr);
        if (fragmentShader) vkDestroyShaderModule(device_, fragmentShader, nullptr);
        if (instanceVertexShader) vkDestroyShaderModule(device_, instanceVertexShader, nullptr);
        if (instanceFragmentShader) vkDestroyShaderModule(device_, instanceFragmentShader, nullptr);
        setError("Packaged Vulkan scene shaders are missing or invalid under assets/shaders.");
        return false;
    }

    const std::array<VkPipelineShaderStageCreateInfo, 2> stages {{
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertexShader, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragmentShader, .pName = "main"}
    }};
    const VkVertexInputBindingDescription binding {
        .binding = 0,
        .stride = sizeof(PackedSceneVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };
    const std::array<VkVertexInputAttributeDescription, 3> attributes {{
        {.location = 0, .binding = 0, .format = VK_FORMAT_R16G16_SFLOAT,
            .offset = static_cast<std::uint32_t>(offsetof(PackedSceneVertex, x))},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R8G8B8A8_UNORM,
            .offset = static_cast<std::uint32_t>(offsetof(PackedSceneVertex, r))},
        {.location = 2, .binding = 0, .format = VK_FORMAT_R16G16_SFLOAT,
            .offset = static_cast<std::uint32_t>(offsetof(PackedSceneVertex, u))}
    }};
    const VkPipelineVertexInputStateCreateInfo vertexInput {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size()),
        .pVertexAttributeDescriptions = attributes.data()
    };
    const VkVertexInputBindingDescription instanceBinding {
        .binding = 0,
        .stride = sizeof(PackedSceneInstance),
        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE
    };
    const std::array<VkVertexInputAttributeDescription, 7> instanceAttributes {{
        {.location = 0, .binding = 0, .format = VK_FORMAT_R16G16_SFLOAT,
            .offset = static_cast<std::uint32_t>(offsetof(PackedSceneInstance, centerX))},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R16G16_SFLOAT,
            .offset = static_cast<std::uint32_t>(offsetof(PackedSceneInstance, axisXx))},
        {.location = 2, .binding = 0, .format = VK_FORMAT_R16G16_SFLOAT,
            .offset = static_cast<std::uint32_t>(offsetof(PackedSceneInstance, axisYx))},
        {.location = 3, .binding = 0, .format = VK_FORMAT_R8G8B8A8_UNORM,
            .offset = static_cast<std::uint32_t>(offsetof(PackedSceneInstance, r))},
        {.location = 4, .binding = 0, .format = VK_FORMAT_R16G16_UNORM,
            .offset = static_cast<std::uint32_t>(offsetof(PackedSceneInstance, u0))},
        {.location = 5, .binding = 0, .format = VK_FORMAT_R16G16_UNORM,
            .offset = static_cast<std::uint32_t>(offsetof(PackedSceneInstance, u1))},
        {.location = 6, .binding = 0, .format = VK_FORMAT_R8G8_UINT,
            .offset = static_cast<std::uint32_t>(offsetof(PackedSceneInstance, shape))}
    }};
    const VkPipelineVertexInputStateCreateInfo instanceVertexInput {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &instanceBinding,
        .vertexAttributeDescriptionCount = static_cast<std::uint32_t>(instanceAttributes.size()),
        .pVertexAttributeDescriptions = instanceAttributes.data()
    };
    const VkPipelineInputAssemblyStateCreateInfo inputAssembly {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };
    const VkPipelineViewportStateCreateInfo viewportState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1
    };
    const VkPipelineRasterizationStateCreateInfo rasterization {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0F
    };
    const VkPipelineMultisampleStateCreateInfo multisample {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
    };
    const VkPipelineColorBlendAttachmentState blendAttachment {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
            | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };
    const VkPipelineColorBlendStateCreateInfo blend {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blendAttachment
    };
    constexpr std::array<VkDynamicState, 2> dynamicStates {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()
    };
    const VkPushConstantRange pushRange {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(ScenePushConstants)
    };
    const VkPipelineLayoutCreateInfo layoutInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &sceneDescriptorSetLayout_,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange
    };
    bool success = check(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &scenePipelineLayout_),
        "vkCreatePipelineLayout");
    const VkPipelineRenderingCreateInfo renderingInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &swapchainFormat_
    };
    const VkGraphicsPipelineCreateInfo pipelineInfo {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingInfo,
        .stageCount = static_cast<std::uint32_t>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pColorBlendState = &blend,
        .pDynamicState = &dynamic,
        .layout = scenePipelineLayout_
    };
    if (success) {
        success = check(vkCreateGraphicsPipelines(
            device_, pipelineCache_, 1, &pipelineInfo, nullptr, &scenePipeline_), "vkCreateGraphicsPipelines(scene)");
        if (success) recordPipelineCreation();
    }
    if (success) {
        const std::array<VkPipelineShaderStageCreateInfo, 2> instanceStages {{
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = instanceVertexShader, .pName = "main"},
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = instanceFragmentShader, .pName = "main"}
        }};
        VkGraphicsPipelineCreateInfo instancePipelineInfo = pipelineInfo;
        instancePipelineInfo.pStages = instanceStages.data();
        instancePipelineInfo.pVertexInputState = &instanceVertexInput;
        success = check(vkCreateGraphicsPipelines(
            device_, pipelineCache_, 1, &instancePipelineInfo, nullptr, &sceneInstancePipeline_),
            "vkCreateGraphicsPipelines(scene instances)");
        if (success) recordPipelineCreation();
    }
    vkDestroyShaderModule(device_, vertexShader, nullptr);
    vkDestroyShaderModule(device_, fragmentShader, nullptr);
    vkDestroyShaderModule(device_, instanceVertexShader, nullptr);
    vkDestroyShaderModule(device_, instanceFragmentShader, nullptr);
    return success;
}

bool VulkanGraphicsBackend::createTexture(
    TextureResource& texture,
    const std::uint8_t* rgba,
    int width,
    int height,
    std::string_view debugName)
{
    if (!rgba || width <= 0 || height <= 0) return false;
    const VkDeviceSize byteCount = static_cast<VkDeviceSize>(width)
        * static_cast<VkDeviceSize>(height) * 4U;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;
    VmaAllocationInfo stagingInfo {};
    const VkBufferCreateInfo bufferInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = byteCount,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    const VmaAllocationCreateInfo stagingAllocationInfo {
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT
            | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
    };
    if (!check(vmaCreateBuffer(allocator_, &bufferInfo, &stagingAllocationInfo,
            &stagingBuffer, &stagingAllocation, &stagingInfo), "vmaCreateBuffer(texture staging)")) {
        return false;
    }
    std::memcpy(stagingInfo.pMappedData, rgba, static_cast<std::size_t>(byteCount));
    if (!check(vmaFlushAllocation(allocator_, stagingAllocation, 0, byteCount),
            "vmaFlushAllocation(texture staging)")) {
        vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);
        return false;
    }

    const VkImageCreateInfo imageInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    const VmaAllocationCreateInfo imageAllocationInfo {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    if (!check(vmaCreateImage(allocator_, &imageInfo, &imageAllocationInfo,
            &texture.image, &texture.allocation, nullptr), "vmaCreateImage(scene texture)")) {
        vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);
        return false;
    }
    const bool uploaded = submitSynchronousUpload([&](VkCommandBuffer commandBuffer) {
        const VkImageMemoryBarrier2 toTransfer {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image = texture.image,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
        };
        const VkDependencyInfo transferDependency {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &toTransfer
        };
        vkCmdPipelineBarrier2(commandBuffer, &transferDependency);
        const VkBufferImageCopy copy {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .imageOffset = {0, 0, 0},
            .imageExtent = {
                static_cast<std::uint32_t>(width),
                static_cast<std::uint32_t>(height),
                1
            }
        };
        vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, texture.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        const VkImageMemoryBarrier2 toShader {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image = texture.image,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
        };
        const VkDependencyInfo shaderDependency {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &toShader
        };
        vkCmdPipelineBarrier2(commandBuffer, &shaderDependency);
    });
    vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);
    if (!uploaded) {
        vmaDestroyImage(allocator_, texture.image, texture.allocation);
        texture.image = VK_NULL_HANDLE;
        texture.allocation = VK_NULL_HANDLE;
        return false;
    }

    const VkImageViewCreateInfo viewInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = texture.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .components = {
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY
        },
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    };
    if (!check(vkCreateImageView(device_, &viewInfo, nullptr, &texture.view),
            "vkCreateImageView(scene texture)")) {
        return false;
    }
    const VkDescriptorImageInfo descriptorImage {
        .sampler = sceneSampler_,
        .imageView = texture.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    const VkWriteDescriptorSet write {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = texture.descriptorSet,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &descriptorImage
    };
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    texture.width = width;
    texture.height = height;
    texture.ready = true;
    if (debugUtilsEnabled_ && vkSetDebugUtilsObjectNameEXT) {
        const std::string name(debugName);
        const VkDebugUtilsObjectNameInfoEXT nameInfo {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
            .objectType = VK_OBJECT_TYPE_IMAGE,
            .objectHandle = reinterpret_cast<std::uint64_t>(texture.image),
            .pObjectName = name.c_str()
        };
        vkSetDebugUtilsObjectNameEXT(device_, &nameInfo);
    }
    return true;
}

bool VulkanGraphicsBackend::loadSceneTextures()
{
    constexpr std::array<std::uint8_t, 4> white {255, 255, 255, 255};
    if (!createTexture(sceneTextures_[0], white.data(), 1, 1, "scene-white")) {
        return false;
    }
    composer_.setTextureReady(TextureId::None, true);

    for (std::size_t pageIndex = 0; pageIndex < kSceneAtlasPages.size(); ++pageIndex) {
        const SceneAtlasPage& page = kSceneAtlasPages[pageIndex];
        textures_.request(page.key, page.relativePath);
        if (textures_.status(page.key) == TextureStatus::Failed) {
            setError(textures_.lastError());
            return false;
        }
        const std::optional<DecodedImageView> image = textures_.decodedImage(page.key);
        if (!image || !image->valid()) {
            setError("Texture source did not provide decoded RGBA data for required Vulkan asset: "
                + std::string(page.relativePath));
            return false;
        }
        if (image->width != page.width || image->height != page.height) {
            setError("Generated scene atlas page dimensions do not match its compiled manifest: "
                + std::string(page.relativePath));
            return false;
        }
        TextureResource& resource = sceneTextures_[pageIndex + 1U];
        if (!createTexture(resource, image->rgba.data(), image->width, image->height, page.key)) {
            return false;
        }
        textures_.releaseDecodedImage(page.key);
        ++diagnostics_.texturesReady;
    }
    for (std::size_t texture = 1; texture < textureIndex(TextureId::Count); ++texture) {
        const TextureId textureId = static_cast<TextureId>(texture);
        const std::size_t pageIndex = sceneAtlasPageForTexture(textureId);
        composer_.setTextureReady(textureId, pageIndex < kSceneAtlasPages.size());
    }
    updateMemoryDiagnostics();
    return true;
}

void VulkanGraphicsBackend::render(const RenderSnapshot& snapshot)
{
    frameStatus_ = GraphicsFrameStatus::Skipped;
    diagnostics_.sceneDrawCalls = 0;
    diagnostics_.sceneVertices = 0;
    diagnostics_.bufferUploads = 0;
    diagnostics_.uploadedBytes = 0;
    diagnostics_.limiterIdleMilliseconds = 0.0;
    diagnostics_.pipelineCreationsThisFrame = 0;
    if (!initialized_ || renderingActive_) return;

    const double refreshRateHz = host_.displayRefreshRateHz();
    if (std::abs(refreshRateHz - configuredRefreshRateHz_) > 0.05) {
        configuredRefreshRateHz_ = refreshRateHz;
        refreshFrameLimit();
    }

    const ViewportMetrics viewport = host_.viewportMetrics();
    composer_.setViewport({
        viewport.logicalWidth,
        viewport.logicalHeight,
        viewport.drawableWidth,
        viewport.drawableHeight,
        viewport.densityRatio,
        viewport.sceneLeftNdc
    });
    composer_.setPresentationTime(host_.monotonicSeconds());
    const ScenePacket& packet = composer_.compose(snapshot);
    if (!beginFrame(packet)) return;
    recordScene(packet);
}

bool VulkanGraphicsBackend::beginFrame(const ScenePacket& packet)
{
    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSizeInPixels(window_, &width, &height) || width <= 0 || height <= 0) {
        frameStatus_ = GraphicsFrameStatus::Skipped;
        return false;
    }
    if (surfaceRebuildRequested_) {
        if (!recreateSurface()) frameStatus_ = GraphicsFrameStatus::Fatal;
        if (frameStatus_ == GraphicsFrameStatus::Fatal) return false;
    }
    if (swapchainRebuildRequested_
        || swapchainExtent_.width != static_cast<std::uint32_t>(width)
        || swapchainExtent_.height != static_cast<std::uint32_t>(height)) {
        const VkFormat previousFormat = swapchainFormat_;
        if (!createOrRecreateSwapchain()) {
            frameStatus_ = GraphicsFrameStatus::Fatal;
            return false;
        }
        if (!swapchain_) return false;
        if (!scenePipeline_ || !sceneInstancePipeline_ || previousFormat != swapchainFormat_) {
            if (!createScenePipeline()) {
                frameStatus_ = GraphicsFrameStatus::Fatal;
                return false;
            }
        }
    }

    FrameResources& frame = frames_[frameSlot_];
    retireFrame(frame);
    if (frameStatus_ == GraphicsFrameStatus::Fatal) return false;
    const std::uint64_t acquireStartedNanoseconds = SDL_GetTicksNS();
    VkResult acquireResult = vkAcquireNextImageKHR(
        device_, swapchain_, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &activeImageIndex_);
    diagnostics_.limiterIdleMilliseconds += static_cast<double>(
        SDL_GetTicksNS() - acquireStartedNanoseconds) / 1'000'000.0;
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchainRebuildRequested_ = true;
        frameStatus_ = GraphicsFrameStatus::Skipped;
        return false;
    }
    if (acquireResult == VK_ERROR_SURFACE_LOST_KHR) {
        surfaceRebuildRequested_ = true;
        if (!recreateSurface()) frameStatus_ = GraphicsFrameStatus::Fatal;
        return false;
    }
    if (acquireResult == VK_SUBOPTIMAL_KHR) {
        // VK_SUBOPTIMAL_KHR still acquires an image and signals imageAvailable.
        // Consume both through the normal submit/present path, then recreate the
        // swapchain before the next acquisition.
        swapchainRebuildRequested_ = true;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR
        && !check(acquireResult, "vkAcquireNextImageKHR")) {
        frameStatus_ = GraphicsFrameStatus::Fatal;
        return false;
    }
    if (!check(vkResetCommandPool(device_, frame.commandPool, 0), "vkResetCommandPool")) {
        frameStatus_ = GraphicsFrameStatus::Fatal;
        return false;
    }
    const VkCommandBufferBeginInfo beginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    if (!check(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo), "vkBeginCommandBuffer")) {
        frameStatus_ = GraphicsFrameStatus::Fatal;
        return false;
    }
    vkCmdResetQueryPool(frame.commandBuffer, frame.timestampQueries, 0, 2);
    vkCmdWriteTimestamp2(frame.commandBuffer, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, frame.timestampQueries, 0);

    const VkImageMemoryBarrier2 toAttachment {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        // The acquire semaphore is waited at COLOR_ATTACHMENT_OUTPUT.  Include
        // that stage in the barrier's first synchronization scope so the
        // layout transition cannot race presentation's read of this image.
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_NONE,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .image = swapchainImages_[activeImageIndex_],
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    };
    const VkDependencyInfo dependency {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toAttachment
    };
    vkCmdPipelineBarrier2(frame.commandBuffer, &dependency);

    const VkClearValue clearValue {.color = {{
        packet.clearColor.r, packet.clearColor.g, packet.clearColor.b, packet.clearColor.a
    }}};
    const VkRenderingAttachmentInfo colorAttachment {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapchainImageViews_[activeImageIndex_],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clearValue
    };
    const VkRenderingInfo renderingInfo {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, swapchainExtent_},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment
    };
    vkCmdBeginRendering(frame.commandBuffer, &renderingInfo);
    activeFrame_ = &frame;
    renderingActive_ = true;
    frameStatus_ = GraphicsFrameStatus::Ready;
    return true;
}

void VulkanGraphicsBackend::recordScene(const ScenePacket& packet)
{
    if (!activeFrame_ || !renderingActive_) return;
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(packet.vertices.size_bytes());
    if (!ensureVertexCapacity(*activeFrame_, std::max<VkDeviceSize>(bytes, 1U))) {
        frameStatus_ = GraphicsFrameStatus::Fatal;
        return;
    }
    if (bytes > 0) {
        std::memcpy(activeFrame_->mappedVertices, packet.vertices.data(), static_cast<std::size_t>(bytes));
        if (!check(vmaFlushAllocation(allocator_, activeFrame_->vertexAllocation, 0, bytes),
                "vmaFlushAllocation(scene vertices)")) {
            frameStatus_ = GraphicsFrameStatus::Fatal;
            return;
        }
        ++diagnostics_.bufferUploads;
        diagnostics_.uploadedBytes += static_cast<std::size_t>(bytes);
    }

    const VkDeviceSize instanceBytes = static_cast<VkDeviceSize>(packet.instances.size_bytes());
    if (!ensureInstanceCapacity(*activeFrame_, std::max<VkDeviceSize>(instanceBytes, 1U))) {
        frameStatus_ = GraphicsFrameStatus::Fatal;
        return;
    }
    if (instanceBytes > 0) {
        std::memcpy(activeFrame_->mappedInstances, packet.instances.data(),
            static_cast<std::size_t>(instanceBytes));
        if (!check(vmaFlushAllocation(
                allocator_, activeFrame_->instanceAllocation, 0, instanceBytes),
                "vmaFlushAllocation(scene instances)")) {
            frameStatus_ = GraphicsFrameStatus::Fatal;
            return;
        }
        ++diagnostics_.bufferUploads;
        diagnostics_.uploadedBytes += static_cast<std::size_t>(instanceBytes);
    }

    const VkDeviceSize terrainBytes = static_cast<VkDeviceSize>(packet.miningTerrainVertices.size_bytes());
    const VkDeviceSize terrainInstanceBytes =
        static_cast<VkDeviceSize>(packet.miningTerrainInstances.size_bytes());
    const bool terrainUploadRequired = activeFrame_->miningTerrainRevision != packet.miningTerrainRevision;
    if (terrainUploadRequired && terrainBytes > 0) {
        if (!ensureMiningTerrainVertexCapacity(*activeFrame_, terrainBytes)) {
            frameStatus_ = GraphicsFrameStatus::Fatal;
            return;
        }
        std::memcpy(
            activeFrame_->mappedMiningTerrainVertices,
            packet.miningTerrainVertices.data(),
            static_cast<std::size_t>(terrainBytes));
        if (!check(vmaFlushAllocation(
                allocator_, activeFrame_->miningTerrainVertexAllocation, 0, terrainBytes),
                "vmaFlushAllocation(mining terrain vertices)")) {
            frameStatus_ = GraphicsFrameStatus::Fatal;
            return;
        }
        ++diagnostics_.bufferUploads;
        diagnostics_.uploadedBytes += static_cast<std::size_t>(terrainBytes);
    }
    if (terrainUploadRequired && terrainInstanceBytes > 0) {
        if (!ensureMiningTerrainInstanceCapacity(*activeFrame_, terrainInstanceBytes)) {
            frameStatus_ = GraphicsFrameStatus::Fatal;
            return;
        }
        std::memcpy(
            activeFrame_->mappedMiningTerrainInstances,
            packet.miningTerrainInstances.data(),
            static_cast<std::size_t>(terrainInstanceBytes));
        if (!check(vmaFlushAllocation(
                allocator_, activeFrame_->miningTerrainInstanceAllocation, 0, terrainInstanceBytes),
                "vmaFlushAllocation(mining terrain instances)")) {
            frameStatus_ = GraphicsFrameStatus::Fatal;
            return;
        }
        ++diagnostics_.bufferUploads;
        diagnostics_.uploadedBytes += static_cast<std::size_t>(terrainInstanceBytes);
    }
    if (terrainUploadRequired && (terrainBytes > 0 || terrainInstanceBytes > 0)) {
        activeFrame_->miningTerrainRevision = packet.miningTerrainRevision;
    }

    const VkViewport viewport {
        .x = 0.0F,
        .y = 0.0F,
        .width = static_cast<float>(swapchainExtent_.width),
        .height = static_cast<float>(swapchainExtent_.height),
        .minDepth = 0.0F,
        .maxDepth = 1.0F
    };
    const VkRect2D scissor {{0, 0}, swapchainExtent_};
    vkCmdSetViewport(activeFrame_->commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(activeFrame_->commandBuffer, 0, 1, &scissor);
    const VkDeviceSize vertexOffset = 0;
    SceneVertexStream boundVertexStream = SceneVertexStream::Frame;
    bool vertexStreamBound = false;
    SceneInstanceStream boundInstanceStream = SceneInstanceStream::Frame;
    bool instanceStreamBound = false;
    SceneDrawType boundDrawType = SceneDrawType::Triangles;
    bool pipelineBound = false;
    VkDescriptorSet boundDescriptor = VK_NULL_HANDLE;
    for (const SceneDraw& draw : packet.draws) {
        if (!pipelineBound || draw.drawType != boundDrawType) {
            vkCmdBindPipeline(
                activeFrame_->commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                draw.drawType == SceneDrawType::InstancedQuad
                    ? sceneInstancePipeline_
                    : scenePipeline_);
            boundDrawType = draw.drawType;
            pipelineBound = true;
        }
        if (draw.drawType == SceneDrawType::InstancedQuad) {
            if (!instanceStreamBound || draw.instanceStream != boundInstanceStream) {
                const VkBuffer instanceBuffer = draw.instanceStream == SceneInstanceStream::MiningTerrain
                    ? activeFrame_->miningTerrainInstanceBuffer
                    : activeFrame_->instanceBuffer;
                if (!instanceBuffer) {
                    setError("A scene draw referenced an unavailable Vulkan instance stream.");
                    frameStatus_ = GraphicsFrameStatus::Fatal;
                    return;
                }
                vkCmdBindVertexBuffers(
                    activeFrame_->commandBuffer, 0, 1, &instanceBuffer, &vertexOffset);
                boundInstanceStream = draw.instanceStream;
                instanceStreamBound = true;
            }
        } else if (!vertexStreamBound || draw.vertexStream != boundVertexStream) {
            const VkBuffer vertexBuffer = draw.vertexStream == SceneVertexStream::MiningTerrain
                ? activeFrame_->miningTerrainVertexBuffer
                : activeFrame_->vertexBuffer;
            if (!vertexBuffer) {
                setError("A scene draw referenced an unavailable Vulkan vertex stream.");
                frameStatus_ = GraphicsFrameStatus::Fatal;
                return;
            }
            vkCmdBindVertexBuffers(activeFrame_->commandBuffer, 0, 1, &vertexBuffer, &vertexOffset);
            boundVertexStream = draw.vertexStream;
            vertexStreamBound = true;
        }
        std::size_t textureResource = 0;
        if (draw.atlasPage != kNoSceneAtlasPage) {
            const std::size_t pageIndex = draw.atlasPage;
            if (pageIndex < kSceneAtlasPages.size()) {
                textureResource = pageIndex + 1U;
            }
        }
        VkDescriptorSet descriptor = sceneTextures_[textureResource].ready
            ? sceneTextures_[textureResource].descriptorSet
            : sceneTextures_[0].descriptorSet;
        if (descriptor != boundDescriptor) {
            vkCmdBindDescriptorSets(activeFrame_->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                scenePipelineLayout_, 0, 1, &descriptor, 0, nullptr);
            boundDescriptor = descriptor;
        }
        ScenePushConstants push {};
        if (draw.coordinateSpace == CoordinateSpace::World) {
            push.positionScale[0] = packet.transform.worldUnitX * 2.0F / std::max(1.0F, packet.transform.cssWidth);
            push.positionScale[1] = packet.transform.worldUnitY * 2.0F / std::max(1.0F, packet.transform.cssHeight);
            push.positionOffset[0] = packet.transform.pixelCenterX * 2.0F / std::max(1.0F, packet.transform.cssWidth) - 1.0F;
            push.positionOffset[1] = packet.transform.pixelCenterY * 2.0F / std::max(1.0F, packet.transform.cssHeight) - 1.0F;
        } else {
            push.positionScale[0] = 1.0F;
            push.positionScale[1] = 1.0F;
        }
        push.effectColor[0] = draw.effectColor.r;
        push.effectColor[1] = draw.effectColor.g;
        push.effectColor[2] = draw.effectColor.b;
        push.effectColor[3] = draw.effectColor.a;
        std::copy(draw.effectParams.begin(), draw.effectParams.end(), push.effectParams);
        std::copy(draw.effectSize.begin(), draw.effectSize.end(), push.effectSize);
        push.effectMode = draw.pipeline == PipelineClass::RoundedFrame ? 1U : 0U;
        push.useTexture = draw.pipeline == PipelineClass::Textured ? 1U : 0U;
        vkCmdPushConstants(activeFrame_->commandBuffer, scenePipelineLayout_,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        if (draw.drawType == SceneDrawType::InstancedQuad) {
            vkCmdDraw(activeFrame_->commandBuffer, 6, draw.instanceCount, 0, draw.firstInstance);
        } else {
            vkCmdDraw(activeFrame_->commandBuffer, draw.vertexCount, 1, draw.firstVertex, 0);
        }
    }
    diagnostics_.sceneDrawCalls = static_cast<int>(packet.draws.size());
    diagnostics_.sceneVertices = static_cast<int>(
        packet.vertices.size() + packet.miningTerrainVertices.size()
        + (packet.instances.size() + packet.miningTerrainInstances.size()) * 6U);
}

bool VulkanGraphicsBackend::prepareFrameCapture()
{
    destroyFrameCaptureResources();
    frameCaptureError_.clear();
    if (!swapchainCaptureSupported_) {
        frameCaptureError_ = "The active Vulkan surface does not support swapchain image readback.";
        host_.log(PlatformLogLevel::Warning, frameCaptureError_);
        frameCaptureRequested_ = false;
        return false;
    }
    if (swapchainExtent_.width == 0 || swapchainExtent_.height == 0) {
        frameCaptureError_ = "A zero-extent Vulkan surface cannot be captured.";
        host_.log(PlatformLogLevel::Warning, frameCaptureError_);
        frameCaptureRequested_ = false;
        return false;
    }
    const VkDeviceSize pixelCount = static_cast<VkDeviceSize>(swapchainExtent_.width)
        * static_cast<VkDeviceSize>(swapchainExtent_.height);
    if (pixelCount > std::numeric_limits<VkDeviceSize>::max() / 4U
        || pixelCount > std::numeric_limits<std::size_t>::max() / 4U) {
        frameCaptureError_ = "Vulkan frame capture dimensions overflow the readback buffer size.";
        host_.log(PlatformLogLevel::Warning, frameCaptureError_);
        frameCaptureRequested_ = false;
        return false;
    }
    const VkDeviceSize byteCount = pixelCount * 4U;
    const VkBufferCreateInfo bufferInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = byteCount,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    const VmaAllocationCreateInfo allocationInfo {
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT
            | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        .preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT
    };
    VmaAllocationInfo resultInfo {};
    const VkResult result = vmaCreateBuffer(
        allocator_, &bufferInfo, &allocationInfo,
        &frameCaptureResources_.buffer,
        &frameCaptureResources_.allocation,
        &resultInfo);
    if (result != VK_SUCCESS || !resultInfo.pMappedData) {
        if (frameCaptureResources_.buffer) destroyFrameCaptureResources();
        frameCaptureError_ = "Unable to allocate the Vulkan frame readback buffer: " + vkResultName(result);
        host_.log(PlatformLogLevel::Warning, frameCaptureError_);
        frameCaptureRequested_ = false;
        return false;
    }
    frameCaptureResources_.mappedPixels = resultInfo.pMappedData;
    frameCaptureResources_.byteCount = byteCount;
    frameCaptureResources_.width = swapchainExtent_.width;
    frameCaptureResources_.height = swapchainExtent_.height;
    frameCaptureResources_.format = swapchainFormat_;
    return true;
}

void VulkanGraphicsBackend::recordFrameCapture(VkCommandBuffer commandBuffer)
{
    const VkImageMemoryBarrier2 toTransfer {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .image = swapchainImages_[activeImageIndex_],
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    };
    const VkDependencyInfo toTransferDependency {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toTransfer
    };
    vkCmdPipelineBarrier2(commandBuffer, &toTransferDependency);
    const VkBufferImageCopy copyRegion {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageOffset = {0, 0, 0},
        .imageExtent = {frameCaptureResources_.width, frameCaptureResources_.height, 1}
    };
    vkCmdCopyImageToBuffer(
        commandBuffer,
        swapchainImages_[activeImageIndex_],
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        frameCaptureResources_.buffer,
        1,
        &copyRegion);
    frameCaptureRequested_ = false;
    frameCaptureRecorded_ = true;
}

bool VulkanGraphicsBackend::completeFrameCapture()
{
    if (!frameCaptureRecorded_) return false;
    frameCaptureRecorded_ = false;
    const VkResult invalidateResult = vmaInvalidateAllocation(
        allocator_, frameCaptureResources_.allocation, 0, frameCaptureResources_.byteCount);
    if (invalidateResult != VK_SUCCESS) {
        frameCaptureError_ = "Unable to invalidate the Vulkan frame readback allocation: "
            + vkResultName(invalidateResult);
        host_.log(PlatformLogLevel::Warning, frameCaptureError_);
        destroyFrameCaptureResources();
        return false;
    }

    FrameCapture capture;
    capture.width = frameCaptureResources_.width;
    capture.height = frameCaptureResources_.height;
    capture.format = frameCaptureResources_.format == VK_FORMAT_B8G8R8A8_UNORM
        ? FrameCapturePixelFormat::Bgra8Unorm
        : FrameCapturePixelFormat::Rgba8Unorm;
    const std::size_t byteCount = static_cast<std::size_t>(frameCaptureResources_.byteCount);
    const auto* first = static_cast<const std::uint8_t*>(frameCaptureResources_.mappedPixels);
    capture.pixels.assign(first, first + byteCount);
    destroyFrameCaptureResources();
    if (!canonicalizeFrameCapture(capture, frameCaptureError_)) {
        host_.log(PlatformLogLevel::Warning, frameCaptureError_);
        return false;
    }
    completedFrameCapture_ = std::move(capture);
    return true;
}

void VulkanGraphicsBackend::destroyFrameCaptureResources()
{
    if (frameCaptureResources_.buffer && allocator_) {
        vmaDestroyBuffer(
            allocator_, frameCaptureResources_.buffer, frameCaptureResources_.allocation);
    }
    frameCaptureResources_ = {};
}

GraphicsFrameStatus VulkanGraphicsBackend::endFrameAndPresent()
{
    if (!renderingActive_ || !activeFrame_) return frameStatus_;
    const bool frameHadFatalError = frameStatus_ == GraphicsFrameStatus::Fatal;
    FrameResources& frame = *activeFrame_;
    vkCmdEndRendering(frame.commandBuffer);
    renderingActive_ = false;

    vkCmdWriteTimestamp2(frame.commandBuffer, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, frame.timestampQueries, 1);
    const bool captureThisFrame = frameCaptureRequested_ && prepareFrameCapture();
    if (captureThisFrame) recordFrameCapture(frame.commandBuffer);
    const VkImageMemoryBarrier2 toPresent {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = captureThisFrame
            ? VK_PIPELINE_STAGE_2_TRANSFER_BIT
            : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = captureThisFrame
            ? VK_ACCESS_2_TRANSFER_READ_BIT
            : VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        // Presentation performs its own visibility operation, but the layout
        // transition still needs an explicit execution endpoint before the
        // render-finished semaphore is signaled. BOTTOM_OF_PIPE is the Vulkan
        // WSI-prescribed destination scope for a transition to PRESENT_SRC.
        .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        .dstAccessMask = VK_ACCESS_2_NONE,
        .oldLayout = captureThisFrame
            ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
            : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .image = swapchainImages_[activeImageIndex_],
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    };
    const VkDependencyInfo dependency {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toPresent
    };
    vkCmdPipelineBarrier2(frame.commandBuffer, &dependency);
    if (!check(vkEndCommandBuffer(frame.commandBuffer), "vkEndCommandBuffer")) {
        frameStatus_ = GraphicsFrameStatus::Fatal;
        activeFrame_ = nullptr;
        return frameStatus_;
    }

    const VkSemaphoreSubmitInfo waitInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = frame.imageAvailable,
        .value = 0,
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .deviceIndex = 0
    };
    const VkCommandBufferSubmitInfo commandInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = frame.commandBuffer,
        .deviceMask = 0
    };
    const VkSemaphoreSubmitInfo signalInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = swapchainRenderFinished_[activeImageIndex_],
        .value = 0,
        // Presentation must not observe the image until the final transition
        // to PRESENT_SRC_KHR has completed.  That transition's destination
        // scope is external to the graphics pipeline, so signal at the end of
        // all commands rather than at ALL_GRAPHICS.
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .deviceIndex = 0
    };
    const VkSubmitInfo2 submitInfo {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &waitInfo,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandInfo,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = &signalInfo
    };
    if (!check(vkResetFences(device_, 1, &frame.fence), "vkResetFences(frame)")) {
        frameStatus_ = GraphicsFrameStatus::Fatal;
        activeFrame_ = nullptr;
        return frameStatus_;
    }
    if (!check(vkQueueSubmit2(graphicsQueue_, 1, &submitInfo, frame.fence), "vkQueueSubmit2")) {
        frameStatus_ = GraphicsFrameStatus::Fatal;
        activeFrame_ = nullptr;
        return frameStatus_;
    }
    frame.submissionPending = true;
    frame.submittedSerial = nextFrameSerial_++;

    const VkPresentInfoKHR presentInfo {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &swapchainRenderFinished_[activeImageIndex_],
        .swapchainCount = 1,
        .pSwapchains = &swapchain_,
        .pImageIndices = &activeImageIndex_
    };
    const VkResult presentResult = vkQueuePresentKHR(graphicsQueue_, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        swapchainRebuildRequested_ = true;
        frameStatus_ = GraphicsFrameStatus::Skipped;
    } else if (presentResult == VK_ERROR_SURFACE_LOST_KHR) {
        surfaceRebuildRequested_ = true;
        frameStatus_ = GraphicsFrameStatus::Skipped;
    } else if (!check(presentResult, "vkQueuePresentKHR")) {
        frameStatus_ = GraphicsFrameStatus::Fatal;
    } else {
        frameStatus_ = GraphicsFrameStatus::Ready;
    }
    if (frameHadFatalError) frameStatus_ = GraphicsFrameStatus::Fatal;
    if (captureThisFrame && frame.submissionPending) {
        retireFrame(frame);
        if (frameStatus_ != GraphicsFrameStatus::Fatal) completeFrameCapture();
    }
    if (frameStatus_ == GraphicsFrameStatus::Ready) applyFrameLimitAfterPresent();
    activeFrame_ = nullptr;
    frameSlot_ = (frameSlot_ + 1U) % framesInFlight;
    return frameStatus_;
}

bool VulkanGraphicsBackend::ensureVertexCapacity(FrameResources& frame, VkDeviceSize bytes)
{
    if (frame.vertexBuffer && frame.vertexCapacity >= bytes) return true;
    if (frame.vertexBuffer) {
        vmaDestroyBuffer(allocator_, frame.vertexBuffer, frame.vertexAllocation);
        frame.vertexBuffer = VK_NULL_HANDLE;
        frame.vertexAllocation = VK_NULL_HANDLE;
        frame.mappedVertices = nullptr;
        frame.vertexCapacity = 0;
    }
    VkDeviceSize capacity = initialVertexCapacity;
    while (capacity < bytes) capacity *= 2U;
    const VkBufferCreateInfo bufferInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = capacity,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    const VmaAllocationCreateInfo allocationInfo {
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT
            | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
    };
    VmaAllocationInfo resultInfo {};
    if (!check(vmaCreateBuffer(allocator_, &bufferInfo, &allocationInfo,
            &frame.vertexBuffer, &frame.vertexAllocation, &resultInfo), "vmaCreateBuffer(scene vertex ring)")) {
        return false;
    }
    frame.mappedVertices = resultInfo.pMappedData;
    frame.vertexCapacity = capacity;
    return true;
}

bool VulkanGraphicsBackend::ensureInstanceCapacity(FrameResources& frame, VkDeviceSize bytes)
{
    if (frame.instanceBuffer && frame.instanceCapacity >= bytes) return true;
    if (frame.instanceBuffer) {
        vmaDestroyBuffer(allocator_, frame.instanceBuffer, frame.instanceAllocation);
        frame.instanceBuffer = VK_NULL_HANDLE;
        frame.instanceAllocation = VK_NULL_HANDLE;
        frame.mappedInstances = nullptr;
        frame.instanceCapacity = 0;
    }
    VkDeviceSize capacity = initialInstanceCapacity;
    while (capacity < bytes) capacity *= 2U;
    const VkBufferCreateInfo bufferInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = capacity,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    const VmaAllocationCreateInfo allocationInfo {
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT
            | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
    };
    VmaAllocationInfo resultInfo {};
    if (!check(vmaCreateBuffer(allocator_, &bufferInfo, &allocationInfo,
            &frame.instanceBuffer, &frame.instanceAllocation, &resultInfo),
            "vmaCreateBuffer(scene instance ring)")) {
        return false;
    }
    frame.mappedInstances = resultInfo.pMappedData;
    frame.instanceCapacity = capacity;
    return true;
}

bool VulkanGraphicsBackend::ensureMiningTerrainVertexCapacity(FrameResources& frame, VkDeviceSize bytes)
{
    if (frame.miningTerrainVertexBuffer && frame.miningTerrainVertexCapacity >= bytes) return true;
    if (frame.miningTerrainVertexBuffer) {
        vmaDestroyBuffer(
            allocator_, frame.miningTerrainVertexBuffer, frame.miningTerrainVertexAllocation);
        frame.miningTerrainVertexBuffer = VK_NULL_HANDLE;
        frame.miningTerrainVertexAllocation = VK_NULL_HANDLE;
        frame.mappedMiningTerrainVertices = nullptr;
        frame.miningTerrainVertexCapacity = 0;
        frame.miningTerrainRevision = 0;
    }
    VkDeviceSize capacity = initialVertexCapacity;
    while (capacity < bytes) capacity *= 2U;
    const VkBufferCreateInfo bufferInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = capacity,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    const VmaAllocationCreateInfo allocationInfo {
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT
            | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
    };
    VmaAllocationInfo resultInfo {};
    if (!check(vmaCreateBuffer(
            allocator_,
            &bufferInfo,
            &allocationInfo,
            &frame.miningTerrainVertexBuffer,
            &frame.miningTerrainVertexAllocation,
            &resultInfo),
            "vmaCreateBuffer(mining terrain vertex cache)")) {
        return false;
    }
    frame.mappedMiningTerrainVertices = resultInfo.pMappedData;
    frame.miningTerrainVertexCapacity = capacity;
    return true;
}

bool VulkanGraphicsBackend::ensureMiningTerrainInstanceCapacity(
    FrameResources& frame,
    VkDeviceSize bytes)
{
    if (frame.miningTerrainInstanceBuffer
        && frame.miningTerrainInstanceCapacity >= bytes) return true;
    if (frame.miningTerrainInstanceBuffer) {
        vmaDestroyBuffer(
            allocator_, frame.miningTerrainInstanceBuffer, frame.miningTerrainInstanceAllocation);
        frame.miningTerrainInstanceBuffer = VK_NULL_HANDLE;
        frame.miningTerrainInstanceAllocation = VK_NULL_HANDLE;
        frame.mappedMiningTerrainInstances = nullptr;
        frame.miningTerrainInstanceCapacity = 0;
        frame.miningTerrainRevision = 0;
    }
    VkDeviceSize capacity = initialInstanceCapacity;
    while (capacity < bytes) capacity *= 2U;
    const VkBufferCreateInfo bufferInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = capacity,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    const VmaAllocationCreateInfo allocationInfo {
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT
            | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
    };
    VmaAllocationInfo resultInfo {};
    if (!check(vmaCreateBuffer(
            allocator_,
            &bufferInfo,
            &allocationInfo,
            &frame.miningTerrainInstanceBuffer,
            &frame.miningTerrainInstanceAllocation,
            &resultInfo),
            "vmaCreateBuffer(mining terrain instance cache)")) {
        return false;
    }
    frame.mappedMiningTerrainInstances = resultInfo.pMappedData;
    frame.miningTerrainInstanceCapacity = capacity;
    return true;
}

void VulkanGraphicsBackend::retireFrame(FrameResources& frame)
{
    if (!frame.submissionPending) return;
    const std::uint64_t fenceWaitStartedNanoseconds = SDL_GetTicksNS();
    const VkResult waitResult = vkWaitForFences(device_, 1, &frame.fence, VK_TRUE, UINT64_MAX);
    diagnostics_.limiterIdleMilliseconds += static_cast<double>(
        SDL_GetTicksNS() - fenceWaitStartedNanoseconds) / 1'000'000.0;
    if (!check(waitResult, "vkWaitForFences(frame)")) {
        frameStatus_ = GraphicsFrameStatus::Fatal;
        return;
    }
    frame.submissionPending = false;
    if (frame.submittedSerial != 0) {
        completedFrameSerial_ = std::max(completedFrameSerial_, frame.submittedSerial);
        std::array<std::uint64_t, 2> timestamps {};
        const VkResult queryResult = vkGetQueryPoolResults(device_, frame.timestampQueries, 0, 2,
            sizeof(timestamps), timestamps.data(), sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT);
        if (queryResult == VK_SUCCESS && timestamps[1] >= timestamps[0]) {
            VkPhysicalDeviceProperties properties {};
            vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
            diagnostics_.gpuFrameMilliseconds = static_cast<double>(timestamps[1] - timestamps[0])
                * static_cast<double>(properties.limits.timestampPeriod) / 1'000'000.0;
        }
    }
}

bool VulkanGraphicsBackend::submitSynchronousUpload(
    const std::function<void(VkCommandBuffer)>& recorder)
{
    if (!device_ || !recorder) return false;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    const VkCommandPoolCreateInfo poolInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = graphicsQueueFamily_
    };
    if (!check(vkCreateCommandPool(device_, &poolInfo, nullptr, &pool), "vkCreateCommandPool(upload)")) {
        return false;
    }
    const VkCommandBufferAllocateInfo allocateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    const VkFenceCreateInfo fenceInfo {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    bool success = check(vkAllocateCommandBuffers(device_, &allocateInfo, &commandBuffer),
        "vkAllocateCommandBuffers(upload)")
        && check(vkCreateFence(device_, &fenceInfo, nullptr, &fence), "vkCreateFence(upload)");
    if (success) {
        const VkCommandBufferBeginInfo beginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
        };
        success = check(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer(upload)");
        if (success) recorder(commandBuffer);
        success = success && check(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer(upload)");
    }
    if (success) {
        const VkCommandBufferSubmitInfo commandInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = commandBuffer
        };
        const VkSubmitInfo2 submitInfo {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &commandInfo
        };
        success = check(vkQueueSubmit2(graphicsQueue_, 1, &submitInfo, fence), "vkQueueSubmit2(upload)")
            && check(vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences(upload)");
    }
    if (fence) vkDestroyFence(device_, fence, nullptr);
    if (pool) vkDestroyCommandPool(device_, pool, nullptr);
    return success;
}

bool VulkanGraphicsBackend::recreateSurface()
{
    if (!device_ || !instance_ || !window_) return false;
    if (!waitForAllFrames()
        || !check(vkQueueWaitIdle(graphicsQueue_), "vkQueueWaitIdle(surface rebuild)")) {
        return false;
    }
    destroySwapchain();
    if (surface_) SDL_Vulkan_DestroySurface(instance_, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_)) {
        setError(std::string("SDL could not recreate the Vulkan surface: ") + SDL_GetError());
        return false;
    }
    if (!SDL_Vulkan_GetPresentationSupport(instance_, physicalDevice_, graphicsQueueFamily_)) {
        setError("The selected Vulkan graphics queue cannot present to the recreated window surface.");
        return false;
    }
    surfaceRebuildRequested_ = false;
    swapchainRebuildRequested_ = true;
    frameStatus_ = GraphicsFrameStatus::Skipped;
    return true;
}

void VulkanGraphicsBackend::shutdown()
{
    if (!instance_ && !device_ && !initialized_) return;
    renderingActive_ = false;
    activeFrame_ = nullptr;
    if (device_) {
        waitForAllFrames();
        if (graphicsQueue_) {
            const VkResult idleResult = vkQueueWaitIdle(graphicsQueue_);
            if (idleResult != VK_SUCCESS) {
                host_.log(PlatformLogLevel::Warning,
                    "Vulkan queue did not become idle during shutdown: " + vkResultName(idleResult));
            }
        }
    }
    savePipelineCache();
    destroyFrameCaptureResources();
    destroyTextures();
    destroyScenePipeline();
    if (sceneSampler_) vkDestroySampler(device_, sceneSampler_, nullptr);
    sceneSampler_ = VK_NULL_HANDLE;
    if (sceneDescriptorPool_) vkDestroyDescriptorPool(device_, sceneDescriptorPool_, nullptr);
    sceneDescriptorPool_ = VK_NULL_HANDLE;
    if (sceneDescriptorSetLayout_) vkDestroyDescriptorSetLayout(device_, sceneDescriptorSetLayout_, nullptr);
    sceneDescriptorSetLayout_ = VK_NULL_HANDLE;
    if (pipelineCache_) vkDestroyPipelineCache(device_, pipelineCache_, nullptr);
    pipelineCache_ = VK_NULL_HANDLE;
    destroySwapchain();
    destroyFrameResources();
    if (allocator_) vmaDestroyAllocator(allocator_);
    allocator_ = VK_NULL_HANDLE;
    if (device_) vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
    if (surface_ && instance_) SDL_Vulkan_DestroySurface(instance_, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
    if (debugMessenger_ && vkDestroyDebugUtilsMessengerEXT) {
        vkDestroyDebugUtilsMessengerEXT(instance_, debugMessenger_, nullptr);
    }
    debugMessenger_ = VK_NULL_HANDLE;
    if (instance_) vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
    physicalDevice_ = VK_NULL_HANDLE;
    graphicsQueue_ = VK_NULL_HANDLE;
    swapchainFormat_ = VK_FORMAT_UNDEFINED;
    swapchainExtent_ = {};
    frameSlot_ = 0;
    nextFrameSerial_ = 1;
    completedFrameSerial_ = 0;
    frameStatus_ = GraphicsFrameStatus::Skipped;
    presentDeadline_.reset();
    configuredRefreshRateHz_ = 0.0;
    surfaceRebuildRequested_ = false;
    swapchainCaptureSupported_ = false;
    frameCaptureRequested_ = false;
    frameCaptureRecorded_ = false;
    completedFrameCapture_.reset();
    frameCaptureError_.clear();
    diagnostics_ = {};
    initialized_ = false;
    composer_.reset();
}

void VulkanGraphicsBackend::destroySwapchain()
{
    if (!device_) return;
    swapchainCaptureSupported_ = false;
    for (VkSemaphore semaphore : swapchainRenderFinished_) {
        if (semaphore) vkDestroySemaphore(device_, semaphore, nullptr);
    }
    swapchainRenderFinished_.clear();
    for (VkImageView view : swapchainImageViews_) {
        if (view) vkDestroyImageView(device_, view, nullptr);
    }
    swapchainImageViews_.clear();
    swapchainImages_.clear();
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
}

void VulkanGraphicsBackend::destroyScenePipeline()
{
    if (!device_) return;
    if (scenePipeline_) vkDestroyPipeline(device_, scenePipeline_, nullptr);
    scenePipeline_ = VK_NULL_HANDLE;
    if (sceneInstancePipeline_) vkDestroyPipeline(device_, sceneInstancePipeline_, nullptr);
    sceneInstancePipeline_ = VK_NULL_HANDLE;
    if (scenePipelineLayout_) vkDestroyPipelineLayout(device_, scenePipelineLayout_, nullptr);
    scenePipelineLayout_ = VK_NULL_HANDLE;
}

void VulkanGraphicsBackend::destroyTextures()
{
    if (!device_ || !allocator_) return;
    for (TextureResource& texture : sceneTextures_) {
        if (texture.view) vkDestroyImageView(device_, texture.view, nullptr);
        if (texture.image) vmaDestroyImage(allocator_, texture.image, texture.allocation);
        texture = {};
    }
}

void VulkanGraphicsBackend::destroyFrameResources()
{
    if (!device_) return;
    for (FrameResources& frame : frames_) {
        if (frame.vertexBuffer && allocator_) {
            vmaDestroyBuffer(allocator_, frame.vertexBuffer, frame.vertexAllocation);
        }
        if (frame.instanceBuffer && allocator_) {
            vmaDestroyBuffer(allocator_, frame.instanceBuffer, frame.instanceAllocation);
        }
        if (frame.miningTerrainVertexBuffer && allocator_) {
            vmaDestroyBuffer(
                allocator_, frame.miningTerrainVertexBuffer, frame.miningTerrainVertexAllocation);
        }
        if (frame.miningTerrainInstanceBuffer && allocator_) {
            vmaDestroyBuffer(
                allocator_,
                frame.miningTerrainInstanceBuffer,
                frame.miningTerrainInstanceAllocation);
        }
        if (frame.timestampQueries) vkDestroyQueryPool(device_, frame.timestampQueries, nullptr);
        if (frame.imageAvailable) vkDestroySemaphore(device_, frame.imageAvailable, nullptr);
        if (frame.fence) vkDestroyFence(device_, frame.fence, nullptr);
        if (frame.commandPool) vkDestroyCommandPool(device_, frame.commandPool, nullptr);
        frame = {};
    }
}

bool VulkanGraphicsBackend::waitForAllFrames()
{
    if (!device_) return true;
    for (FrameResources& frame : frames_) {
        if (!frame.fence || !frame.submissionPending) continue;
        if (!check(vkWaitForFences(device_, 1, &frame.fence, VK_TRUE, UINT64_MAX),
                "vkWaitForFences(all frames)")) {
            return false;
        }
        frame.submissionPending = false;
        if (frame.submittedSerial != 0) {
            completedFrameSerial_ = std::max(completedFrameSerial_, frame.submittedSerial);
        }
    }
    return true;
}

bool VulkanGraphicsBackend::waitForRmlFrames()
{
    return waitForAllFrames();
}

void VulkanGraphicsBackend::savePipelineCache()
{
    if (!device_ || !pipelineCache_ || pipelineCachePath_.empty()) return;
    std::size_t size = 0;
    if (vkGetPipelineCacheData(device_, pipelineCache_, &size, nullptr) != VK_SUCCESS || size == 0) return;
    std::vector<std::uint8_t> bytes(size);
    if (vkGetPipelineCacheData(device_, pipelineCache_, &size, bytes.data()) != VK_SUCCESS) return;
    std::error_code error;
    std::filesystem::create_directories(cacheDirectory_, error);
    const std::filesystem::path finalPath = pipelineCachePath_;
    std::filesystem::path temporaryPath = pipelineCachePath_;
    temporaryPath += ".tmp";
    {
        std::ofstream stream(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!stream) return;
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(size));
        if (!stream) return;
    }
    std::filesystem::remove(finalPath, error);
    error.clear();
    std::filesystem::rename(temporaryPath, finalPath, error);
}

void VulkanGraphicsBackend::updateMemoryDiagnostics()
{
    if (!allocator_) return;
    VmaTotalStatistics statistics {};
    vmaCalculateStatistics(allocator_, &statistics);
    diagnostics_.deviceMemoryBytes = static_cast<std::size_t>(statistics.total.statistics.allocationBytes);
}

void VulkanGraphicsBackend::setPreferences(const AppPreferences& preferences)
{
    composer_.setCameraShakeEnabled(!preferences.cameraShakeDisabled);
    if (frameLimitMode_ != preferences.frameLimitMode) {
        frameLimitMode_ = preferences.frameLimitMode;
        refreshFrameLimit();
    }
}

void VulkanGraphicsBackend::refreshFrameLimit()
{
    const SteamDeckRuntimeDetection deck = steamDeckDetector_.detect();
    const FrameLimitResolution resolution = resolveFrameLimit(
        frameLimitMode_, configuredRefreshRateHz_, deck.queryAvailable && deck.runningOnSteamDeck);
    presentDeadline_.configure(resolution.targetFramesPerSecond);
    diagnostics_.nominalTargetFramesPerSecond = resolution.nominalTargetFramesPerSecond;
    diagnostics_.targetFramesPerSecond = resolution.targetFramesPerSecond;
    diagnostics_.softwareFrameLimiterActive = resolution.targetFramesPerSecond > 0.0
        && (!resolution.refreshRateKnown
            || resolution.targetFramesPerSecond + 0.1 < resolution.activeRefreshRateHz);
}

void VulkanGraphicsBackend::applyFrameLimitAfterPresent()
{
    const std::uint64_t beforeDelay = SDL_GetTicksNS();
    const FrameDeadlineDecision decision = presentDeadline_.presented(beforeDelay);
    diagnostics_.presentIntervalMilliseconds =
        static_cast<double>(decision.observedPresentIntervalNanoseconds) / 1'000'000.0;
    if (decision.targetIntervalNanoseconds > 0
        && decision.observedPresentIntervalNanoseconds
            > decision.targetIntervalNanoseconds + decision.targetIntervalNanoseconds / 5U) {
        ++diagnostics_.missedRefreshes;
    }
    if (decision.delayNanoseconds > 0) {
        SDL_DelayPrecise(decision.delayNanoseconds);
        diagnostics_.limiterIdleMilliseconds +=
            static_cast<double>(SDL_GetTicksNS() - beforeDelay) / 1'000'000.0;
    }
}

void VulkanGraphicsBackend::setSteamDeckRuntimeDetector(SteamDeckRuntimeDetector detector) noexcept
{
    steamDeckDetector_ = detector;
    refreshFrameLimit();
}

void VulkanGraphicsBackend::requestSwapchainRebuild() noexcept
{
    swapchainRebuildRequested_ = true;
}

bool VulkanGraphicsBackend::requestFrameCapture()
{
    frameCaptureError_.clear();
    if (!initialized_ || !swapchain_) {
        frameCaptureError_ = "Vulkan frame capture requires an initialized non-zero swapchain.";
        return false;
    }
    if (!swapchainCaptureSupported_) {
        frameCaptureError_ = "The active Vulkan surface does not support swapchain image readback.";
        return false;
    }
    if (frameCaptureRequested_ || frameCaptureRecorded_ || completedFrameCapture_.has_value()) {
        frameCaptureError_ = "A Vulkan frame capture is already pending or awaiting collection.";
        return false;
    }
    frameCaptureRequested_ = true;
    return true;
}

std::optional<FrameCapture> VulkanGraphicsBackend::takeFrameCapture()
{
    std::optional<FrameCapture> result = std::move(completedFrameCapture_);
    completedFrameCapture_.reset();
    return result;
}

std::string_view VulkanGraphicsBackend::frameCaptureError() const noexcept
{
    return frameCaptureError_;
}

void VulkanGraphicsBackend::setError(std::string message)
{
    lastError_ = std::move(message);
    host_.log(PlatformLogLevel::Error, lastError_);
}

bool VulkanGraphicsBackend::check(VkResult result, std::string_view operation)
{
    if (result == VK_SUCCESS) return true;
    setError(std::string(operation) + " failed: " + vkResultName(result));
    return false;
}

GraphicsFrameStatus VulkanGraphicsBackend::frameStatus() const { return frameStatus_; }
RendererDiagnostics VulkanGraphicsBackend::diagnostics() const { return diagnostics_; }
std::string_view VulkanGraphicsBackend::description() const { return "Vulkan 1.3 / SDL3 surface"; }
std::string_view VulkanGraphicsBackend::deviceName() const noexcept { return deviceName_; }
std::string_view VulkanGraphicsBackend::lastError() const noexcept { return lastError_; }
double VulkanGraphicsBackend::targetFramesPerSecond() const noexcept
{
    return diagnostics_.targetFramesPerSecond;
}
VkDevice VulkanGraphicsBackend::device() const { return device_; }
VkPhysicalDevice VulkanGraphicsBackend::physicalDevice() const { return physicalDevice_; }
VmaAllocator VulkanGraphicsBackend::allocator() const { return allocator_; }
const VkAllocationCallbacks* VulkanGraphicsBackend::allocationCallbacks() const { return nullptr; }
VkPipelineCache VulkanGraphicsBackend::pipelineCache() const { return pipelineCache_; }
VkCommandBuffer VulkanGraphicsBackend::activeCommandBuffer() const
{
    return renderingActive_ && activeFrame_ ? activeFrame_->commandBuffer : VK_NULL_HANDLE;
}
VkFormat VulkanGraphicsBackend::swapchainFormat() const { return swapchainFormat_; }
VkExtent2D VulkanGraphicsBackend::swapchainExtent() const { return swapchainExtent_; }
std::uint64_t VulkanGraphicsBackend::frameSerial() const
{
    return nextFrameSerial_;
}
std::uint64_t VulkanGraphicsBackend::completedFrameSerial() const { return completedFrameSerial_; }
void VulkanGraphicsBackend::recordPipelineCreation()
{
    ++diagnostics_.pipelineCreationsThisFrame;
}

} // namespace rocket
