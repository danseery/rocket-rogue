#pragma once

#include <algorithm>

namespace rocket {

// Presentation-only scene transition. Gameplay owns the action that happens
// once it completes; this type only provides a deterministic blackout envelope
// that can be shared by any future scene handoff.
class SceneTransition {
public:
    void beginFadeToBlack(double durationSeconds) noexcept
    {
        durationSeconds_ = std::max(0.001, durationSeconds);
        elapsedSeconds_ = 0.0;
        phase_ = Phase::FadingToBlack;
        active_ = true;
    }

    void beginFadeFromBlack(double durationSeconds) noexcept
    {
        durationSeconds_ = std::max(0.001, durationSeconds);
        elapsedSeconds_ = 0.0;
        phase_ = Phase::FadingFromBlack;
        active_ = true;
    }

    bool advance(double deltaSeconds) noexcept
    {
        if (!active_) {
            return false;
        }
        // Hold one rendered black frame before the owner swaps scenes. Without
        // this fence a fixed-step update could replace the outgoing scene on
        // the same frame that first reaches full opacity.
        if (phase_ == Phase::HoldingBlackFrame) {
            active_ = false;
            phase_ = Phase::Inactive;
            return true;
        }
        elapsedSeconds_ = std::min(
            durationSeconds_,
            elapsedSeconds_ + std::max(0.0, deltaSeconds));
        if (phase_ == Phase::FadingToBlack && elapsedSeconds_ >= durationSeconds_) {
            phase_ = Phase::HoldingBlackFrame;
        } else if (phase_ == Phase::FadingFromBlack && elapsedSeconds_ >= durationSeconds_) {
            active_ = false;
            phase_ = Phase::Inactive;
        }
        return false;
    }

    bool active() const noexcept { return active_; }

    double blackoutOpacity() const noexcept
    {
        if (!active_) {
            return 0.0;
        }
        const double progress = std::clamp(elapsedSeconds_ / durationSeconds_, 0.0, 1.0);
        return phase_ == Phase::FadingFromBlack ? 1.0 - progress : progress;
    }

    void clear() noexcept
    {
        active_ = false;
        phase_ = Phase::Inactive;
        elapsedSeconds_ = 0.0;
        durationSeconds_ = 0.0;
    }

private:
    enum class Phase {
        Inactive,
        FadingToBlack,
        HoldingBlackFrame,
        FadingFromBlack
    };

    bool active_ = false;
    Phase phase_ = Phase::Inactive;
    double elapsedSeconds_ = 0.0;
    double durationSeconds_ = 0.0;
};

} // namespace rocket
