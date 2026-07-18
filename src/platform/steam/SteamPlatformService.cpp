#include "platform/steam/SteamPlatformService.h"

#if defined(ROCKET_STEAMWORKS_ENABLED)
#include <steam/steam_api.h>
#endif

namespace rocket {

SteamPlatformService::~SteamPlatformService()
{
    shutdown();
}

bool SteamPlatformService::initialize() noexcept
{
    lastError_.clear();
    if (initialized_) return true;
#if defined(ROCKET_STEAMWORKS_ENABLED)
    if (!SteamAPI_Init()) {
        lastError_ = "Steamworks is packaged but SteamAPI_Init failed; continuing without Deck-specific defaults.";
        return false;
    }
    initialized_ = true;
    deckDetector_ = SteamDeckRuntimeDetector::bindSteamUtils(SteamUtils());
    return true;
#else
    initialized_ = true;
    return true;
#endif
}

void SteamPlatformService::shutdown() noexcept
{
#if defined(ROCKET_STEAMWORKS_ENABLED)
    if (initialized_) SteamAPI_Shutdown();
#endif
    deckDetector_.clear();
    initialized_ = false;
}

SteamDeckRuntimeDetector SteamPlatformService::deckDetector() const noexcept
{
    return deckDetector_;
}

bool SteamPlatformService::sdkAvailable() const noexcept
{
#if defined(ROCKET_STEAMWORKS_ENABLED)
    return true;
#else
    return false;
#endif
}

bool SteamPlatformService::initialized() const noexcept { return initialized_; }
const std::string& SteamPlatformService::lastError() const noexcept { return lastError_; }

} // namespace rocket
