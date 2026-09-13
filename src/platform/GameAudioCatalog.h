#pragma once

#include "platform/AppServices.h"
#include <array>
#include <chrono>

namespace rocket {
// Count carried and banked ore together so stowing is not a second pickup.
// Drone payload is excluded until it is delivered to the ship.
inline int miningCollectedOreCount(const MiningRunState& mining) {
    const auto& carried = mining.temporaryMaterials;
    const auto& banked = mining.stowedMaterials;
    return carried.common + carried.rare + carried.exotic +
        banked.common + banked.rare + banked.exotic;
}
struct AudioCueDefinition {
    std::string_view path;
    double cooldown;
};
inline constexpr std::array<AudioCueDefinition, static_cast<std::size_t>(GameAudioCue::Count)> audioCueCatalog {{
    {"surface/safe_touchdown.wav", .3}, {"surface/hard_touchdown.wav", .3},
    {"surface/bay_open.wav", .3}, {"surface/rig_ejection.wav", .3},
    {"surface/arresting_burst.wav", .3}, {"surface/rig_impact.wav", .3},
    {"surface/drone_launch.wav", .06}, {"surface/bay_close.wav", .3},
    {"surface/surface_ready.wav", .3}, {"surface/takeoff_ignition.wav", .5},
    {"ui/focus.wav", .16}, {"ui/activate.wav", .1}, {"ui/cancel.wav", .15},
    {"ui/open.wav", .15}, {"ui/close.wav", .15}, {"ui/error.wav", .35}, {"ui/toggle.wav", .15},
    {"gameplay/upgrade.wav", .5}, {"gameplay/reward.wav", .5}, {"gameplay/ore_credit.wav", .25},
    {"gameplay/progression.wav", .6}, {"gameplay/damage.wav", .3}, {"gameplay/failure.wav", 1.0},
    {"gameplay/warning.wav", 3.0}, {"gameplay/scanner.wav", .4}, {"gameplay/tether.wav", .2},
    {"gameplay/drill.wav", .42}, {"gameplay/deposit.wav", .4}, {"gameplay/repair.wav", .4},
    {"gameplay/drone_task.wav", .7}, {"gameplay/drone_return.wav", .4},
    {"gameplay/engine_toggle.wav", .25}, {"gameplay/orbit.wav", .5}, {"gameplay/weapon.wav", .12},
    {"gameplay/thrust.wav", 0.0}, {"gameplay/ship_explosion.wav", 1.0}
}};
inline double audioClockSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline bool audioCueCritical(GameAudioCue cue) {
    return cue == GameAudioCue::ShipExplosion || cue == GameAudioCue::Failure || cue == GameAudioCue::Warning ||
        cue == GameAudioCue::Damage || cue == GameAudioCue::HardTouchdown;
}
class AudioCueLimiter {
public:
    bool admit(GameAudioCue cue, double now) {
        const auto i = static_cast<std::size_t>(cue);
        if (i >= next_.size() || now < next_[i]) return false;
        next_[i] = now + audioCueCatalog[i].cooldown;
        return true;
    }
private:
    std::array<double, audioCueCatalog.size()> next_ {};
};
} // namespace rocket
