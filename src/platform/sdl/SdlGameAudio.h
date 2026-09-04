#pragma once

#include "platform/AppServices.h"

#include <filesystem>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

struct SDL_AudioStream;

namespace rocket {

class SdlGameAudio final : public IGameAudio {
public:
    SdlGameAudio(std::filesystem::path runtimeRoot, IPlatformHost& host);
    ~SdlGameAudio() override;

    bool playOneShot(const GameAudioEvent& event) override;

private:
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
