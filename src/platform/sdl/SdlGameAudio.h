#pragma once

#include "platform/AppServices.h"
#include <SDL3/SDL_audio.h>

#include <filesystem>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

struct SDL_AudioStream;

namespace rocket {

class SdlGameAudio final : public IGameAudio {
public:
    SdlGameAudio(std::filesystem::path runtimeRoot, IPlatformHost& host);
    ~SdlGameAudio() override;

    bool playOneShot(const GameAudioEvent& event) override;
    void setThrust(double level) override;

private:
    SDL_AudioStream* thrustStream_ = nullptr;
    struct CachedSound {
        SDL_AudioSpec spec {};
        std::vector<Uint8> samples;
    };
    std::unordered_map<std::string, CachedSound> cache_;
    struct ActiveStream {
        SDL_AudioStream* stream = nullptr;
        std::uint64_t retireAtMilliseconds = 0;
    };
    void logOnce(std::string key, std::string message);

    std::filesystem::path runtimeRoot_;
    IPlatformHost& host_;
    bool audioAvailable_ = false;
    std::vector<ActiveStream> streams_;
    std::unordered_set<std::string> loggedFailures_;
};

} // namespace rocket
