#include "platform/sdl/SdlGameAudio.h"
#include "platform/GameAudioCatalog.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <utility>

namespace rocket {
SdlGameAudio::SdlGameAudio(std::filesystem::path runtimeRoot, IPlatformHost& host)
    : runtimeRoot_(std::move(runtimeRoot)), host_(host)
{
    audioAvailable_ = SDL_InitSubSystem(SDL_INIT_AUDIO);
    if (!audioAvailable_) {
        logOnce("audio-device", "SDL audio is unavailable; surface cues will remain silent.");
    }
}

SdlGameAudio::~SdlGameAudio()
{
    setThrust(0.0);
    for (const ActiveStream& active : streams_) {
        SDL_DestroyAudioStream(active.stream);
    }
    if (audioAvailable_) SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void SdlGameAudio::logOnce(std::string key, std::string message)
{
    if (loggedFailures_.insert(std::move(key)).second) {
        host_.log(PlatformLogLevel::Warning, message);
    }
}

void SdlGameAudio::setThrust(double level)
{
    level = std::clamp(level, 0.0, 1.0);
    if (level <= .01 || !audioAvailable_) {
        if (thrustStream_) SDL_DestroyAudioStream(thrustStream_);
        thrustStream_ = nullptr;
        return;
    }
    const auto path = runtimeRoot_ / "assets/audio/gameplay/thrust.wav";
    const auto key = path.string();
    if (loggedFailures_.contains(key)) return;
    auto found = cache_.find(key);
    if (found == cache_.end()) {
        CachedSound decoded;
        Uint8* data = nullptr;
        Uint32 size = 0;
        if (!SDL_LoadWAV(key.c_str(), &decoded.spec, &data, &size)) {
            logOnce(key, "Thrust audio unavailable: " + key);
            return;
        }
        decoded.samples.assign(data, data + size);
        SDL_free(data);
        found = cache_.emplace(key, std::move(decoded)).first;
    }
    const auto& sound = found->second;
    if (!thrustStream_) {
        thrustStream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &sound.spec, nullptr, nullptr);
        if (!thrustStream_) { logOnce(key, "Thrust playback device unavailable."); return; }
    }
    SDL_SetAudioStreamGain(thrustStream_, static_cast<float>(.35 * level));
    SDL_SetAudioStreamFrequencyRatio(thrustStream_, static_cast<float>(.85 + .3 * level));
    // Keep at least one full loop buffered across long frames, without stacking voices.
    while (SDL_GetAudioStreamQueued(thrustStream_) < static_cast<int>(sound.samples.size())) {
        if (!SDL_PutAudioStreamData(thrustStream_, sound.samples.data(), static_cast<int>(sound.samples.size()))) break;
    }
    SDL_ResumeAudioStreamDevice(thrustStream_);
}

bool SdlGameAudio::playOneShot(const GameAudioEvent& event)
{
    if (!audioAvailable_) return false;
    const std::uint64_t now = SDL_GetTicks();
    std::erase_if(streams_, [now](const ActiveStream& active) {
        if (now < active.retireAtMilliseconds) return false;
        SDL_DestroyAudioStream(active.stream);
        return true;
    });
    const auto cueIndex = static_cast<std::size_t>(event.cue);
    if (cueIndex >= audioCueCatalog.size() || streams_.size() >= 8U) return false;
    if (!audioCueCritical(event.cue) && streams_.size() >= 6U) return false;
    const std::filesystem::path path = runtimeRoot_ / "assets" / "audio" / audioCueCatalog[cueIndex].path;
    const std::string key = path.string();
    if (loggedFailures_.contains(key)) return false;
    auto found = cache_.find(key);
    if (found == cache_.end()) {
        CachedSound decoded;
        Uint8* buffer = nullptr;
        Uint32 length = 0;
        if (!SDL_LoadWAV(key.c_str(), &decoded.spec, &buffer, &length)) {
            logOnce(key, "Audio cue unavailable: " + key);
            return false;
        }
        decoded.samples.assign(buffer, buffer + length);
        SDL_free(buffer);
        found = cache_.emplace(key, std::move(decoded)).first;
    }
    const auto& spec = found->second.spec;
    const auto& samples = found->second.samples;

    SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &spec,
        nullptr,
        nullptr);
    if (stream == nullptr) {
        logOnce("audio-device", "Could not open the SDL playback device for surface audio.");
        return false;
    }
    const float pitch = static_cast<float>(std::clamp(event.pitch, 0.75, 1.25));
    const bool queued = SDL_SetAudioStreamFrequencyRatio(stream, pitch) &&
        SDL_SetAudioStreamGain(stream, 0.35f) &&
        SDL_PutAudioStreamData(stream, samples.data(), static_cast<int>(samples.size())) &&
        SDL_FlushAudioStream(stream) &&
        SDL_ResumeAudioStreamDevice(stream);
    if (!queued) {
        SDL_DestroyAudioStream(stream);
        logOnce(path.string() + "#play", "Could not play surface audio cue: " + path.string());
        return false;
    }
    const double bytesPerSecond = static_cast<double>(std::max(1, spec.freq)) *
        std::max(1, static_cast<int>(SDL_AUDIO_FRAMESIZE(spec))) * static_cast<double>(pitch);
    const auto durationMilliseconds = static_cast<std::uint64_t>(
        static_cast<double>(samples.size()) * 1000.0 / bytesPerSecond);
    streams_.push_back({stream, now + durationMilliseconds + 250U});
    return true;
}

} // namespace rocket
