#include "platform/sdl/SdlGameAudio.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <utility>

namespace rocket {
namespace {

std::string_view cueFilename(GameAudioCue cue)
{
    switch (cue) {
    case GameAudioCue::SafeTouchdown: return "safe_touchdown.wav";
    case GameAudioCue::HardTouchdown: return "hard_touchdown.wav";
    case GameAudioCue::BayOpen: return "bay_open.wav";
    case GameAudioCue::RigEjection: return "rig_ejection.wav";
    case GameAudioCue::ArrestingBurst: return "arresting_burst.wav";
    case GameAudioCue::RigImpact: return "rig_impact.wav";
    case GameAudioCue::DroneLaunch: return "drone_launch.wav";
    case GameAudioCue::BayClose: return "bay_close.wav";
    case GameAudioCue::SurfaceReady: return "surface_ready.wav";
    case GameAudioCue::TakeoffIgnition: return "takeoff_ignition.wav";
    }
    return {};
}

} // namespace

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

bool SdlGameAudio::playOneShot(const GameAudioEvent& event)
{
    if (!audioAvailable_) return false;
    const std::uint64_t now = SDL_GetTicks();
    std::erase_if(streams_, [now](const ActiveStream& active) {
        if (now < active.retireAtMilliseconds) return false;
        SDL_DestroyAudioStream(active.stream);
        return true;
    });
    const std::string_view filename = cueFilename(event.cue);
    const std::filesystem::path path = runtimeRoot_ / "assets" / "audio" / "surface" / filename;
    if (!std::filesystem::exists(path)) {
        logOnce(
            path.string(),
            "Surface audio cue is silent until an approved WAV is supplied: " + path.string());
        return false;
    }

    SDL_AudioSpec spec {};
    Uint8* buffer = nullptr;
    Uint32 length = 0;
    if (!SDL_LoadWAV(path.string().c_str(), &spec, &buffer, &length)) {
        logOnce(path.string(), "Could not load surface audio cue: " + path.string());
        return false;
    }

    SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &spec,
        nullptr,
        nullptr);
    if (stream == nullptr) {
        SDL_free(buffer);
        logOnce("audio-device", "Could not open the SDL playback device for surface audio.");
        return false;
    }
    const float pitch = static_cast<float>(std::clamp(event.pitch, 0.75, 1.25));
    const bool queued = SDL_SetAudioStreamFrequencyRatio(stream, pitch) &&
        SDL_PutAudioStreamData(stream, buffer, static_cast<int>(length)) &&
        SDL_FlushAudioStream(stream) &&
        SDL_ResumeAudioStreamDevice(stream);
    SDL_free(buffer);
    if (!queued) {
        SDL_DestroyAudioStream(stream);
        logOnce(path.string() + "#play", "Could not play surface audio cue: " + path.string());
        return false;
    }
    const double bytesPerSecond = static_cast<double>(std::max(1, spec.freq)) *
        std::max(1, static_cast<int>(SDL_AUDIO_FRAMESIZE(spec))) * static_cast<double>(pitch);
    const auto durationMilliseconds = static_cast<std::uint64_t>(
        static_cast<double>(length) * 1000.0 / bytesPerSecond);
    streams_.push_back({stream, now + durationMilliseconds + 250U});
    return true;
}

} // namespace rocket
