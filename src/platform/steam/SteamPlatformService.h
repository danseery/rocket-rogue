#pragma once

#include "platform/FrameLimitPolicy.h"

#include <string>

namespace rocket {

// Optional Steamworks boundary. Normal desktop and CI builds compile the same
// no-SDK service without loading or linking Steam. A Steam packaging build can
// provide ROCKET_STEAMWORKS_SDK_ROOT to bind ISteamUtils before Vulkan starts.
class SteamPlatformService {
public:
    SteamPlatformService() = default;
    ~SteamPlatformService();

    SteamPlatformService(const SteamPlatformService&) = delete;
    SteamPlatformService& operator=(const SteamPlatformService&) = delete;

    bool initialize() noexcept;
    void shutdown() noexcept;
    SteamDeckRuntimeDetector deckDetector() const noexcept;
    bool sdkAvailable() const noexcept;
    bool initialized() const noexcept;
    const std::string& lastError() const noexcept;

private:
    SteamDeckRuntimeDetector deckDetector_;
    std::string lastError_;
    bool initialized_ = false;
};

} // namespace rocket

